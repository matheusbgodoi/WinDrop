/* SPDX-License-Identifier: BSD-3-Clause-Clear */
#include "mt7902_hw.h"
#include "mt7902_regs.h"
#include "../tools/regproxy_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TX_MCU_NDESC 64
#define TX_FWDL_NDESC 64
/* At most 256, so a ring's descriptors fit one 4 KiB page of the slab. */
#define RX_EVENT_NDESC 256
#define RX_DATA_NDESC 256
#define RX_BUF_BYTES 2048
#define TX_MCU_BUF_BYTES 2048
#define FWDL_CHUNK_BYTES 4096
#define DMA_SLAB_BYTES ((size_t)2 << 20)
/* Descriptors carry the low 32 address bits; the high-bits field is unused. */
#define DMA_ADDRESS_BITS 32

#define MCU_TXD_BYTES 64
#define MCU_UNI_TXD_BYTES 48
#define MCU_RXD_BYTES 36
#define MCU_RXD_SEQ_OFFSET 29

#define log_err(...) fprintf(stderr, "mt7902: " __VA_ARGS__)

int mt7902_trace;

/* ---- registers ---- */

static uint32_t raw_rr(mt7902_dev *dev, uint32_t offset) {
  unsigned value = 0;
  if (!regproxy_read(dev->proxy, offset, 4, &value)) {
    if (!dev->io_failed) log_err("register read 0x%05x failed\n", offset);
    dev->io_failed = 1;
    return 0xffffffffu;
  }
  return value;
}

static void raw_wr(mt7902_dev *dev, uint32_t offset, uint32_t value) {
  if (!regproxy_write(dev->proxy, offset, 4, value)) {
    if (!dev->io_failed) log_err("register write 0x%05x failed\n", offset);
    dev->io_failed = 1;
  }
}

/* mt7921's __mt7921_reg_addr: fixed windows first, then the L1 remap window. */
static uint32_t reg_addr(mt7902_dev *dev, uint32_t addr) {
  static const struct {
    uint32_t phys, maps, size;
  } fixed_map[] = {
      {0x820d0000, 0x30000, 0x10000}, {0x820ed000, 0x24800, 0x00800},
      {0x820e4000, 0x21000, 0x00400}, {0x820e7000, 0x21e00, 0x00200},
      {0x820eb000, 0x24200, 0x00400}, {0x820e2000, 0x20800, 0x00400},
      {0x820e3000, 0x20c00, 0x00400}, {0x820e5000, 0x21400, 0x00800},
      {0x00400000, 0x80000, 0x10000}, {0x00410000, 0x90000, 0x10000},
      {0x40000000, 0x70000, 0x10000}, {0x54000000, 0x02000, 0x01000},
      {0x55000000, 0x03000, 0x01000}, {0x58000000, 0x06000, 0x01000},
      {0x59000000, 0x07000, 0x01000}, {0x7c000000, 0xf0000, 0x10000},
      {0x7c020000, 0xd0000, 0x10000}, {0x7c060000, 0xe0000, 0x10000},
      {0x80020000, 0xb0000, 0x10000}, {0x81020000, 0xc0000, 0x10000},
      {0x820c0000, 0x08000, 0x04000}, {0x820c8000, 0x0c000, 0x02000},
      {0x820cc000, 0x0e000, 0x01000}, {0x820cd000, 0x0f000, 0x01000},
      {0x74030000, 0x10000, 0x10000}, {0x820ce000, 0x21c00, 0x00200},
      {0x820cf000, 0x22000, 0x01000}, {0x820e0000, 0x20000, 0x00400},
      {0x820e1000, 0x20400, 0x00200}, {0x820e9000, 0x23400, 0x00200},
      {0x820ea000, 0x24000, 0x00200}, {0x820ec000, 0x24600, 0x00200},
  };
  if (addr < 0x100000) return addr;
  for (size_t i = 0; i < sizeof(fixed_map) / sizeof(fixed_map[0]); i++) {
    if (addr < fixed_map[i].phys) continue;
    uint32_t ofs = addr - fixed_map[i].phys;
    if (ofs > fixed_map[i].size) continue;
    return fixed_map[i].maps + ofs;
  }
  if ((addr >= 0x18000000 && addr < 0x18c00000) ||
      (addr >= 0x70000000 && addr < 0x78000000) ||
      (addr >= 0x7c000000 && addr < 0x7c400000)) {
    uint32_t remap = raw_rr(dev, MT_HIF_REMAP_L1);
    raw_wr(dev, MT_HIF_REMAP_L1, (remap & 0xffff0000u) | (addr >> 16));
    raw_rr(dev, MT_HIF_REMAP_L1); /* flushes the write */
    return MT_HIF_REMAP_BASE_L1 + (addr & 0xffffu);
  }
  log_err("address 0x%08x has no mapping\n", addr);
  dev->io_failed = 1;
  return 0;
}

