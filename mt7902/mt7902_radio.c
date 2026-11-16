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
  return cmd_acked(dev, MCU_EXT_CMD(MCU_EXT_CMD_MAC_INIT_CTRL), mac_on,
                   sizeof(mac_on), "MAC enable");
}

/* mt7921_mcu_set_chan_info, 20 MHz only. */
static int set_chan_info(mt7902_dev *dev, const mt7902_caps *caps,
                         uint32_t ext_cmd, uint8_t channel) {
  uint8_t req[76] = {0};
  req[0] = channel; /* control_ch */
  req[1] = channel; /* center_ch */
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
  req[14] = channel;              /* center_ch */
  req[16] = 1;                    /* drop_err */
  return cmd_acked(dev, MCU_UNI_CMD(MCU_UNI_CMD_SNIFFER), req, sizeof(req),
                   "sniffer config");
}

int mt7902_set_channel(mt7902_dev *dev, const mt7902_caps *caps,
                       uint8_t channel) {
  if (sniffer_config(dev, channel)) return -1;
  return set_chan_info(dev, caps, MCU_EXT_CMD_CHANNEL_SWITCH, channel);
}

int mt7902_monitor_start(mt7902_dev *dev, const mt7902_caps *caps,
                         uint8_t channel) {
  if (set_chan_info(dev, caps, MCU_EXT_CMD_SET_RX_PATH, channel)) return -1;

  /* mt76_connac_mcu_uni_add_dev: the interface, then its BSS. */
  uint8_t dev_req[16] = {0};
  put_le16(dev_req + 6, 12); /* tag 0 is DEV_INFO_ACTIVE; len */
  dev_req[8] = 1;            /* active */
  memcpy(dev_req + 10, caps->mac, 6);
  if (cmd_acked(dev, MCU_UNI_CMD(MCU_UNI_CMD_DEV_INFO_UPDATE), dev_req,
                sizeof(dev_req), "device info"))
    return -1;

  uint8_t bss_req[36] = {0};
  uint8_t *basic = bss_req + 4; /* struct mt76_connac_bss_basic_tlv, tag 0 */
  put_le16(basic + 2, 32);
  basic[4] = 1; /* active */
  put_le32(basic + 8, CONNECTION_INFRA_AP);
  basic[12] = 1; /* conn_state */
  put_le16(basic + 20, MONITOR_WLAN_IDX); /* bmc_tx_wlan_idx */
  put_le16(basic + 26, MONITOR_WLAN_IDX); /* sta_idx */
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
  if (rxd1 & MT_RXD1_NORMAL_GROUP_3) {
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
