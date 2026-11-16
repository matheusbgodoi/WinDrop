/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* User-space bring-up of the MediaTek MT7902 over the winawdl register proxy:
 * power control, WFDMA rings in polled mode, MCU commands and firmware
 * download. The sequence follows mt76's mt7921e path. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "dma_mem.h"

typedef struct mt7902_ring {
  uint32_t regs;      /* BAR offset of the ring's register block */
  uint32_t ndesc;
  uint32_t buf_bytes; /* size of each attached buffer */
  uint32_t *desc;     /* ndesc descriptors of four words */
  size_t *buf_offset; /* slab offset of each descriptor's buffer */
  uint32_t head;      /* TX: next descriptor to fill. RX: next to read */
} mt7902_ring;

typedef struct mt7902_dev {
  void *proxy; /* HANDLE */
  dma_mem mem;
  size_t mem_used;
  mt7902_ring tx_mcu, tx_fwdl, rx_event, rx_data;
  uint8_t mcu_seq;
  int io_failed;
} mt7902_dev;

/* A received buffer, valid until the next mt7902_rx_poll on the same ring. */
typedef struct mt7902_rx {
  const uint8_t *data;
  uint32_t len;
} mt7902_rx;

/* Nonzero prints every MCU event as it arrives. */
extern int mt7902_trace;

int mt7902_open(mt7902_dev *dev);
/* Stops DMA and releases everything. Safe on a partly opened device. */
void mt7902_close(mt7902_dev *dev);

uint32_t mt7902_rr(mt7902_dev *dev, uint32_t addr);
void mt7902_wr(mt7902_dev *dev, uint32_t addr, uint32_t value);

/* Power-on, reset, rings and DMA enable, up to the point where the ROM code
 * accepts download commands. */
int mt7902_bringup(mt7902_dev *dev);
/* Downloads the patch and the RAM image and waits for the firmware to run. */
int mt7902_load_firmware(mt7902_dev *dev, const char *patch_path,
                         const char *ram_path);

/* Sends one MCU command; `cmd` is an id from mt7902_regs.h, built with the
 * MCU_EXT_CMD, MCU_CE_CMD or MCU_UNI_CMD macros where it is not a plain one.
 * With `response`, waits for the event carrying the same sequence number and
 * copies up to `response_cap` bytes of it, MCU header included; returns its
 * length, or a negative value on failure. */
int mt7902_mcu_cmd(mt7902_dev *dev, uint32_t cmd, const void *payload,
                   size_t len, uint8_t *response, size_t response_cap);
/* Returns 1 with a frame, 0 when the ring is empty. */
int mt7902_rx_poll(mt7902_dev *dev, mt7902_ring *ring, mt7902_rx *out);
