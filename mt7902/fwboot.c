/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
/* Phase 2 test: brings the MT7902 up, downloads its firmware and reports
 * whether it runs.   fwboot <patch.bin> <ram.bin> */
#include "mt7902_hw.h"
#include "mt7902_regs.h"
#include <stdio.h>
#include <windows.h>

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: fwboot <patch.bin> <ram.bin>\n");
    return 2;
  }
  mt7902_dev dev;
  if (mt7902_open(&dev)) return 2;

  int rc = 1;
  if (mt7902_bringup(&dev)) {
    fprintf(stderr, "bring-up failed\n");
    goto out;
  }
  printf("bring-up done, DMA slab of %zu chunks of %zu KiB from 0x%llx, MCU "
         "state 0x%x\n",
         dev.mem.chunk_count, dev.mem.chunk_bytes / 1024,
         (unsigned long long)dev.mem.chunk_phys[0],
         mt7902_rr(&dev, MT_CONN_ON_MISC));

  if (mt7902_load_firmware(&dev, argv[1], argv[2])) {
    fprintf(stderr, "firmware load failed\n");
    goto out;
  }
  printf("firmware running, MCU state 0x%x\n", mt7902_rr(&dev, MT_CONN_ON_MISC));

  /* Whatever the firmware says on its own in the first moments. */
  ULONGLONG until = GetTickCount64() + 500;
  int events = 0;
  mt7902_rx rx;
  while (GetTickCount64() < until)
    if (mt7902_rx_poll(&dev, &dev.rx_event, &rx)) {
      printf("event %d: %u bytes, eid 0x%02x\n", events++, rx.len,
             rx.len > 28 ? rx.data[28] : 0);
    }
  rc = 0;
out:
  mt7902_close(&dev);
  return rc;
}
