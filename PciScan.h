/** @file
  PciScan.h - PCI/PCIe display-adapter enumeration.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef GPUCHECK_PCISCAN_H_
#define GPUCHECK_PCISCAN_H_

#include "GpuCheck.h"

#define GPUCHECK_MAX_DISPLAY_DEVICES  32

/* PCI configuration space offsets (Type 0 header) */
#define PCI_OFF_VENDOR_ID        0x00
#define PCI_OFF_DEVICE_ID        0x02
#define PCI_OFF_COMMAND          0x04
#define PCI_OFF_STATUS           0x06
#define PCI_OFF_REVISION_ID      0x08
#define PCI_OFF_PROG_IF          0x09
#define PCI_OFF_SUBCLASS         0x0A
#define PCI_OFF_BASE_CLASS       0x0B
#define PCI_OFF_HEADER_TYPE      0x0E
#define PCI_OFF_SUBSYS_VENDOR    0x2C
#define PCI_OFF_SUBSYS_DEVICE    0x2E
#define PCI_OFF_CAP_PTR          0x34

#define PCI_STATUS_CAP_LIST      0x0010
#define PCI_HEADER_TYPE_MASK     0x7F
#define PCI_HEADER_MULTI_FUNC    0x80

#define PCI_CAP_ID_PCIE          0x10

/* PCI base class for display controllers */
#define PCI_CLASS_DISPLAY        0x03
#define PCI_SUBCLASS_VGA         0x00
#define PCI_SUBCLASS_XGA         0x01
#define PCI_SUBCLASS_3D          0x02
#define PCI_SUBCLASS_OTHER       0x80

/* Vendor IDs we name explicitly */
#define PCI_VENDOR_NVIDIA        0x10DE
#define PCI_VENDOR_AMD_ATI       0x1002
#define PCI_VENDOR_INTEL         0x8086
#define PCI_VENDOR_AMD           0x1022
#define PCI_VENDOR_MATROX        0x102B
#define PCI_VENDOR_ASPEED        0x1A03
#define PCI_VENDOR_VMWARE        0x15AD
#define PCI_VENDOR_REDHAT        0x1AF4
#define PCI_VENDOR_REDHAT_QUMRANET 0x1B36
#define PCI_VENDOR_S3            0x5333
#define PCI_VENDOR_CIRRUS        0x1013
#define PCI_VENDOR_XGI           0x18CA
#define PCI_VENDOR_SIS           0x1039
#define PCI_VENDOR_VIA           0x1106

/* PCIe capability structure offsets, relative to the capability header */
#define PCIE_CAP_OFF_CAPABILITIES  0x02   /* PCI Express Capabilities Register  */
#define PCIE_CAP_OFF_LINK_CAP      0x0C   /* Link Capabilities                  */
#define PCIE_CAP_OFF_LINK_STATUS   0x12   /* Link Status                        */
#define PCIE_CAP_OFF_LINK_CAP2     0x2C   /* Link Capabilities 2 (cap ver >= 2) */

/* Device/Port Type, PCI Express Capabilities Register bits 7:4 */
#define PCIE_PORT_TYPE_ENDPOINT          0x0
#define PCIE_PORT_TYPE_LEGACY_ENDPOINT   0x1
#define PCIE_PORT_TYPE_ROOT_PORT         0x4
#define PCIE_PORT_TYPE_UPSTREAM          0x5
#define PCIE_PORT_TYPE_DOWNSTREAM        0x6
#define PCIE_PORT_TYPE_RC_INTEGRATED     0x9

typedef enum {
  GpuTypeUnknown = 0,
  GpuTypeIntegrated,
  GpuTypeDiscrete
} GPU_TYPE;

typedef enum {
  ScanSourcePciIo = 0,
  ScanSourceRootBridge
} SCAN_SOURCE;

typedef struct {
  UINTN        Segment;
  UINTN        Bus;
  UINTN        Device;
  UINTN        Function;

  UINT16       VendorId;
  UINT16       DeviceId;
  UINT16       SubsysVendorId;
  UINT16       SubsysDeviceId;

  UINT8        BaseClass;
  UINT8        SubClass;
  UINT8        ProgInterface;
  UINT8        RevisionId;
  UINT8        HeaderType;

  UINT64       RomSize;          /* PCI expansion ROM size, 0 if none/unknown */

  BOOLEAN      PcieCapValid;
  UINT8        PcieCapOffset;
  UINT8        PcieCapVersion;
  UINT8        PcieDevicePortType;
  UINT8        CurrentLinkSpeed;  /* encoded 1..6, 0 = unknown */
  UINT8        CurrentLinkWidth;  /* lanes, 0 = unknown        */
  UINT8        MaxLinkSpeed;
  UINT8        MaxLinkWidth;

  GPU_TYPE     Type;
  SCAN_SOURCE  Source;
} GPU_DEVICE_INFO;

typedef struct {
  UINTN            Count;
  GPU_DEVICE_INFO  Devices[GPUCHECK_MAX_DISPLAY_DEVICES];
  UINTN            TotalPciFunctions;       /* every PCI function seen        */
  UINTN            PciIoHandleCount;
  BOOLEAN          UsedRootBridgeFallback;
  BOOLEAN          ConnectAllAttempted;
  BOOLEAN          Overflow;                /* more than MAX display devices  */
} GPU_SCAN_RESULTS;

/**
  Enumerate every PCI function reachable through EFI_PCI_IO_PROTOCOL and
  record those whose PCI Base Class is 0x03 (display controller).

  If that yields no display controllers, a secondary brute-force scan through
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL is attempted so that a device the PCI bus
  driver skipped can still be reported.  Both passes are read-only.
**/
EFI_STATUS
GpuScanPci (
  GPU_SCAN_RESULTS  *Results
  );

CONST CHAR16 *  GpuVendorName (UINT16 VendorId);
CONST CHAR16 *  GpuClassName (UINT8 BaseClass, UINT8 SubClass);
CONST CHAR16 *  GpuLinkSpeedName (UINT8 EncodedSpeed);
CONST CHAR16 *  GpuTypeName (GPU_TYPE Type);
CONST CHAR16 *  GpuPortTypeName (UINT8 PortType);

BOOLEAN  GpuHasProbableDiscrete (CONST GPU_SCAN_RESULTS *Results);

#endif /* GPUCHECK_PCISCAN_H_ */