uint32_t mt7902_rr(mt7902_dev *dev, uint32_t addr) {
  return raw_rr(dev, reg_addr(dev, addr));
}

void mt7902_wr(mt7902_dev *dev, uint32_t addr, uint32_t value) {
  raw_wr(dev, reg_addr(dev, addr), value);
}

static void reg_set(mt7902_dev *dev, uint32_t addr, uint32_t bits) {
  uint32_t offset = reg_addr(dev, addr);
  raw_wr(dev, offset, raw_rr(dev, offset) | bits);
}

static void reg_clear(mt7902_dev *dev, uint32_t addr, uint32_t bits) {
  uint32_t offset = reg_addr(dev, addr);
  raw_wr(dev, offset, raw_rr(dev, offset) & ~bits);
}

static uint64_t now_ms(void) { return GetTickCount64(); }

/* Waits until (reg & mask) == want. Returns 0 when it happened in time. */
static int reg_poll(mt7902_dev *dev, uint32_t addr, uint32_t mask,
                    uint32_t want, unsigned timeout_ms) {
  uint64_t deadline = now_ms() + timeout_ms;
  for (;;) {
    if ((mt7902_rr(dev, addr) & mask) == want) return 0;
    if (dev->io_failed || now_ms() > deadline) return -1;
    SwitchToThread();
  }
}

/* ---- DMA memory and rings ---- */

/* Hands out slab space; an object never straddles a chunk, the unit inside
 * which the slab is physically contiguous. */
static size_t slab_alloc(mt7902_dev *dev, size_t bytes, size_t align) {
  size_t chunk = dev->mem.chunk_bytes;
  size_t at = (dev->mem_used + align - 1) & ~(align - 1);
  if (bytes > chunk) return (size_t)-1;
  if (at / chunk != (at + bytes - 1) / chunk) at = (at / chunk + 1) * chunk;
  if (at + bytes > dev->mem.chunk_count * chunk) return (size_t)-1;
  dev->mem_used = at + bytes;
  return at;
}

static int ring_alloc(mt7902_dev *dev, mt7902_ring *ring, uint32_t regs,
                      uint32_t ndesc, uint32_t buf_bytes) {
  size_t desc_at = slab_alloc(dev, (size_t)ndesc * 16, 4096);
  ring->buf_offset = calloc(ndesc, sizeof(*ring->buf_offset));
  if (desc_at == (size_t)-1 || !ring->buf_offset) return -1;
  ring->regs = regs;
  ring->ndesc = ndesc;
  ring->buf_bytes = buf_bytes;
  ring->desc = (uint32_t *)(dev->mem.va + desc_at);
  ring->head = 0;
  for (uint32_t i = 0; i < ndesc; i++) {
    ring->buf_offset[i] = slab_alloc(dev, buf_bytes, buf_bytes);
    if (ring->buf_offset[i] == (size_t)-1) return -1;
  }
  raw_wr(dev, regs + MT_RING_DESC_BASE,
         (uint32_t)dma_mem_phys(&dev->mem, desc_at));
  raw_wr(dev, regs + MT_RING_SIZE_REG, ndesc);
  return 0;
}

static void tx_ring_start(mt7902_dev *dev, mt7902_ring *ring) {
  for (uint32_t i = 0; i < ring->ndesc; i++) {
    uint32_t *d = ring->desc + 4 * i;
    d[0] = (uint32_t)dma_mem_phys(&dev->mem, ring->buf_offset[i]);
    d[1] = MT_DMA_CTL_DMA_DONE;
    d[2] = d[3] = 0;
  }
  raw_wr(dev, ring->regs + MT_RING_CPU_IDX, 0);
  ring->head = raw_rr(dev, ring->regs + MT_RING_DMA_IDX) % ring->ndesc;
}

static void rx_desc_arm(mt7902_ring *ring, uint32_t i) {
  ring->desc[4 * i + 1] = ring->buf_bytes << MT_DMA_CTL_SD_LEN0_SHIFT;
}

