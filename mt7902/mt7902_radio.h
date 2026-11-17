/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* The MT7902 as a raw 802.11 radio once its firmware runs: capability query,
 * MAC start, channel and sniffer mode, and the receive descriptor. */
#pragma once
#include "mt7902_hw.h"

/* Regulatory domain handed to the firmware. */
#define MT7902_COUNTRY "BR"

typedef struct mt7902_caps {
  uint8_t mac[6];
  uint8_t nss;
  uint8_t has_2ghz, has_5ghz;
} mt7902_caps;

/* A received 802.11 frame, pointing into the ring buffer it arrived in. */
typedef struct mt7902_frame {
  const uint8_t *data; /* starts at the 802.11 frame control field */
  uint32_t len;
  uint8_t channel;
  uint8_t fcs_error;
  int8_t rssi; /* dBm, 0 when the chip did not report it */
} mt7902_frame;

/* How wide the radio listens on 5 GHz: 20, or 80 for the block the channel
 * is in. Set before mt7902_monitor_start. Frames still go out 20 MHz wide. */
extern uint8_t mt7902_width_mhz;

/* What the firmware does after boot before the radio is usable. */
int mt7902_radio_init(mt7902_dev *dev, mt7902_caps *caps);
/* Turns sniffer mode on, tuned to a 20 MHz channel. */
int mt7902_monitor_start(mt7902_dev *dev, const mt7902_caps *caps,
                         uint8_t channel);
/* Retunes while sniffing. */
int mt7902_set_channel(mt7902_dev *dev, const mt7902_caps *caps,
                       uint8_t channel);
/* Changes the address the card answers to: it acknowledges frames sent to it
 * and takes acknowledgements for frames sent from it. */
int mt7902_set_address(mt7902_dev *dev, const uint8_t *mac);

/* Legacy rates for mt7902_send_frame, in the chip's encoding: PHY mode in
 * bits 9:6 (0 is CCK, 1 is OFDM), rate index below. */
#define MT7902_RATE_CCK_1M 0x000
#define MT7902_RATE_OFDM_6M 0x04b
#define MT7902_RATE_OFDM_12M 0x04a
#define MT7902_RATE_OFDM_24M 0x049
#define MT7902_RATE_OFDM_54M 0x04c
/* One spatial stream: HT is mode 2, VHT mode 4, HE mode 8, MCS below. */
#define MT7902_RATE_HT_MCS(mcs) (0x080 | (mcs))
#define MT7902_RATE_VHT_MCS(mcs) (0x100 | (mcs))
#define MT7902_RATE_HE_MCS(mcs) (0x200 | (mcs))

/* Spatial extension index of transmitted frames: 24 duplicates the stream on
 * both antennas, 0 sends on the first one alone. */
extern uint32_t mt7902_tx_spe;

/* Queues an 802.11 frame, header included and FCS excluded, at a fixed rate.
 * `no_ack` is for group-addressed frames and whatever needs no retry. */
int mt7902_send_frame(mt7902_dev *dev, const uint8_t *frame, size_t len,
                      uint16_t rate, int no_ack);

/* Packet type of a buffer from the data or event ring (PKT_TYPE_*). */
uint8_t mt7902_rx_type(const mt7902_rx *rx);
/* Locates the 802.11 frame in a PKT_TYPE_NORMAL buffer. Returns 0 on success,
 * -1 when the buffer holds something else or is malformed. */
int mt7902_rx_frame(const mt7902_rx *rx, mt7902_frame *frame);
