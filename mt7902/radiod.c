/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* The MT7902 as winawdl's radio daemon: serves radio/radio_link.h on the named
 * pipe, so the AWDL daemon written for the Realtek radio runs on this card.
 *   radiod <patch.bin> <ram.bin> <channel> [seconds]
 * One thread polls the card's rings and the pipe in turn. One client at a
 * time, administrators and LocalSystem only: whoever holds the pipe puts raw
 * frames on air. With `seconds` the daemon ends by itself, for test runs. */
#include "../radio/radio_link.h"
#include "mt7902_radio.h"
#include "mt7902_regs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
/* after windows.h, which it needs */
#include <sddl.h>

#define PIPE_BUFFER (1u << 16)
#define RADIOTAP_RX_BYTES 10 /* FLAGS and DBM_ANTSIGNAL, as the Realtek radio */
#define ADDR1 4
#define ADDR3 16

typedef struct pipe_io {
  HANDLE handle;
  OVERLAPPED in, out;
  int connected, reading;
  uint8_t chunk[PIPE_BUFFER];
  uint8_t pending[sizeof(struct radio_link_header) + RADIO_LINK_MAX_PAYLOAD +
                  PIPE_BUFFER];
  size_t filled;
} pipe_io;

typedef struct radio {
  mt7902_dev dev;
  mt7902_caps caps;
  uint8_t channel;
  uint8_t bssid[6];
  int filter;
  uint64_t rx_sent, rx_dropped, tx_ok, tx_refused, clients;
} radio;

static volatile LONG g_stop;

static BOOL WINAPI on_console(DWORD event) {
  (void)event;
  InterlockedExchange(&g_stop, 1);
  return TRUE;
}

static int pipe_open(pipe_io *p) {
  SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, FALSE};
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(
          "D:(A;;GA;;;SY)(A;;GA;;;BA)", SDDL_REVISION_1,
          &sa.lpSecurityDescriptor, NULL))
    return -1;
  p->handle = CreateNamedPipeA(
      RADIO_LINK_PIPE_NAME,
      PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_REJECT_REMOTE_CLIENTS, 1,
      PIPE_BUFFER, PIPE_BUFFER, 0, &sa);
  LocalFree(sa.lpSecurityDescriptor);
  p->in.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
  p->out.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
  return p->handle == INVALID_HANDLE_VALUE || !p->in.hEvent || !p->out.hEvent
             ? -1
             : 0;
}

static void pipe_drop(pipe_io *p) {
  if (p->reading) {
    DWORD ignored;
    CancelIoEx(p->handle, &p->in);
    GetOverlappedResult(p->handle, &p->in, &ignored, TRUE);
  }
  DisconnectNamedPipe(p->handle);
  p->connected = p->reading = 0;
  p->filled = 0;
}

/* Blocks until the client took the bytes: it reads far faster than frames
 * arrive, and a client that stopped reading is dropped by its next write. */
static int pipe_send(pipe_io *p, uint16_t type, const uint8_t *a, size_t a_len,
                     const uint8_t *b, size_t b_len) {
  uint8_t message[sizeof(struct radio_link_header) + RADIO_LINK_MAX_PAYLOAD];
  struct radio_link_header header = {type, 0, (uint32_t)(a_len + b_len)};
  if (!p->connected || a_len + b_len > RADIO_LINK_MAX_PAYLOAD) return -1;
  memcpy(message, &header, sizeof(header));
  memcpy(message + sizeof(header), a, a_len);
  if (b_len) memcpy(message + sizeof(header) + a_len, b, b_len);
  DWORD total = (DWORD)(sizeof(header) + a_len + b_len), done = 0, put;
  while (done < total) {
    ResetEvent(p->out.hEvent);
    if (!WriteFile(p->handle, message + done, total - done, &put, &p->out) &&
        (GetLastError() != ERROR_IO_PENDING ||
         !GetOverlappedResult(p->handle, &p->out, &put, TRUE)))
      return -1;
    done += put;
  }
  return 0;
}