/* The chip owns every descriptor but the one cpu_idx points at. */
static void rx_ring_start(mt7902_dev *dev, mt7902_ring *ring) {
  for (uint32_t i = 0; i < ring->ndesc; i++) {
    uint32_t *d = ring->desc + 4 * i;
    d[0] = (uint32_t)dma_mem_phys(&dev->mem, ring->buf_offset[i]);
    d[2] = d[3] = 0;
    rx_desc_arm(ring, i);
  }
  ring->head = raw_rr(dev, ring->regs + MT_RING_DMA_IDX) % ring->ndesc;
  raw_wr(dev, ring->regs + MT_RING_CPU_IDX,
         (ring->head + ring->ndesc - 1) % ring->ndesc);
}

int mt7902_rx_poll(mt7902_dev *dev, mt7902_ring *ring, mt7902_rx *out) {
  uint32_t dma_idx = raw_rr(dev, ring->regs + MT_RING_DMA_IDX) % ring->ndesc;
  if (dma_idx == ring->head) return 0;
  uint32_t i = ring->head;
  uint32_t ctrl = ring->desc[4 * i + 1];
  if (!(ctrl & MT_DMA_CTL_DMA_DONE)) return 0;
  out->data = dev->mem.va + ring->buf_offset[i];
  out->len = (ctrl >> MT_DMA_CTL_SD_LEN0_SHIFT) & MT_DMA_CTL_SD_LEN0_MASK;
  if (out->len > ring->buf_bytes) out->len = ring->buf_bytes;
  /* Slot i stays ours until a later poll moves cpu_idx past it. */
  rx_desc_arm(ring, i);
  raw_wr(dev, ring->regs + MT_RING_CPU_IDX, i);
  ring->head = (i + 1) % ring->ndesc;
  return 1;
}

/* Queues one buffer and waits for the chip to take it. */
static int tx_send(mt7902_dev *dev, mt7902_ring *ring, const void *head,
                   size_t head_len, const void *body, size_t body_len) {
  size_t len = head_len + body_len;
  if (len > ring->buf_bytes) return -1;
  uint32_t i = ring->head;
  uint8_t *buf = dev->mem.va + ring->buf_offset[i];
  if (head_len) memcpy(buf, head, head_len);
  if (body_len) memcpy(buf + head_len, body, body_len);
  uint32_t *d = ring->desc + 4 * i;
  d[2] = d[3] = 0;
  d[1] = ((uint32_t)len << MT_DMA_CTL_SD_LEN0_SHIFT) | MT_DMA_CTL_LAST_SEC0;
  ring->head = (i + 1) % ring->ndesc;
  raw_wr(dev, ring->regs + MT_RING_CPU_IDX, ring->head);

  uint64_t deadline = now_ms() + 1000;
  while (raw_rr(dev, ring->regs + MT_RING_DMA_IDX) % ring->ndesc != ring->head) {
    if (dev->io_failed || now_ms() > deadline) {
      log_err("TX ring at 0x%05x: chip did not take descriptor %u\n",
              ring->regs, i);
      return -1;
    }
    SwitchToThread();
  }
  return 0;
}

/* ---- power, reset, DMA ---- */

static int set_ownership(mt7902_dev *dev, int driver) {
  for (int attempt = 0; attempt < 10; attempt++) {
    mt7902_wr(dev, MT_CONN_ON_LPCTL,
              driver ? PCIE_LPCR_HOST_CLR_OWN : PCIE_LPCR_HOST_SET_OWN);
    if (!reg_poll(dev, MT_CONN_ON_LPCTL, PCIE_LPCR_HOST_OWN_SYNC,
                  driver ? 0 : PCIE_LPCR_HOST_OWN_SYNC, 50))
      return 0;
  }
  log_err("%s ownership not granted\n", driver ? "driver" : "firmware");
  return -1;
}

static int wfsys_reset(mt7902_dev *dev) {
  reg_clear(dev, MT_WFSYS_SW_RST_B, WFSYS_SW_RST_B);
  Sleep(50);
  reg_set(dev, MT_WFSYS_SW_RST_B, WFSYS_SW_RST_B);
  if (reg_poll(dev, MT_WFSYS_SW_RST_B, WFSYS_SW_INIT_DONE, WFSYS_SW_INIT_DONE,
               500)) {
    log_err("WFSYS reset did not complete\n");
    return -1;
  }
  return 0;
}

