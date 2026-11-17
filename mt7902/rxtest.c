/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
/* Phase 3 test: boots the MT7902, listens on one channel in sniffer mode and
 * reports what arrived.   rxtest <patch.bin> <ram.bin> <channel> [seconds] */
#include "mt7902_radio.h"
#include "mt7902_regs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/* AWDL action frames: vendor-specific action, Apple's OUI, type 8. */
static int is_awdl_action(const mt7902_frame *f) {
  static const uint8_t tail[] = {0x7f, 0x00, 0x17, 0xf2, 0x08};
  return f->len >= 24 + sizeof(tail) && f->data[0] == 0xd0 &&
         !memcmp(f->data + 24, tail, sizeof(tail));
}

int main(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: rxtest <patch.bin> <ram.bin> <channel> [seconds]\n");
    return 2;
  }
  uint8_t channel = (uint8_t)atoi(argv[3]);
  unsigned seconds = argc > 4 ? (unsigned)atoi(argv[4]) : 5;
  mt7902_trace = argc > 5;

  mt7902_dev dev;
  if (mt7902_start(&dev, argv[1], argv[2])) return 2;
  int rc = 1;
  mt7902_caps caps;
  if (mt7902_radio_init(&dev, &caps)) goto out;
  printf("MAC %02x:%02x:%02x:%02x:%02x:%02x, %u streams, 2.4 GHz %u, 5 GHz %u\n",
         caps.mac[0], caps.mac[1], caps.mac[2], caps.mac[3], caps.mac[4],
         caps.mac[5], caps.nss, caps.has_2ghz, caps.has_5ghz);
  if (mt7902_monitor_start(&dev, &caps, channel)) goto out;
  printf("listening on channel %u for %u s\n", channel, seconds);

  unsigned frames = 0, by_type[4] = {0}, fcs_errors = 0, awdl = 0, other = 0;
  unsigned events = 0, shown = 0, type_seen[32] = {0};
  ULONGLONG start = GetTickCount64(), first_frame_ms = 0;
  mt7902_rx rx;
  while (GetTickCount64() - start < seconds * 1000ull) {
    int busy = 0;
    if (mt7902_rx_poll(&dev, &dev.rx_data, &rx)) {
      busy = 1;
      mt7902_frame f;
      type_seen[mt7902_rx_type(&rx) & 31]++;
      if (mt7902_rx_frame(&rx, &f)) {
        other++;
      } else {
        if (!frames++) first_frame_ms = GetTickCount64() - start;
        by_type[f.data[0] >> 2 & 3]++;
        fcs_errors += f.fcs_error;
        if (is_awdl_action(&f)) awdl++;
        if (shown < 8 && f.len >= 16) {
          shown++;
          printf("  fc %02x%02x len %4u ch %3u from %02x:%02x:%02x:%02x:%02x:%02x\n",
                 f.data[0], f.data[1], f.len, f.channel, f.data[10], f.data[11],
                 f.data[12], f.data[13], f.data[14], f.data[15]);
        }
      }
    }
    if (mt7902_rx_poll(&dev, &dev.rx_event, &rx)) {
      busy = 1;
      events++;
    }
    if (!busy) SwitchToThread();
  }
  printf("%u frames (first after %llu ms): %u management, %u control, %u data; "
         "%u FCS errors; %u AWDL action frames\n",
         frames, first_frame_ms, by_type[0], by_type[1], by_type[2], fcs_errors,
         awdl);
  printf("%u other buffers on the data ring, %u on the event ring; packet "
         "types seen:",
         other, events);
  for (int t = 0; t < 32; t++)
    if (type_seen[t]) printf(" %d:%u", t, type_seen[t]);
  printf("\n");
  rc = frames ? 0 : 1;
out:
  mt7902_close(&dev);
  return rc;
}
