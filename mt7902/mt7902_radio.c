/* SPDX-License-Identifier: BSD-3-Clause-Clear */
#include "mt7902_radio.h"
#include "mt7902_regs.h"
#include <stdio.h>
#include <string.h>

#define MCU_RXD_BYTES 36
#define MT_NIC_CAP_MAC_ADDR 7
#define MT_NIC_CAP_PHY 8
#define MONITOR_WLAN_IDX 19 /* MT792x_WTBL_RESERVED, for interface 0 */
#define CONNECTION_INFRA_AP 0x10002u

#define log_err(...) printf("mt7902: " __VA_ARGS__)

static uint32_t get_le32(const uint8_t *p) {
  uint32_t v;
  memcpy(&v, p, 4);
  return v;
}

static void put_le16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }
static void put_le32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }

static int cmd_acked(mt7902_dev *dev, uint32_t cmd, const void *req,
                     size_t len, const char *what) {
  uint8_t rsp[128];
  if (mt7902_mcu_cmd(dev, cmd, req, len, rsp, sizeof(rsp)) < 0) {
    log_err("%s failed\n", what);
    return -1;
  }
  return 0;
}

/* A register read through the firmware, answered in order: mt76 sends one
 * after each command that has no answer of its own, "to avoid PSE buffer
 * underflow". Without it a run of such commands makes the firmware ask for a
 * DMA stop. */
static int mcu_sync(mt7902_dev *dev) {
  uint8_t req[8] = {0};
  put_le32(req, MT_PSE_BASE);
  return cmd_acked(dev, MCU_CE_CMD(MCU_CE_CMD_REG_READ) | MCU_CMD_FIELD_QUERY,
                   req, sizeof(req), "MCU sync");
}

/* mt7921_mcu_get_nic_capability: a count, then type/length/value elements. */
static int query_caps(mt7902_dev *dev, mt7902_caps *caps) {
  uint8_t rsp[2048];
  int n = mt7902_mcu_cmd(dev, MCU_CE_CMD(MCU_CE_CMD_GET_NIC_CAPAB) |
                                  MCU_CMD_FIELD_QUERY,
                         NULL, 0, rsp, sizeof(rsp));
  if (n < MCU_RXD_BYTES + 4) {
    log_err("capability query failed\n");
    return -1;
  }
  const uint8_t *p = rsp + MCU_RXD_BYTES, *end = rsp + n;
  unsigned count = p[0] | p[1] << 8;
  p += 4;
  for (unsigned i = 0; i < count && p + 8 <= end; i++) {
    uint32_t type = get_le32(p), len = get_le32(p + 4);
    p += 8;
    if (len > (size_t)(end - p)) break;
    if (type == MT_NIC_CAP_MAC_ADDR && len >= 6) memcpy(caps->mac, p, 6);
    if (type == MT_NIC_CAP_PHY && len >= 11) {
      caps->nss = p[4];
      caps->has_2ghz = p[10] & 1;
      caps->has_5ghz = p[10] >> 1 & 1;
    }
    p += len;
  }
  if (!caps->nss || caps->nss > 4) {
    log_err("capability answer carried no usable PHY element\n");
    return -1;
  }
  return 0;
}

/* mt76_connac_mcu_set_channel_domain. Without it the firmware receives on
 * 5 GHz but puts nothing on air there. Only channels that need no radar
 * detection are listed, with no restriction flags. */