static int dma_stop(mt7902_dev *dev) {
  reg_clear(dev, MT_WFDMA0_GLO_CFG,
            MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN |
                MT_WFDMA0_GLO_CFG_CSR_DISP_BASE_PTR_CHAIN_EN |
                MT_WFDMA0_GLO_CFG_OMIT_TX_INFO | MT_WFDMA0_GLO_CFG_OMIT_RX_INFO |
                MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2);
  return reg_poll(dev, MT_WFDMA0_GLO_CFG,
                  MT_WFDMA0_GLO_CFG_TX_DMA_BUSY | MT_WFDMA0_GLO_CFG_RX_DMA_BUSY,
                  0, 100);
}

static void dma_logic_reset(mt7902_dev *dev) {
  uint32_t bits = MT_WFDMA0_RST_DMASHDL_ALL_RST | MT_WFDMA0_RST_LOGIC_RST;
  reg_clear(dev, MT_WFDMA0_RST, bits);
  reg_set(dev, MT_WFDMA0_RST, bits);
}

/* The driver replays this when our handle closes, however the process ends,
 * so the chip never writes into memory Windows has taken back. */
static int arm_dma_stop_on_close(mt7902_dev *dev) {
  REGPROXY_OP op = {0};
  op.offset = MT_WFDMA0_GLO_CFG;
  op.value = MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN;
  op.width = 4;
  op.kind = REGPROXY_CLEAR_BITS;
  DWORD returned = 0;
  return DeviceIoControl(dev->proxy, IOCTL_REGPROXY_SET_CLOSE_OPS, &op,
                         sizeof(op), NULL, 0, &returned, NULL)
             ? 0
             : -1;
}

static void dma_prefetch(mt7902_dev *dev) {
  static const uint8_t rx_rings[] = {0, 1, 2, 3};
  static const uint8_t tx_rings[] = {0, 1, 2, 3, 4, 5, 6, 15, 16};
  uint32_t base = 0;
  for (size_t i = 0; i < sizeof(rx_rings); i++, base += 0x40)
    raw_wr(dev, MT_WFDMA0_RX_RING_EXT_CTRL(rx_rings[i]), base << 16 | 0x4);
  for (size_t i = 0; i < sizeof(tx_rings); i++, base += 0x40)
    raw_wr(dev, MT_WFDMA0_TX_RING_EXT_CTRL(tx_rings[i]), base << 16 | 0x4);
}

int mt7902_open(mt7902_dev *dev) {
  memset(dev, 0, sizeof(*dev));
  dev->proxy = regproxy_open();
  if (dev->proxy == INVALID_HANDLE_VALUE) {
    log_err("register proxy not openable: is the driver bound and the "
            "process elevated?\n");
    return -1;
  }
  return 0;
}

void mt7902_close(mt7902_dev *dev) {
  if (dev->proxy && dev->proxy != INVALID_HANDLE_VALUE) {
    if (dev->mem.va) {
      dev->io_failed = 0;
      if (dma_stop(dev)) log_err("DMA still busy at close\n");
      dma_logic_reset(dev);
    }
    CloseHandle(dev->proxy);
  }
  /* Only after the chip stopped using it. */
  dma_mem_free(&dev->mem);
  free(dev->tx_mcu.buf_offset);
  free(dev->tx_fwdl.buf_offset);
  free(dev->rx_event.buf_offset);
  free(dev->rx_data.buf_offset);
  memset(dev, 0, sizeof(*dev));
}

