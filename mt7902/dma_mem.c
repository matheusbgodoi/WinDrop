/* SPDX-License-Identifier: MIT */
#include "dma_mem.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>

#define PAGE_BYTES 4096u
#define BLOCK_BYTES ((size_t)2 << 20)

typedef BOOL(WINAPI *allocate_pages2_fn)(HANDLE, PULONG_PTR, PULONG_PTR,
                                         PMEM_EXTENDED_PARAMETER, ULONG);

static int enable_lock_memory(void) {
  HANDLE token = NULL;
  if (!OpenProcessToken(GetCurrentProcess(),
                        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
    return -1;
  TOKEN_PRIVILEGES tp = {0};
  tp.PrivilegeCount = 1;
  tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
  BOOL found = LookupPrivilegeValueW(NULL, L"SeLockMemoryPrivilege",
                                     &tp.Privileges[0].Luid);
  if (found) AdjustTokenPrivileges(token, FALSE, &tp, 0, NULL, NULL);
  /* AdjustTokenPrivileges succeeds even when the privilege is not held. */
  int ok = found && GetLastError() == ERROR_SUCCESS;
  CloseHandle(token);
  return ok ? 0 : -1;
}

static int compare_frames(const void *a, const void *b) {
  ULONG_PTR x = *(const ULONG_PTR *)a, y = *(const ULONG_PTR *)b;
  return (x > y) - (x < y);
}

static void release_frames(ULONG_PTR *frames, size_t count) {
  ULONG_PTR n = count;
  if (n) FreeUserPhysicalPages(GetCurrentProcess(), &n, frames);
}

/* The system hands out memory wherever it likes, so this asks for `request`
 * chunks, keeps the `want` lowest if they end under the limit, and frees the
 * rest. Each returned entry is the first 4 KiB frame of a chunk. */
static ULONG_PTR *lowest_chunks(size_t chunk_pages, size_t request, size_t want,
                                ULONG_PTR limit_frame) {
  ULONG_PTR *frames = calloc(request, sizeof(*frames));
  if (!frames) return NULL;
  ULONG_PTR got = request;
  BOOL ok;
  if (chunk_pages == 1) {
    ok = AllocateUserPhysicalPages(GetCurrentProcess(), &got, frames);
  } else {
    /* Looked up at run time: the import library carrying it varies by SDK. */
    allocate_pages2_fn allocate = (allocate_pages2_fn)(void *)GetProcAddress(
        GetModuleHandleW(L"kernelbase.dll"), "AllocateUserPhysicalPages2");
    MEM_EXTENDED_PARAMETER large = {0};
    large.Type = MemExtendedParameterAttributeFlags;
    large.ULong64 = MEM_EXTENDED_PARAMETER_NONPAGED_LARGE;
    ok = allocate && allocate(GetCurrentProcess(), &got, frames, &large, 1);
  }
  if (!ok) got = 0;

  qsort(frames, got, sizeof(*frames), compare_frames);
  int fits = got >= want && frames[want - 1] + chunk_pages <= limit_frame;
  size_t keep = fits ? want : 0;
  release_frames(frames + keep, got - keep);
  if (!fits) {
    free(frames);
    return NULL;
  }
  return frames;
}

static int map_chunks(dma_mem *mem, ULONG_PTR *frames, size_t chunk_pages,
                      size_t count) {
  size_t chunk_bytes = chunk_pages * PAGE_BYTES, bytes = count * chunk_bytes;
  uint8_t *va = VirtualAlloc(NULL, bytes, MEM_RESERVE | MEM_PHYSICAL,
                             PAGE_READWRITE);
  uint64_t *phys = calloc(count, sizeof(*phys));
  ULONG_PTR *pages = calloc(chunk_pages, sizeof(*pages));
  int ok = va && phys && pages;
  /* A block maps as its consecutive 4 KiB frames. */
  for (size_t c = 0; ok && c < count; c++) {
    for (size_t p = 0; p < chunk_pages; p++) pages[p] = frames[c] + p;
    ok = MapUserPhysicalPages(va + c * chunk_bytes, chunk_pages, pages);
    phys[c] = (uint64_t)frames[c] * PAGE_BYTES;
  }
  free(pages);
  if (!ok) {
    if (va) VirtualFree(va, 0, MEM_RELEASE);
    free(phys);
    return -1;
  }
  memset(va, 0, bytes);
  mem->va = va;
  mem->chunk_bytes = chunk_bytes;
  mem->chunk_count = count;
  mem->chunk_phys = phys;
  mem->os = frames;
  return 0;
}

int dma_mem_alloc(dma_mem *mem, size_t bytes, unsigned address_bits) {
  memset(mem, 0, sizeof(*mem));
  if (enable_lock_memory()) return -1;
  ULONG_PTR limit = (ULONG_PTR)1 << (address_bits - 12);

  static const struct {
    size_t chunk_pages, request;
  } tries[] = {
      {512, 16},      {512, 256},      {512, 4096}, /* 2 MiB blocks */
      {1, 1u << 15},  {1, 1u << 17},   {1, 1u << 19}, /* single pages */
  };
  for (size_t i = 0; i < sizeof(tries) / sizeof(tries[0]); i++) {
    size_t chunk_bytes = tries[i].chunk_pages * PAGE_BYTES;
    size_t want = (bytes + chunk_bytes - 1) / chunk_bytes;
    size_t request = tries[i].request > want ? tries[i].request : want;
    ULONG_PTR *frames =
        lowest_chunks(tries[i].chunk_pages, request, want, limit);
    if (!frames) continue;
    if (!map_chunks(mem, frames, tries[i].chunk_pages, want)) return 0;
    release_frames(frames, want);
    free(frames);
    return -2;
  }
  return -2;
}

void dma_mem_free(dma_mem *mem) {
  if (!mem->va) return;
  size_t chunk_pages = mem->chunk_bytes / PAGE_BYTES;
  MapUserPhysicalPages(mem->va, mem->chunk_count * chunk_pages, NULL);
  VirtualFree(mem->va, 0, MEM_RELEASE);
  release_frames(mem->os, mem->chunk_count); /* one entry per chunk */
  free(mem->os);
  free(mem->chunk_phys);
  memset(mem, 0, sizeof(*mem));
}
