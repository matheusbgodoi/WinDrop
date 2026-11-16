/* SPDX-License-Identifier: MIT */
/* Phase 1 probe: opens the register proxy bound to the MT7902 and reads the
 * chip ID through the L1 remap window, as mt7921e does in its probe.
 * Register addresses come from mt76 (mt792x_regs.h, mt7921/regs.h).
 *
 *   mt7902_chipid          read only, apart from the remap window it restores
 *   mt7902_chipid --own    also asks the chip for driver ownership first
 */
#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../driver/regproxy/regproxy_ioctl.h"

#define MT_HIF_REMAP_L1 0xfe24cu      /* MT_INFRA(0x24c) */
#define MT_HIF_REMAP_BASE_L1 0x40000u
#define MT_HW_CHIPID 0x70010200u
#define MT_HW_REV 0x70010204u
#define MT_CONN_ON_LPCTL 0xe0010u     /* 0x7c060010 through the fixed map */
#define PCIE_LPCR_HOST_CLR_OWN 0x2u
#define PCIE_LPCR_HOST_OWN_SYNC 0x4u

static HANDLE open_proxy(void) {
  HDEVINFO set = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_WINAWDL_REGPROXY, NULL,
                                      NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (set == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

  SP_DEVICE_INTERFACE_DATA ifc = {sizeof(ifc)};
  HANDLE h = INVALID_HANDLE_VALUE;
  if (SetupDiEnumDeviceInterfaces(set, NULL, &GUID_DEVINTERFACE_WINAWDL_REGPROXY,
                                  0, &ifc)) {
    DWORD need = 0;
    SetupDiGetDeviceInterfaceDetailW(set, &ifc, NULL, 0, &need, NULL);
    SP_DEVICE_INTERFACE_DETAIL_DATA_W *detail = calloc(1, need);
    if (detail) {
      detail->cbSize = sizeof(*detail);
      if (SetupDiGetDeviceInterfaceDetailW(set, &ifc, detail, need, NULL, NULL))
        h = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                        OPEN_EXISTING, 0, NULL);
      free(detail);
    }
  }
  SetupDiDestroyDeviceInfoList(set);
  return h;
}

static int reg_op(HANDLE h, unsigned kind, unsigned offset, unsigned *value) {
  REGPROXY_OP op = {offset, *value, 4, (unsigned char)kind, 0};
  DWORD got = 0;
  if (!DeviceIoControl(h, IOCTL_REGPROXY_BATCH, &op, sizeof(op), &op, sizeof(op),
                       &got, NULL) ||
      got != sizeof(op)) {
    fprintf(stderr, "register op at 0x%05x failed: %lu\n", offset, GetLastError());
    return -1;
  }
  *value = op.value;
  return 0;
}

static int rr(HANDLE h, unsigned offset, unsigned *value) {
  *value = 0;
  return reg_op(h, REGPROXY_READ, offset, value);
}

static int wr(HANDLE h, unsigned offset, unsigned value) {
  return reg_op(h, REGPROXY_WRITE, offset, &value);
}

/* Reads a chip address outside the fixed map by pointing the L1 window at it. */
static int l1_rr(HANDLE h, unsigned addr, unsigned *value) {
  unsigned remap = 0, readback = 0;
  if (rr(h, MT_HIF_REMAP_L1, &remap)) return -1;
  if (wr(h, MT_HIF_REMAP_L1, (remap & 0xffff0000u) | (addr >> 16))) return -1;
  if (rr(h, MT_HIF_REMAP_L1, &readback)) return -1; /* flushes the write */
  int rc = rr(h, MT_HIF_REMAP_BASE_L1 + (addr & 0xffffu), value);
  wr(h, MT_HIF_REMAP_L1, remap);
  return rc;
}

static int take_ownership(HANDLE h) {
  unsigned lpctl = 0;
  if (wr(h, MT_CONN_ON_LPCTL, PCIE_LPCR_HOST_CLR_OWN)) return -1;
  for (int i = 0; i < 50; i++) {
    if (rr(h, MT_CONN_ON_LPCTL, &lpctl)) return -1;
    if (!(lpctl & PCIE_LPCR_HOST_OWN_SYNC)) return 0;
    Sleep(10);
  }
  fprintf(stderr, "driver ownership not granted, LPCTL 0x%08x\n", lpctl);
  return -1;
}

int main(int argc, char **argv) {
  HANDLE h = open_proxy();
  if (h == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "register proxy not found or not openable: %lu\n",
            GetLastError());
    return 2;
  }

  REGPROXY_INFO info = {0};
  DWORD got = 0;
  if (!DeviceIoControl(h, IOCTL_REGPROXY_GET_INFO, NULL, 0, &info, sizeof(info),
                       &got, NULL)) {
    fprintf(stderr, "GET_INFO failed: %lu\n", GetLastError());
    return 2;
  }
  printf("proxy version %u, BAR 0x%llx, length 0x%x, direct mapped %u\n",
         info.version, info.bar_phys, info.bar_len, info.direct_mapped);
  if (info.bar_len <= MT_HIF_REMAP_L1) {
    fprintf(stderr, "BAR too small for the remap register, wrong BAR mapped\n");
    return 3;
  }

  unsigned lpctl = 0, chip = 0, rev = 0;
  if (rr(h, MT_CONN_ON_LPCTL, &lpctl)) return 4;
  printf("LPCTL 0x%08x (%s)\n", lpctl,
         lpctl & PCIE_LPCR_HOST_OWN_SYNC ? "firmware owns the chip"
                                         : "driver owns the chip");

  if (argc > 1 && !strcmp(argv[1], "--own")) {
    if (take_ownership(h)) return 5;
    puts("driver ownership granted");
  }

  if (l1_rr(h, MT_HW_CHIPID, &chip) || l1_rr(h, MT_HW_REV, &rev)) return 4;
  printf("chip id 0x%04x, revision 0x%04x\n", chip & 0xffff, rev & 0xff);

  CloseHandle(h);
  return (chip & 0xffff) == 0x7902 ? 0 : 1;
}
