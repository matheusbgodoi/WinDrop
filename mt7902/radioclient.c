/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
/* Test client for the radio daemon: connects to its pipe, optionally asks for
 * AWDL frames only, counts what arrives and sends a few probe requests.
 *   radioclient <seconds> [awdl] */
#include "../radio/radio_link.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static const uint8_t kAwdlBssid[6] = {0x00, 0x25, 0x00, 0xff, 0x94, 0x73};

static int read_exact(HANDLE pipe, void *data, DWORD len) {
  uint8_t *bytes = data;
  while (len) {
    DWORD got;
    if (!ReadFile(pipe, bytes, len, &got, NULL) || !got) return -1;
    bytes += got;
    len -= got;
  }
  return 0;
}

static int send_message(HANDLE pipe, uint16_t type, const void *payload,
                        uint32_t len) {
  uint8_t message[sizeof(struct radio_link_header) + 256];
  struct radio_link_header header = {type, 0, len};
  DWORD put;
  memcpy(message, &header, sizeof(header));
  memcpy(message + sizeof(header), payload, len);
  return WriteFile(pipe, message, (DWORD)sizeof(header) + len, &put, NULL) ? 0
                                                                           : -1;
}

int main(int argc, char **argv) {
  unsigned seconds = argc > 1 ? (unsigned)atoi(argv[1]) : 10;
  int awdl = argc > 2 && !strcmp(argv[2], "awdl");
  HANDLE pipe = INVALID_HANDLE_VALUE;
  for (int attempt = 0; attempt < 300 && pipe == INVALID_HANDLE_VALUE; attempt++) {
    pipe = CreateFileA(RADIO_LINK_PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0,
                       NULL, OPEN_EXISTING, 0, NULL);
    if (pipe == INVALID_HANDLE_VALUE) Sleep(100);
  }
  if (pipe == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "no radio daemon on %s (error %lu)\n", RADIO_LINK_PIPE_NAME,
            GetLastError());
    return 2;
  }

  struct radio_link_header header;
  struct radio_link_hello hello;
  if (read_exact(pipe, &header, sizeof(header)) ||
      header.type != RADIO_LINK_HELLO || header.length != sizeof(hello) ||
      read_exact(pipe, &hello, sizeof(hello))) {
    fprintf(stderr, "no hello from the radio\n");
    return 1;
  }
  printf("radio protocol %u, channel %u, %u MHz\n", hello.version, hello.channel,
         hello.width_mhz);
  if (awdl) send_message(pipe, RADIO_LINK_SET_BSSID_FILTER, kAwdlBssid, 6);

  /* A radiotap header with no fields, then a wildcard probe request. */
  uint8_t probe[8 + 24 + 12] = {0, 0, 8, 0, 0, 0, 0, 0, 0x40};
  static const uint8_t from[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
  static const uint8_t ies[12] = {0, 0, 1, 8, 0x8c, 0x12, 0x98, 0x24,
                                  0xb0, 0x48, 0x60, 0x6c};
  memset(probe + 8 + 4, 0xff, 6);
  memcpy(probe + 8 + 10, from, 6);
  memset(probe + 8 + 16, 0xff, 6);
  memcpy(probe + 8 + 24, ies, sizeof(ies));

  static uint8_t payload[RADIO_LINK_MAX_PAYLOAD];
  unsigned frames = 0, awdl_actions = 0, beacons = 0, sent = 0, shown = 0;
  int rssi_sum = 0;
  ULONGLONG end = GetTickCount64() + 1000ull * seconds, next_probe = 0;
  while (GetTickCount64() < end) {
    if (!awdl && GetTickCount64() >= next_probe) {
      if (!send_message(pipe, RADIO_LINK_TX, probe, sizeof(probe))) sent++;
      next_probe = GetTickCount64() + 500;
    }
    DWORD waiting = 0;
    if (!PeekNamedPipe(pipe, NULL, 0, NULL, &waiting, NULL)) break;
    if (waiting < sizeof(header)) {
      Sleep(1);
      continue;
    }
    if (read_exact(pipe, &header, sizeof(header)) ||
        header.length > sizeof(payload) ||
        read_exact(pipe, payload, header.length))
      break;
    if (header.type != RADIO_LINK_RX || header.length < 4) continue;
    uint32_t radiotap = payload[2] | (uint32_t)payload[3] << 8;
    if (radiotap + 24 > header.length) continue;
    const uint8_t *f = payload + radiotap;
    uint32_t len = header.length - radiotap;
    frames++;
    rssi_sum += (int8_t)payload[9];
    if (f[0] == 0x80) beacons++;
    /* Vendor-specific action frame from Apple, type 8: AWDL. */
    int is_awdl = f[0] == 0xd0 && len > 28 && f[24] == 127 && f[25] == 0x00 &&
                  f[26] == 0x17 && f[27] == 0xf2 && f[28] == 8;
    awdl_actions += is_awdl;
    if (is_awdl && shown < 4) {
      shown++;
      printf("  AWDL action from %02x:%02x:%02x:%02x:%02x:%02x, BSSID "
             "%02x:%02x:%02x:%02x:%02x:%02x, %u bytes, %d dBm\n",
             f[10], f[11], f[12], f[13], f[14], f[15], f[16], f[17], f[18],
             f[19], f[20], f[21], len, (int8_t)payload[9]);
    }
  }
  printf("%u frames in %u s (%u beacons, %u AWDL actions), mean signal %d dBm; "
         "%u probe requests handed to the radio\n",
         frames, seconds, beacons, awdl_actions, frames ? rssi_sum / (int)frames : 0,
         sent);
  CloseHandle(pipe);
  return frames ? 0 : 1;
}
