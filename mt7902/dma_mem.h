/* SPDX-License-Identifier: MIT */
/* Locked physical memory for user-space DMA on Windows, whose physical
 * addresses the chip uses as bus addresses. The technique is winawdl's
 * (DESIGN-NOTES.md, "Windows design"): 2 MiB blocks where enough of them are
 * free under the address limit, single 4 KiB pages otherwise. */
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct dma_mem {
  uint8_t *va;          /* one contiguous view over all chunks */
  size_t chunk_bytes;   /* 2 MiB or 4 KiB; physically contiguous inside one */
  size_t chunk_count;
  uint64_t *chunk_phys; /* physical address of each chunk, in view order */
  void *os;             /* frames, kept for release */
} dma_mem;

/* Gets at least `bytes` of locked, zeroed memory, all of it below
 * `1 << address_bits`. Needs SeLockMemoryPrivilege. Returns 0 on success,
 * -1 without the privilege, -2 when memory that low is not free. */
int dma_mem_alloc(dma_mem *mem, size_t bytes, unsigned address_bits);
/* Only safe once the chip's DMA is stopped. */
void dma_mem_free(dma_mem *mem);

/* Bus address of a view offset. An object must not straddle a chunk. */
static inline uint64_t dma_mem_phys(const dma_mem *mem, size_t offset) {
  return mem->chunk_phys[offset / mem->chunk_bytes] + offset % mem->chunk_bytes;
}