int mt7902_bringup(mt7902_dev *dev) {
  if (set_ownership(dev, 0) || set_ownership(dev, 1)) return -1;
  uint32_t chip = mt7902_rr(dev, MT_HW_CHIPID) & 0xffff;
  if (chip != 0x7902) {
    log_err("unexpected chip id 0x%04x\n", chip);
    return -1;
  }
  if (wfsys_reset(dev)) return -1;

  /* Nothing here handles interrupts: every ring is polled. */
  raw_wr(dev, MT_WFDMA0_HOST_INT_ENA, 0);
  raw_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0);

  if (dma_stop(dev)) {
    log_err("DMA did not go idle\n");
    return -1;
  }
  reg_clear(dev, MT_WFDMA0_GLO_CFG_EXT0, MT_WFDMA0_GLO_CFG_EXT0_CSR_TX_DMASHDL_EN);
  reg_set(dev, MT_DMASHDL_SW_CONTROL, MT_DMASHDL_DMASHDL_BYPASS);
  dma_logic_reset(dev);

  int mem_rc = dma_mem_alloc(&dev->mem, DMA_SLAB_BYTES, DMA_ADDRESS_BITS);
  if (mem_rc) {
    log_err("%s\n", mem_rc == -1
                        ? "this account may not lock pages in memory"
                        : "not enough free memory below 4 GiB for the DMA slab");
    return -1;
  }
  if (arm_dma_stop_on_close(dev)) {
    log_err("could not arm the DMA stop on close, refusing to enable DMA\n");
    return -1;
  }

  if (ring_alloc(dev, &dev->tx_mcu, MT_TX_RING(MT7902_TXQ_MCU_WM), TX_MCU_NDESC,
                 TX_MCU_BUF_BYTES) ||
      ring_alloc(dev, &dev->tx_fwdl, MT_TX_RING(MT7902_TXQ_FWDL), TX_FWDL_NDESC,
                 FWDL_CHUNK_BYTES) ||
      ring_alloc(dev, &dev->rx_event, MT_RX_RING(MT7902_RXQ_EVENT),
                 RX_EVENT_NDESC, RX_BUF_BYTES) ||
      ring_alloc(dev, &dev->rx_data, MT_RX_RING(MT7902_RXQ_DATA), RX_DATA_NDESC,
                 RX_BUF_BYTES)) {
    log_err("DMA slab too small for the rings\n");
    return -1;
  }

  dma_prefetch(dev);
  raw_wr(dev, MT_WFDMA0_RST_DTX_PTR, 0xffffffffu);
  tx_ring_start(dev, &dev->tx_mcu);
  tx_ring_start(dev, &dev->tx_fwdl);
  rx_ring_start(dev, &dev->rx_event);
  rx_ring_start(dev, &dev->rx_data);

  raw_wr(dev, MT_WFDMA0_PRI_DLY_INT_CFG0, 0);
  reg_set(dev, MT_WFDMA0_GLO_CFG,
          MT_WFDMA0_GLO_CFG_TX_WB_DDONE | MT_WFDMA0_GLO_CFG_FIFO_LITTLE_ENDIAN |
              MT_WFDMA0_GLO_CFG_CLK_GAT_DIS | MT_WFDMA0_GLO_CFG_OMIT_TX_INFO |
              MT_WFDMA0_GLO_CFG_DMA_SIZE_3 | MT_WFDMA0_GLO_CFG_FIFO_DIS_CHECK |
              MT_WFDMA0_GLO_CFG_RX_WB_DDONE |
              MT_WFDMA0_GLO_CFG_CSR_DISP_BASE_PTR_CHAIN_EN |
              MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2);
  reg_set(dev, MT_WFDMA0_GLO_CFG,
          MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN);
  reg_set(dev, MT_WFDMA_DUMMY_CR, MT_WFDMA_NEED_REINIT);

  /* mt7921e_mcu_init */
  mt7902_wr(dev, MT_TOP_LPCR_HOST_BAND0, MT_TOP_LPCR_HOST_DRV_OWN);
  if (reg_poll(dev, MT_TOP_LPCR_HOST_BAND0, MT_TOP_LPCR_HOST_FW_OWN, 0, 500)) {
    log_err("timeout for driver own\n");
    return -1;
  }
  reg_set(dev, MT_PCIE_MAC_PM, MT_PCIE_MAC_PM_L0S_DIS);
  return dev->io_failed ? -1 : 0;
}

/* ---- MCU ---- */