static int set_channel_domain(mt7902_dev *dev) {
  static const uint8_t ch_2g[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
  static const uint8_t ch_5g[] = {36, 40, 44, 48, 149, 153, 157, 161, 165};
  uint8_t req[12 + 8 * (sizeof(ch_2g) + sizeof(ch_5g))] = {0};
  memcpy(req, MT7902_COUNTRY, 2); /* alpha2 */
  req[5] = 3;                     /* bw_5g: 20, 40, 80 and 160 MHz */
  req[6] = 3;                     /* bw_6g */
  req[8] = sizeof(ch_2g);
  req[9] = sizeof(ch_5g);
  uint8_t *p = req + 12;
  for (size_t i = 0; i < sizeof(ch_2g); i++, p += 8) p[0] = ch_2g[i];
  for (size_t i = 0; i < sizeof(ch_5g); i++, p += 8) p[0] = ch_5g[i];
  if (mt7902_mcu_cmd(dev, MCU_CE_CMD(MCU_CE_CMD_SET_CHAN_DOMAIN), req,
                     sizeof(req), NULL, 0)) {
    log_err("channel domain failed\n");
    return -1;
  }
  return mcu_sync(dev);
}

/* mt76_connac_mcu_rate_txpower_band: a power limit for every rate on every
 * channel, eight channels to a message. `band` is 1 for 2.4 GHz, 2 for 5 GHz.
 * mt76 derives the limits from the regulatory domain; this uses one value. */
#define SKU_POWER_LIMITS 161
#define TX_POWER_HALF_DBM (2 * 17)

static int set_rate_txpower(mt7902_dev *dev, uint8_t band) {
  static const uint8_t ch_2g[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
  static const uint8_t ch_5g[] = {
      36,  38,  40,  42,  44,  46,  48,  50,  52,  54,  56,  58,
      60,  62,  64,  100, 102, 104, 106, 108, 110, 112, 114, 116,
      118, 120, 122, 124, 126, 128, 132, 134, 136, 138, 140, 142,
      144, 149, 151, 153, 155, 157, 159, 161, 165, 169, 173, 177};
  const uint8_t *list = band == 1 ? ch_2g : ch_5g;
  size_t total = band == 1 ? sizeof(ch_2g) : sizeof(ch_5g);
  for (size_t at = 0; at < total; at += 8) {
    size_t n = total - at < 8 ? total - at : 8;
    uint8_t req[44 + 8 * (1 + SKU_POWER_LIMITS)] = {0};
    req[4] = (uint8_t)n; /* n_chan */
    req[5] = band;
    req[6] = list[at + n - 1] == 177; /* last_msg */
    memcpy(req + 8, MT7902_COUNTRY, 2);
    uint8_t *sku = req + 44;
    for (size_t i = 0; i < n; i++, sku += 1 + SKU_POWER_LIMITS) {
      sku[0] = list[at + i];
      memset(sku + 1, TX_POWER_HALF_DBM, SKU_POWER_LIMITS);
    }
    if (mt7902_mcu_cmd(dev, MCU_CE_CMD(MCU_CE_CMD_SET_RATE_TX_POWER), req,
                       44 + n * (1 + SKU_POWER_LIMITS), NULL, 0) ||
        mcu_sync(dev))
      return -1;
  }
  return 0;
}

int mt7902_radio_init(mt7902_dev *dev, mt7902_caps *caps) {
  memset(caps, 0, sizeof(*caps));
  if (query_caps(dev, caps)) return -1;

  uint8_t efuse[4] = {0 /* EE_MODE_EFUSE */, 1 /* EE_FORMAT_WHOLE */};
  if (cmd_acked(dev, MCU_EXT_CMD(MCU_EXT_CMD_EFUSE_BUFFER_MODE), efuse,
                sizeof(efuse), "EFUSE mode"))
    return -1;

  /* A raw radio wants the 802.11 header as it was on air. */
  mt7902_wr(dev, MT_MDP_DCR0,
            mt7902_rr(dev, MT_MDP_DCR0) & ~MT_MDP_DCR0_RX_HDR_TRANS_EN);

  uint8_t mac_on[4] = {1 /* enable */, 0 /* band */};
  if (cmd_acked(dev, MCU_EXT_CMD(MCU_EXT_CMD_MAC_INIT_CTRL), mac_on,
                sizeof(mac_on), "MAC enable"))
    return -1;
  if (set_channel_domain(dev)) return -1;

  /* mt792x_mac_init: transmit duration limit, then RTS only for frames past
   * 0x92b bytes (mt76_connac_mcu_set_rts_thresh). */
  uint32_t ctcr0 = mt7902_rr(dev, MT_TMAC_CTCR0);
  mt7902_wr(dev, MT_TMAC_CTCR0, (ctcr0 & ~0x3fu) | 0x3f | BIT(17) | BIT(18));
  uint8_t rts[12] = {1 /* prot_idx */, 0 /* band */};
  put_le32(rts + 4, 0x92b); /* len_thresh */
  put_le32(rts + 8, 2);     /* pkt_thresh */
  if (cmd_acked(dev, MCU_EXT_CMD(MCU_EXT_CMD_PROTECT_CTRL), rts, sizeof(rts),
                "RTS threshold"))
    return -1;
  return set_rate_txpower(dev, 1) || set_rate_txpower(dev, 2) ? -1 : 0;
}

uint8_t mt7902_width_mhz = 20;

/* The centre of the 80 MHz block a 5 GHz channel belongs to, or the channel
 * itself while the radio stays 20 MHz wide. */
static uint8_t center_of(uint8_t channel) {
  if (mt7902_width_mhz != 80 || channel < 36) return channel;
  uint8_t first = channel >= 149 ? 149 : channel >= 100 ? 100 : 36;
  return (uint8_t)(first + (channel - first) / 16 * 16 + 6);
}

/* mt7921_mcu_set_chan_info, 20 or 80 MHz wide. */
static int set_chan_info(mt7902_dev *dev, const mt7902_caps *caps,
                         uint32_t ext_cmd, uint8_t channel) {
  uint8_t req[76] = {0};
  req[0] = channel; /* control_ch */
  req[1] = center_of(channel); /* center_ch */
  req[2] = req[1] != channel ? 2 : 0; /* bw: CMD_CBW_80MHZ or 20 */
  req[3] = caps->nss; /* tx_streams_num */
  /* rx_streams: a mask for SET_RX_PATH, a count for CHANNEL_SWITCH */
  req[4] = ext_cmd == MCU_EXT_CMD_SET_RX_PATH ? (uint8_t)((1u << caps->nss) - 1)
                                              : caps->nss;
  req[10] = channel > 14; /* channel_band: 0 is 2.4 GHz, 1 is 5 GHz */
  return cmd_acked(dev, MCU_EXT_CMD(ext_cmd), req, sizeof(req), "channel set");
}

static int sniffer_config(mt7902_dev *dev, uint8_t channel) {
  uint8_t req[20] = {0};
  put_le16(req + 4, 1);  /* tag: config */
  put_le16(req + 6, 16); /* len */
  req[10] = channel > 14 ? 2 : 1; /* ch_band */
  req[12] = channel;              /* control_ch */
  req[14] = center_of(channel);   /* center_ch */
  if (req[14] != channel) {
    req[11] = 1;                        /* bw: 80 MHz */
    req[13] = channel < req[14] ? 1 : 3; /* sco: control above or below */
  }
  req[16] = 1;                    /* drop_err */
  return cmd_acked(dev, MCU_UNI_CMD(MCU_UNI_CMD_SNIFFER), req, sizeof(req),
                   "sniffer config");
}

int mt7902_set_channel(mt7902_dev *dev, const mt7902_caps *caps,
                       uint8_t channel) {
  if (sniffer_config(dev, channel)) return -1;
  return set_chan_info(dev, caps, MCU_EXT_CMD_CHANNEL_SWITCH, channel);
}

int mt7902_set_address(mt7902_dev *dev, const uint8_t *mac) {
  uint8_t req[16] = {0};
  put_le16(req + 6, 12); /* tag 0 is DEV_INFO_ACTIVE; len */
  req[8] = 1;            /* active */
  memcpy(req + 10, mac, 6);
  return cmd_acked(dev, MCU_UNI_CMD(MCU_UNI_CMD_DEV_INFO_UPDATE), req,
                   sizeof(req), "device info");
}

int mt7902_monitor_start(mt7902_dev *dev, const mt7902_caps *caps,
                         uint8_t channel) {
  if (set_chan_info(dev, caps, MCU_EXT_CMD_SET_RX_PATH, channel)) return -1;

  /* mt76_connac_mcu_uni_add_dev: the interface, then its BSS. */
  if (mt7902_set_address(dev, caps->mac)) return -1;

  uint8_t bss_req[36] = {0};
  uint8_t *basic = bss_req + 4; /* struct mt76_connac_bss_basic_tlv, tag 0 */
  put_le16(basic + 2, 32);
  basic[4] = 1; /* active */
  put_le32(basic + 8, CONNECTION_INFRA_AP);
  basic[12] = 1; /* conn_state */
  put_le16(basic + 20, MONITOR_WLAN_IDX); /* bmc_tx_wlan_idx */
  put_le16(basic + 26, MONITOR_WLAN_IDX); /* sta_idx */
  /* phymode: B, G and GN on 2.4 GHz; A, AN and AC on 5 GHz. Then the basic
   * non-HT PHY: ERP or OFDM. */
  basic[25] = channel > 14 ? 0x31 : 0x0e;
  put_le16(basic + 28, channel > 14 ? 3 : 1);
  if (cmd_acked(dev, MCU_UNI_CMD(MCU_UNI_CMD_BSS_INFO_UPDATE), bss_req,
                sizeof(bss_req), "BSS info"))
    return -1;

  uint8_t enable[12] = {0};
  put_le16(enable + 6, 8); /* tag 0 is enable; len */
  enable[8] = 1;
  if (cmd_acked(dev, MCU_UNI_CMD(MCU_UNI_CMD_SNIFFER), enable, sizeof(enable),
                "sniffer enable"))
    return -1;

  if (mt7902_set_channel(dev, caps, channel)) return -1;
  mt7902_wr(dev, MT_WF_RFCR, 0); /* drop nothing */
  return 0;
}

/* mt76_connac2_mac_write_txwi for a fixed-rate 802.11 frame on interface 0. */
uint32_t mt7902_tx_spe = 24;

int mt7902_send_frame(mt7902_dev *dev, const uint8_t *frame, size_t len,
                      uint16_t rate, int no_ack) {
  if (len < 24) return -1;
  uint8_t type = frame[0] >> 2 & 3, subtype = frame[0] >> 4;
  int multicast = frame[4] & 1;
  uint32_t hdr_len = type == 2 && (subtype & 8) ? 26 : 24; /* QoS data */
  uint32_t w[8] = {0};
  w[0] = (uint32_t)(len + 32) | 0u << 23 /* PKT_FMT cut-through */ |
         0x10u << 25 /* Q_IDX ALTX0 */;
  w[1] = BIT(31) /* long format */ | MONITOR_WLAN_IDX |
         2u << 16 /* HDR_FORMAT 802.11 */ | (hdr_len / 2) << 11;
  w[2] = BIT(31) /* FIX_RATE */ | BIT(13) /* HTC_VLD */ | type << 4 | subtype |
         (multicast ? BIT(10) : 0);
  w[3] = 15u << 11 /* REM_TX_COUNT */ | BIT(28) /* BA_DISABLE */ |
         (no_ack ? BIT(0) : 0);
  w[6] = BIT(2) /* FIXED_BW, 20 MHz */ | (uint32_t)rate << 16;
  /* Frame type again, where the PCIe TXD carries it, and the spatial
   * extension index mt76 gives fixed-rate frames on a two-antenna card. */
  w[7] = (uint32_t)type << 20 | (uint32_t)subtype << 16 | mt7902_tx_spe << 11;
  uint8_t txd[32];
  memcpy(txd, w, sizeof(txd));
  return mt7902_tx_frame(dev, txd, frame, len, NULL);
}

uint8_t mt7902_rx_type(const mt7902_rx *rx) {
  return rx->len < 4 ? 0xff
                     : (uint8_t)(get_le32(rx->data) >> MT_RXD0_PKT_TYPE_SHIFT);
}

/* The header and group layout mt7921_mac_fill_rx walks. */
int mt7902_rx_frame(const mt7902_rx *rx, mt7902_frame *frame) {
  if (rx->len < 24 || mt7902_rx_type(rx) != PKT_TYPE_NORMAL) return -1;
  uint32_t rxd0 = get_le32(rx->data), rxd1 = get_le32(rx->data + 4);
  uint32_t rxd2 = get_le32(rx->data + 8), rxd3 = get_le32(rx->data + 12);
  uint32_t total = rxd0 & MT_RXD0_LENGTH_MASK;
  if (total > rx->len || rxd2 & MT_RXD2_NORMAL_HDR_TRANS) return -1;

  uint32_t offset = 24;
  if (rxd1 & MT_RXD1_NORMAL_GROUP_4) offset += 16;
  if (rxd1 & MT_RXD1_NORMAL_GROUP_1) offset += 16;
  if (rxd1 & MT_RXD1_NORMAL_GROUP_2) offset += 8;
  frame->rssi = 0;
  if (rxd1 & MT_RXD1_NORMAL_GROUP_3) {
    /* P-RXV word 1 starts with the first antenna's RCPI, in half dB above
     * -110 dBm. */
    if (offset + 8 <= total) {
      int rcpi = rx->data[offset + 4];
      frame->rssi = (int8_t)((rcpi - 220) / 2);
    }
    offset += 8;
    if (rxd1 & MT_RXD1_NORMAL_GROUP_5) offset += 72;
  }
  offset += 2 * (rxd2 >> MT_RXD2_NORMAL_HDR_OFFSET_SHIFT & 3);
  if (offset + 10 > total) return -1;

  frame->data = rx->data + offset;
  frame->len = total - offset;
  frame->channel = (uint8_t)(rxd3 >> MT_RXD3_NORMAL_CH_FREQ_SHIFT);
  frame->fcs_error = (rxd1 & MT_RXD1_NORMAL_FCS_ERR) != 0;
  return 0;
}
