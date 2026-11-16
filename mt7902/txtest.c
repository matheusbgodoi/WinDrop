/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* Phase 4 test: sends broadcast probe requests from the MT7902 and counts
 * the probe responses that access points address back to it. An answer is
 * proof, from a third party, that the frames went on air.
 *   txtest <patch.bin> <ram.bin> <channel> <probes> <cck|ofdm> [bssid]
 * With a BSSID the probes are addressed to that access point and need its
 * acknowledgement, which the firmware reports back per frame: that tells
 * whether a frame was heard even when nothing answers it. */
#include "mt7902_radio.h"
#include "mt7902_regs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static size_t build_probe_request(uint8_t *f, const uint8_t *mac,
                                  const uint8_t *target, uint16_t seq) {
  static const uint8_t ies[] = {0x00, 0x00, /* SSID: wildcard */
                                0x01, 0x08, 0x82, 0x84, 0x8b, 0x96,
                                0x0c, 0x12, 0x18, 0x24}; /* supported rates */
  memset(f, 0, 24);
  f[0] = 0x40; /* management, probe request */
  memcpy(f + 4, target, 6);
  memcpy(f + 10, mac, 6);
  memcpy(f + 16, target, 6);
  f[22] = (uint8_t)(seq << 4);
  f[23] = (uint8_t)(seq >> 4);
  memcpy(f + 24, ies, sizeof(ies));
  return 24 + sizeof(ies);
}

int main(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: txtest <patch.bin> <ram.bin> <channel> [probes]\n");
    return 2;
  }
  uint8_t channel = (uint8_t)atoi(argv[3]);
  unsigned probes = argc > 4 ? (unsigned)atoi(argv[4]) : 30;
  int ofdm = argc > 5 && !strcmp(argv[5], "ofdm");
  uint8_t target[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  int unicast = 0;
  if (argc > 6) {
    unsigned b[6];
    if (sscanf_s(argv[6], "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4],
               &b[5]) != 6)
      return 2;
    for (int i = 0; i < 6; i++) target[i] = (uint8_t)b[i];
    unicast = 1;
  }

  mt7902_dev dev;
  if (mt7902_start(&dev, argv[1], argv[2])) return 2;
  int rc = 1;
  mt7902_caps caps;
  if (mt7902_radio_init(&dev, &caps) ||
      mt7902_monitor_start(&dev, &caps, channel))
    goto out;
  uint16_t rate = ofdm ? MT7902_RATE_OFDM_6M : MT7902_RATE_CCK_1M;
  printf("probing on channel %u as %02x:%02x:%02x:%02x:%02x:%02x\n", channel,
         caps.mac[0], caps.mac[1], caps.mac[2], caps.mac[3], caps.mac[4],
         caps.mac[5]);

  unsigned sent = 0, refused = 0, frames = 0, to_us = 0, responses = 0;
  unsigned notifies = 0, freed = 0, shown = 0, acked = 0, lost = 0, tx_count = 0;
  ULONGLONG start = GetTickCount64(), next_probe = start;
  ULONGLONG end = start + probes * 100ull + 1500;
  mt7902_ring *rings[] = {&dev.rx_data, &dev.rx_event};
  while (GetTickCount64() < end) {
    if (sent + refused < probes && GetTickCount64() >= next_probe) {
      uint8_t frame[64];
      size_t len = build_probe_request(frame, caps.mac, target, (uint16_t)sent);
      if (mt7902_send_frame(&dev, frame, len, rate, !unicast))
        refused++;
      else
        sent++;
      next_probe += 100;
    }
    int busy = 0;
    for (int r = 0; r < 2; r++) {
      mt7902_rx rx;
      if (!mt7902_rx_poll(&dev, rings[r], &rx)) continue;
      busy = 1;
      if (mt7902_rx_type(&rx) == PKT_TYPE_TXRX_NOTIFY && rx.len >= 8) {
        /* mt7921_mac_tx_free: a count, then one word per frame, with a
         * word naming the station ahead of each group. */
        notifies++;
        unsigned count = (rx.data[2] | rx.data[3] << 8) & 0x3ff;
        for (unsigned i = 0; i < count && 12 + 4 * i <= rx.len; i++) {
          uint32_t info;
          memcpy(&info, rx.data + 8 + 4 * i, 4);
          if (info & 0x80000000u) { /* MT_TX_FREE_PAIR */
            count++;
            continue;
          }
          freed++;
          tx_count += info & 0x1fff;         /* MT_TX_FREE_COUNT */
          if (info >> 13 & 3) lost++; else acked++; /* MT_TX_FREE_STATUS */
        }
        continue;
      }
      mt7902_frame f;
      if (mt7902_rx_frame(&rx, &f)) continue;
      frames++;
      if (f.len < 16 || memcmp(f.data + 4, caps.mac, 6)) continue;
      to_us++;
      if (f.data[0] == 0x50) responses++;
      if (shown < 5) {
        shown++;
        printf("  to us: fc %02x%02x len %u from %02x:%02x:%02x:%02x:%02x:%02x\n",
               f.data[0], f.data[1], f.len, f.data[10], f.data[11], f.data[12],
               f.data[13], f.data[14], f.data[15]);
      }
    }
    if (!busy) SwitchToThread();
  }
  printf("%u probes queued, %u refused; %u TX-free notifications covering %u "
         "frames: %u reported delivered, %u failed, %u transmissions in all\n",
         sent, refused, notifies, freed, acked, lost, tx_count);
  printf("%u frames received, %u addressed to us, %u of them probe responses\n",
         frames, to_us, responses);
  rc = responses ? 0 : 1;
out:
  mt7902_close(&dev);
  return rc;
}