static void put_le16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }
static void put_le32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static uint32_t get_le32(const uint8_t *p) {
  uint32_t v;
  memcpy(&v, p, 4);
  return v;
}
static uint32_t get_be32(const uint8_t *p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

int mt7902_mcu_cmd(mt7902_dev *dev, uint32_t cmd, const void *payload,
                   size_t len, uint8_t *response, size_t response_cap) {
  uint8_t cid = cmd & 0xff, ext_cid = cmd >> 8 & 0xff;
  uint8_t seq = ++dev->mcu_seq & 0xf;
  if (!seq) seq = ++dev->mcu_seq & 0xf;

  if (cmd == MCU_CMD_FW_SCATTER) /* raw data on its own ring, no header */
    return tx_send(dev, &dev->tx_fwdl, NULL, 0, payload, len);

  /* struct mt76_connac2_mcu_txd or mt76_connac2_mcu_uni_txd, filled as
   * mt76_connac2_mcu_fill_message does. Both keep seq at byte 39. */
  uint8_t txd[MCU_TXD_BYTES] = {0};
  size_t txd_len = cmd & MCU_CMD_FIELD_UNI ? MCU_UNI_TXD_BYTES : MCU_TXD_BYTES;
  size_t total = txd_len + len;
  put_le32(txd + 0, (uint32_t)total | 2u << 23 /* PKT_FMT cmd */ |
                        0x20u << 25 /* Q_IDX MCU_PORT_RX_Q0 */);
  put_le32(txd + 4, BIT(31) /* long format */ | 1u << 16 /* HDR_FORMAT cmd */);
  put_le16(txd + 32, (uint16_t)(total - 32));
  txd[37] = MCU_PKT_ID;
  txd[39] = seq;
  if (cmd & MCU_CMD_FIELD_UNI) {
    put_le16(txd + 34, cid);
    txd[43] = MCU_CMD_UNI_EXT_ACK; /* option; s2d_index stays host-to-N9 */
  } else {
    put_le16(txd + 34, 1u << 15 | 0x20u << 10); /* pq_id: MCU port, RX_Q0 */
    txd[36] = cid;
    txd[41] = ext_cid;
    if (ext_cid || (cmd & MCU_CMD_FIELD_CE)) {
      txd[38] = cmd & MCU_CMD_FIELD_QUERY ? MCU_Q_QUERY : MCU_Q_SET;
      txd[43] = ext_cid != 0; /* ext_cid_ack */
    } else {
      txd[38] = MCU_Q_NA;
    }
  }
  if (mt7902_trace)
    printf("cmd 0x%05x seq %u sent with INT_STA 0x%08x, MCU_CMD 0x%08x\n",
           cmd, seq, raw_rr(dev, MT_WFDMA0_HOST_INT_STA), raw_rr(dev, MT_MCU_CMD));
  if (tx_send(dev, &dev->tx_mcu, txd, txd_len, payload, len)) return -1;
  if (!response) return 0;

  uint64_t deadline = now_ms() + 3000;
  mt7902_ring *rings[] = {&dev->rx_event, &dev->rx_data};
  for (;;) {
    int got = 0;
    for (int r = 0; r < 2; r++) {
      mt7902_rx rx;
      if (!mt7902_rx_poll(dev, rings[r], &rx)) continue;
      got = 1;
      if (mt7902_trace) {
        printf("cmd 0x%05x seq %u, ring %d, %u bytes:", cmd, seq, r, rx.len);
        for (uint32_t i = 0; i < rx.len && i < 48; i++)
          printf("%s%02x", i % 4 ? "" : " ", rx.data[i]);
        printf("\n");
      }
      /* Events only: a received frame could carry the same byte by chance. */
      if (rx.len > MCU_RXD_SEQ_OFFSET && rx.data[3] >> 3 == PKT_TYPE_RX_EVENT &&
          rx.data[MCU_RXD_SEQ_OFFSET] == seq) {
        size_t n = rx.len < response_cap ? rx.len : response_cap;
        memcpy(response, rx.data, n);
        return (int)n;
      }
    }
    if (got) continue;
    if (dev->io_failed || now_ms() > deadline) {
      log_err("command 0x%05x (seq %u) got no response; INT_STA 0x%08x, "
              "MCU_CMD 0x%08x, event ring cpu %u dma %u\n",
              cmd, seq, raw_rr(dev, MT_WFDMA0_HOST_INT_STA),
              raw_rr(dev, MT_MCU_CMD),
              raw_rr(dev, dev->rx_event.regs + MT_RING_CPU_IDX),
              raw_rr(dev, dev->rx_event.regs + MT_RING_DMA_IDX));
      log_err("  LPCTL 0x%x, MISC 0x%x, GLO_CFG 0x%08x, DUMMY_CR 0x%x, cmd ring "
              "cpu %u dma %u\n",
              mt7902_rr(dev, MT_CONN_ON_LPCTL), mt7902_rr(dev, MT_CONN_ON_MISC),
              raw_rr(dev, MT_WFDMA0_GLO_CFG), mt7902_rr(dev, MT_WFDMA_DUMMY_CR),
              raw_rr(dev, dev->tx_mcu.regs + MT_RING_CPU_IDX),
              raw_rr(dev, dev->tx_mcu.regs + MT_RING_DMA_IDX));
      return -1;
    }
    SwitchToThread();
  }
}

/* The status byte of patch semaphore and patch finish responses. */
static int mcu_cmd_status(mt7902_dev *dev, uint32_t cid, const void *payload,
                          size_t len) {
  uint8_t rsp[64];
  int n = mt7902_mcu_cmd(dev, cid, payload, len, rsp, sizeof(rsp));
  if (n < MCU_RXD_BYTES - 4 + 1) return -1;
  return rsp[MCU_RXD_BYTES - 4];
}

static int mcu_cmd_acked(mt7902_dev *dev, uint32_t cid, const void *payload,
                         size_t len) {
  uint8_t rsp[64];
  return mt7902_mcu_cmd(dev, cid, payload, len, rsp, sizeof(rsp)) < 0 ? -1 : 0;
}

static int init_download(mt7902_dev *dev, uint32_t addr, uint32_t len,
                         uint32_t mode) {
  uint8_t req[12];
  put_le32(req, addr);
  put_le32(req + 4, len);
  put_le32(req + 8, mode);
  uint8_t cid = addr == MCU_PATCH_ADDRESS || addr == 0x900000
                    ? MCU_CMD_PATCH_START_REQ
                    : MCU_CMD_TARGET_ADDRESS_LEN_REQ;
  return mcu_cmd_acked(dev, cid, req, sizeof(req));
}

static int send_scatter(mt7902_dev *dev, const uint8_t *data, uint32_t len) {
  while (len) {
    uint32_t chunk = len < FWDL_CHUNK_BYTES ? len : FWDL_CHUNK_BYTES;
    if (mt7902_mcu_cmd(dev, MCU_CMD_FW_SCATTER, data, chunk, NULL, 0)) return -1;
    data += chunk;
    len -= chunk;
  }
  return 0;
}

static uint8_t *read_file(const char *path, size_t *size) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    log_err("cannot open %s\n", path);
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *data = n > 0 ? malloc((size_t)n) : NULL;
  if (data && fread(data, 1, (size_t)n, f) != (size_t)n) {
    free(data);
    data = NULL;
  }
  fclose(f);
  *size = data ? (size_t)n : 0;
  return data;
}