/* Starts a connect or a read if none is under way, and collects one that
 * finished. Returns -1 when the client left. */
static int pipe_pump(pipe_io *p) {
  DWORD got = 0;
  if (!p->connected) {
    if (!p->reading) {
      ResetEvent(p->in.hEvent);
      if (ConnectNamedPipe(p->handle, &p->in) ||
          GetLastError() == ERROR_PIPE_CONNECTED) {
        p->connected = 1;
        return 1;
      }
      if (GetLastError() != ERROR_IO_PENDING) return 0;
      p->reading = 1;
    }
    if (WaitForSingleObject(p->in.hEvent, 0) != WAIT_OBJECT_0) return 0;
    p->reading = 0;
    if (!GetOverlappedResult(p->handle, &p->in, &got, FALSE)) return 0;
    p->connected = 1;
    return 1; /* a client arrived */
  }
  if (!p->reading) {
    ResetEvent(p->in.hEvent);
    if (ReadFile(p->handle, p->chunk, sizeof(p->chunk), &got, &p->in)) {
      SetEvent(p->in.hEvent);
    } else if (GetLastError() != ERROR_IO_PENDING) {
      return -1;
    }
    p->reading = 1;
  }
  if (WaitForSingleObject(p->in.hEvent, 0) != WAIT_OBJECT_0) return 0;
  p->reading = 0;
  if (!GetOverlappedResult(p->handle, &p->in, &got, FALSE) || !got) return -1;
  if (p->filled + got > sizeof(p->pending)) return -1;
  memcpy(p->pending + p->filled, p->chunk, got);
  p->filled += got;
  return 0;
}

static void transmit(radio *r, const uint8_t *payload, size_t len) {
  if (len < 4) return;
  size_t radiotap = payload[2] | (size_t)payload[3] << 8;
  if (radiotap + 24 > len) return;
  const uint8_t *frame = payload + radiotap;
  size_t frame_len = len - radiotap;
  int group = frame[ADDR1] & 1, data = (frame[0] & 0x0c) == 0x08;
  /* Only 1 Mb/s gets on air below 5 GHz so far. Above, data to one station
   * goes faster than the frames every neighbour has to hear. */
  uint16_t rate = r->channel <= 14 ? MT7902_RATE_CCK_1M
                  : data && !group ? MT7902_RATE_OFDM_24M
                                   : MT7902_RATE_OFDM_6M;
  if (mt7902_send_frame(&r->dev, frame, frame_len, rate, group))
    r->tx_refused++;
  else
    r->tx_ok++;
}

/* Returns -1 for a message the protocol does not allow. */
static int handle_message(radio *r, uint16_t type, const uint8_t *payload,
                          size_t len) {
  switch (type) {
  case RADIO_LINK_TX:
    transmit(r, payload, len);
    return 0;
  case RADIO_LINK_SET_CHANNEL:
    if (len != 1) return -1;
    if (payload[0] != r->channel &&
        !mt7902_set_channel(&r->dev, &r->caps, payload[0])) {
      r->channel = payload[0];
      printf("radio: channel %u\n", r->channel);
    }
    return 0;
  case RADIO_LINK_SET_BSSID_FILTER:
    if (len != 6) return -1;
    memcpy(r->bssid, payload, 6);
    r->filter = (payload[0] | payload[1] | payload[2] | payload[3] |
                 payload[4] | payload[5]) != 0;
    return 0;
  case RADIO_LINK_SET_ADDRESS:
    if (len != 6) return -1;
    printf("radio: address %02x:%02x:%02x:%02x:%02x:%02x\n", payload[0],
           payload[1], payload[2], payload[3], payload[4], payload[5]);
    mt7902_set_address(&r->dev, payload);
    return 0;
  default:
    return 0; /* a newer client's message */
  }
}