#define PATCH_HDR_BYTES 96
#define PATCH_SEC_BYTES 64

static uint32_t patch_data_mode(uint32_t sec_info) {
  uint32_t mode = DL_MODE_NEED_RSP;
  if (sec_info == PATCH_SEC_NOT_SUPPORT) return mode;
  switch (sec_info >> 24) {
  case PATCH_SEC_ENC_TYPE_AES:
    mode |= DL_MODE_ENCRYPT | DL_MODE_RESET_SEC_IV |
            ((sec_info & 0xff) << DL_MODE_KEY_IDX_SHIFT & 0x6);
    break;
  case PATCH_SEC_ENC_TYPE_SCRAMBLE:
    mode |= DL_MODE_ENCRYPT | DL_CONFIG_ENCRY_MODE_SEL | DL_MODE_RESET_SEC_IV;
    break;
  }
  return mode;
}

static int send_patch_sections(mt7902_dev *dev, const uint8_t *fw, size_t size) {
  if (size < PATCH_HDR_BYTES) return -1;
  uint32_t n_region = get_be32(fw + 44); /* hdr.desc.n_region */
  if (n_region > (size - PATCH_HDR_BYTES) / PATCH_SEC_BYTES) return -1;
  printf("patch build %.16s, %u sections\n", (const char *)fw, n_region);
  for (uint32_t i = 0; i < n_region; i++) {
    const uint8_t *sec = fw + PATCH_HDR_BYTES + i * PATCH_SEC_BYTES;
    uint32_t type = get_be32(sec), offs = get_be32(sec + 4);
    uint32_t addr = get_be32(sec + 12), len = get_be32(sec + 16);
    if ((type & PATCH_SEC_TYPE_MASK) != PATCH_SEC_TYPE_INFO || offs > size ||
        len > size - offs)
      return -1;
    if (init_download(dev, addr, len, patch_data_mode(get_be32(sec + 20))) ||
        send_scatter(dev, fw + offs, len))
      return -1;
  }
  return 0;
}