static int drain_client(radio *r, pipe_io *p) {
  size_t at = 0;
  for (;;) {
    struct radio_link_header header;
    if (p->filled - at < sizeof(header)) break;
    memcpy(&header, p->pending + at, sizeof(header));
    if (header.length > RADIO_LINK_MAX_PAYLOAD) return -1;
    if (p->filled - at < sizeof(header) + header.length) break;
    if (handle_message(r, header.type, p->pending + at + sizeof(header),
                       header.length))
      return -1;
    at += sizeof(header) + header.length;
  }
  memmove(p->pending, p->pending + at, p->filled - at);
  p->filled -= at;
  return 0;
}

/* Returns 1 when a buffer was taken from either ring. */
static int forward_received(radio *r, pipe_io *p) {
  mt7902_ring *rings[] = {&r->dev.rx_data, &r->dev.rx_event};
  int busy = 0;
  for (int i = 0; i < 2; i++) {
    mt7902_rx rx;
    mt7902_frame f;
    if (!mt7902_rx_poll(&r->dev, rings[i], &rx)) continue;
    busy = 1;
    if (!p->connected || mt7902_rx_frame(&rx, &f) || f.fcs_error) continue;
    if (r->filter && (f.len < 24 || memcmp(f.data + ADDR3, r->bssid, 6))) {
      r->rx_dropped++;
      continue;
    }
    uint8_t radiotap[RADIOTAP_RX_BYTES] = {0};
    uint32_t present = 1u << 1 | 1u << 5;
    radiotap[2] = RADIOTAP_RX_BYTES;
    memcpy(radiotap + 4, &present, 4);
    radiotap[9] = (uint8_t)f.rssi;
    if (!pipe_send(p, RADIO_LINK_RX, radiotap, sizeof(radiotap), f.data, f.len))
      r->rx_sent++;
  }
  return busy;
}

int main(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: radiod <patch.bin> <ram.bin> <channel> [seconds]\n");
    return 2;
  }
  static radio r;
  static pipe_io pipe;
  r.channel = (uint8_t)atoi(argv[3]);
  ULONGLONG deadline =
      argc > 4 ? GetTickCount64() + 1000ull * (unsigned)atoi(argv[4]) : 0;
  setvbuf(stdout, NULL, _IONBF, 0);
  SetConsoleCtrlHandler(on_console, TRUE);

  if (pipe_open(&pipe)) {
    fprintf(stderr, "radio: cannot create %s (error %lu): is another radio "
                    "daemon running?\n",
            RADIO_LINK_PIPE_NAME, GetLastError());
    return 2;
  }
  if (mt7902_start(&r.dev, argv[1], argv[2])) return 2;
  int rc = 1;
  if (mt7902_radio_init(&r.dev, &r.caps) ||
      mt7902_monitor_start(&r.dev, &r.caps, r.channel))
    goto out;
  printf("radio: MT7902 on channel %u, waiting on %s\n", r.channel,
         RADIO_LINK_PIPE_NAME);

  while (!g_stop && !r.dev.io_failed &&
         (!deadline || GetTickCount64() < deadline)) {
    int busy = forward_received(&r, &pipe);
    int event = pipe_pump(&pipe);
    if (event == 1) {
      struct radio_link_hello hello = {RADIO_LINK_VERSION, r.channel, 20, {0}};
      r.clients++;
      r.filter = 0;
      printf("radio: client connected\n");
      pipe_send(&pipe, RADIO_LINK_HELLO, (const uint8_t *)&hello, sizeof(hello),
                NULL, 0);
    }
    if (pipe.connected && (event < 0 || drain_client(&r, &pipe))) {
      printf("radio: client gone; %llu frames to it, %llu filtered out, %llu "
             "sent on air, %llu refused\n",
             r.rx_sent, r.rx_dropped, r.tx_ok, r.tx_refused);
      pipe_drop(&pipe);
      mt7902_set_address(&r.dev, r.caps.mac);
    }
    if (!busy && !pipe.filled) SwitchToThread();
  }
  rc = r.dev.io_failed ? 1 : 0;
  printf("radio: stopping after %llu clients; %llu frames to them, %llu sent "
         "on air, %llu refused\n",
         r.clients, r.rx_sent, r.tx_ok, r.tx_refused);
out:
  mt7902_close(&r.dev);
  return rc;
}