static int load_patch(mt7902_dev *dev, const char *path) {
  uint8_t op[4];
  put_le32(op, PATCH_SEM_GET);
  int sem = mcu_cmd_status(dev, MCU_CMD_PATCH_SEM_CONTROL, op, sizeof(op));
  if (sem == PATCH_IS_DL) {
    puts("patch already downloaded");
    return 0;
  }
  if (sem != PATCH_NOT_DL_SEM_SUCCESS) {
    log_err("patch semaphore not granted (%d)\n", sem);
    return -1;
  }

  size_t size = 0;
  uint8_t *fw = read_file(path, &size);
  int rc = fw ? send_patch_sections(dev, fw, size) : -1;
  free(fw);
  if (!rc) {
    uint8_t finish[4] = {0}; /* check_crc = 0 */
    rc = mcu_cmd_acked(dev, MCU_CMD_PATCH_FINISH_REQ, finish, sizeof(finish));
  }
  if (rc) log_err("patch download failed\n");

  put_le32(op, PATCH_SEM_RELEASE);
  if (mcu_cmd_status(dev, MCU_CMD_PATCH_SEM_CONTROL, op, sizeof(op)) !=
      PATCH_REL_SEM_SUCCESS) {
    log_err("patch semaphore not released\n");
    return -1;
  }
  return rc;
}

#define FW_TRAILER_BYTES 36
#define FW_REGION_BYTES 40

static int load_ram(mt7902_dev *dev, const char *path) {
  size_t size = 0;
  uint8_t *fw = read_file(path, &size);
  if (!fw) return -1;
  int rc = -1;
  const uint8_t *trailer = fw + size - FW_TRAILER_BYTES;
  uint32_t n_region = size >= FW_TRAILER_BYTES ? trailer[2] : 0;
  if (!n_region || size < FW_TRAILER_BYTES + n_region * FW_REGION_BYTES) {
    log_err("invalid RAM image\n");
    goto out;
  }
  printf("firmware %.10s, build %.15s, %u regions\n", (const char *)trailer + 7,
         (const char *)trailer + 17, n_region);

  uint32_t offset = 0, override_addr = 0;
  for (uint32_t i = 0; i < n_region; i++) {
    const uint8_t *region = trailer - (n_region - i) * FW_REGION_BYTES;
    uint32_t addr = get_le32(region + 16), len = get_le32(region + 20);
    uint8_t feature = region[24];
    if (len > size || offset > size - len) goto out;
    if (!(feature & FW_FEATURE_NON_DL)) {
      uint32_t mode = DL_MODE_NEED_RSP | (feature & FW_FEATURE_SET_KEY_IDX_MASK);
      if (feature & FW_FEATURE_SET_ENCRYPT)
        mode |= DL_MODE_ENCRYPT | DL_MODE_RESET_SEC_IV;
      if (feature & FW_FEATURE_ENCRY_MODE) mode |= DL_CONFIG_ENCRY_MODE_SEL;
      if (feature & FW_FEATURE_OVERRIDE_ADDR) override_addr = addr;
      if (init_download(dev, addr, len, mode) ||
          send_scatter(dev, fw + offset, len)) {
        log_err("RAM region %u download failed\n", i);
        goto out;
      }
    }
    offset += len;
  }

  uint8_t start[8];
  put_le32(start, override_addr ? FW_START_OVERRIDE : 0);
  put_le32(start + 4, override_addr);
  rc = mcu_cmd_acked(dev, MCU_CMD_FW_START_REQ, start, sizeof(start));
out:
  free(fw);
  return rc;
}

int mt7902_load_firmware(mt7902_dev *dev, const char *patch_path,
                         const char *ram_path) {
  /* Must be in place before the download: mt7921 __mt7921_init_hardware. */
  mt7902_wr(dev, MT_SWDEF_MODE, MT_SWDEF_NORMAL_MODE);
  uint8_t restart[4] = {1}; /* power_mode */
  if (mt7902_mcu_cmd(dev, MCU_CMD_NIC_POWER_CTRL, restart, sizeof(restart),
                     NULL, 0))
    return -1;
  if (reg_poll(dev, MT_CONN_ON_MISC, MT_TOP_MISC_FW_STATE,
               MT_TOP_MISC2_FW_PWR_ON, 1000))
    log_err("MCU is not ready for firmware download, state 0x%x\n",
            mt7902_rr(dev, MT_CONN_ON_MISC) & MT_TOP_MISC_FW_STATE);

  if (load_patch(dev, patch_path) || load_ram(dev, ram_path)) return -1;
  if (reg_poll(dev, MT_CONN_ON_MISC, MT_TOP_MISC2_FW_N9_RDY,
               MT_TOP_MISC2_FW_N9_RDY, 1500)) {
    log_err("timeout for initializing firmware, state 0x%x\n",
            mt7902_rr(dev, MT_CONN_ON_MISC));
    return -1;
  }
  return 0;
}
