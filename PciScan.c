/** @file
  PciScan.c - Enumerate PCI/PCIe display controllers.

  Primary path is EFI_PCI_IO_PROTOCOL, as required: every handle exposing
  PciIo is queried and its *configuration space* is read directly.  A device
  is treated as a display adapter purely on PCI Base Class == 0x03; the
  presence of a framebuffer or a Graphics Output Protocol handle is never
  used, because a GPU can enumerate on PCI while failing to initialise any
  display output at all - which is exactly the case this tool exists to catch.

  Everything here is read-only.  No BAR is written, no bus is reset, no
  device is re-enumerated.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "PciScan.h"

/* -------------------------------------------------------------------------
 * Config-space access abstraction
 *
 * Lets the PciIo pass and the Root Bridge fallback pass share the header
 * decoder and the PCIe capability walker.
 * ---------------------------------------------------------------------- */

typedef struct {
  EFI_PCI_IO_PROTOCOL              *PciIo;       /* preferred, when non-NULL */
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL  *RootBridge;  /* fallback                 */
  UINTN                             Bus;
  UINTN                             Device;
  UINTN                             Function;
} CFG_CTX;

STATIC
EFI_STATUS
CfgRead (
  CFG_CTX  *Ctx,
  UINT32    Offset,
  UINTN     Width,      /* 1, 2 or 4 bytes */
  VOID     *Buffer
  )
{
  if (Ctx->PciIo != NULL) {
    EFI_PCI_IO_PROTOCOL_WIDTH  W;

    switch (Width) {
      case 1:  W = EfiPciIoWidthUint8;  break;
      case 2:  W = EfiPciIoWidthUint16; break;
      case 4:  W = EfiPciIoWidthUint32; break;
      default: return EFI_INVALID_PARAMETER;
    }
    return Ctx->PciIo->Pci.Read (Ctx->PciIo, W, Offset, 1, Buffer);
  }

  if (Ctx->RootBridge != NULL) {
    EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_WIDTH  W;

    switch (Width) {
      case 1:  W = EfiPciWidthUint8;  break;
      case 2:  W = EfiPciWidthUint16; break;
      case 4:  W = EfiPciWidthUint32; break;
      default: return EFI_INVALID_PARAMETER;
    }
    return Ctx->RootBridge->Pci.Read (
                                 Ctx->RootBridge,
                                 W,
                                 EFI_PCI_ADDRESS (Ctx->Bus, Ctx->Device, Ctx->Function, Offset),
                                 1,
                                 Buffer
                                 );
  }

  return EFI_INVALID_PARAMETER;
}

/**
  Read the 64-byte type-0 configuration header as raw bytes.
  Uses 32-bit accesses, which every PCI implementation supports.
**/
STATIC
EFI_STATUS
CfgReadHeader (
  CFG_CTX  *Ctx,
  UINT8     Header[64]
  )
{
  UINT32      Dword;
  UINT32      Offset;
  EFI_STATUS  Status;

  for (Offset = 0; Offset < 64; Offset += 4) {
    Dword  = 0xFFFFFFFF;
    Status = CfgRead (Ctx, Offset, 4, &Dword);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    Header[Offset + 0] = (UINT8)(Dword >> 0);
    Header[Offset + 1] = (UINT8)(Dword >> 8);
    Header[Offset + 2] = (UINT8)(Dword >> 16);
    Header[Offset + 3] = (UINT8)(Dword >> 24);
  }
  return EFI_SUCCESS;
}

STATIC
UINT16
Rd16 (
  CONST UINT8  *B,
  UINTN         Off
  )
{
  return (UINT16)((UINT16)B[Off] | ((UINT16)B[Off + 1] << 8));
}

/* -------------------------------------------------------------------------
 * PCI Express capability
 * ---------------------------------------------------------------------- */

/**
  Walk the capability list looking for the PCI Express capability (ID 0x10)
  and, if found, record port type and link speed/width.

  Failure here is never fatal: the device is still reported, just without
  link information.  PCI enumeration is mandatory, link detail is a bonus.
**/
STATIC
VOID
ParsePcieCapability (
  CFG_CTX          *Ctx,
  CONST UINT8      *Header,
  GPU_DEVICE_INFO  *Info
  )
{
  UINT16  Status16;
  UINT8   CapPtr;
  UINT8   CapId;
  UINT8   NextPtr;
  UINT16  PcieCaps;
  UINT32  LinkCap;
  UINT16  LinkStatus;
  UINT32  LinkCap2;
  UINTN   Guard;

  Info->PcieCapValid = FALSE;

  Status16 = Rd16 (Header, PCI_OFF_STATUS);
  if ((Status16 & PCI_STATUS_CAP_LIST) == 0) {
    return;                                   /* legacy PCI device, no caps */
  }

  CapPtr = (UINT8)(Header[PCI_OFF_CAP_PTR] & 0xFC);

  for (Guard = 0; Guard < 64; Guard++) {
    if ((CapPtr < 0x40) || (CapPtr == 0xFF)) {
      return;
    }
    if (EFI_ERROR (CfgRead (Ctx, CapPtr + 0, 1, &CapId))) {
      return;
    }
    if (EFI_ERROR (CfgRead (Ctx, CapPtr + 1, 1, &NextPtr))) {
      return;
    }
    if (CapId == PCI_CAP_ID_PCIE) {
      break;
    }
    if (CapId == 0xFF) {
      return;
    }
    CapPtr = (UINT8)(NextPtr & 0xFC);
  }

  if (Guard >= 64) {
    return;                                   /* malformed / looping list */
  }

  Info->PcieCapOffset = CapPtr;

  PcieCaps = 0;
  if (EFI_ERROR (CfgRead (Ctx, CapPtr + PCIE_CAP_OFF_CAPABILITIES, 2, &PcieCaps))) {
    return;
  }
  Info->PcieCapVersion     = (UINT8)(PcieCaps & 0x000F);
  Info->PcieDevicePortType = (UINT8)((PcieCaps >> 4) & 0x000F);

  /*
    Every capability register read is bounds-checked against the 256-byte
    legacy config window. CapPtr comes straight from device-reported config
    space and can legitimately be as high as 0xFC; without the check, a device
    returning partially-0xFF config space steers a read past offset 255, which
    EFI_PCI_ADDRESS then re-encodes as an extended-config access on a device
    that may not implement one.
  */
  LinkCap = 0;
  if (((UINTN)CapPtr + PCIE_CAP_OFF_LINK_CAP + 4 <= 0x100) &&
      !EFI_ERROR (CfgRead (Ctx, CapPtr + PCIE_CAP_OFF_LINK_CAP, 4, &LinkCap)))
  {
    Info->MaxLinkSpeed = (UINT8)(LinkCap & 0x0F);
    Info->MaxLinkWidth = (UINT8)((LinkCap >> 4) & 0x3F);
  }

  LinkStatus = 0;
  if (((UINTN)CapPtr + PCIE_CAP_OFF_LINK_STATUS + 2 <= 0x100) &&
      !EFI_ERROR (CfgRead (Ctx, CapPtr + PCIE_CAP_OFF_LINK_STATUS, 2, &LinkStatus)))
  {
    Info->CurrentLinkSpeed = (UINT8)(LinkStatus & 0x0F);
    Info->CurrentLinkWidth = (UINT8)((LinkStatus >> 4) & 0x3F);
  }

  /*
    Link Capabilities 2 carries the Supported Link Speeds Vector, which is the
    authoritative maximum on Gen3-and-later parts (Link Capabilities saturates
    at 0x3 on some silicon).  Only valid for capability version >= 2, and only
    read when the whole dword stays inside the 256-byte legacy config window.
  */
  if ((Info->PcieCapVersion >= 2) &&
      ((UINTN)CapPtr + PCIE_CAP_OFF_LINK_CAP2 + 4 <= 0x100))
  {
    LinkCap2 = 0;
    if (!EFI_ERROR (CfgRead (Ctx, CapPtr + PCIE_CAP_OFF_LINK_CAP2, 4, &LinkCap2))) {
      UINT8  Vector = (UINT8)((LinkCap2 >> 1) & 0x7F);
      UINT8  Bit;

      for (Bit = 7; Bit >= 1; Bit--) {
        if ((Vector & (1u << (Bit - 1))) != 0) {
          if (Bit > Info->MaxLinkSpeed) {
            Info->MaxLinkSpeed = Bit;
          }
          break;
        }
      }
    }
  }

  Info->PcieCapValid = TRUE;
}

/* -------------------------------------------------------------------------
 * Integrated vs discrete heuristic
 *
 * Deliberately conservative.  When the signals do not add up the answer is
 * GpuTypeUnknown - the tool never invents a classification.  Classification
 * is presentation only; it never gates enumeration or reporting.
 * ---------------------------------------------------------------------- */

/**
  Device IDs known to belong to AMD/ATI *integrated* graphics (APU / IGP).
  Expressed as inclusive ranges.  Vendor ID alone can never decide this,
  because AMD ships both APU graphics and add-in Radeon cards under 0x1002.
**/
STATIC CONST UINT16  mAmdIntegratedRanges[][2] = {
  { 0x1309, 0x131D },   /* Kaveri / Godavari / Carrizo APU              */
  { 0x13FE, 0x13FE },   /* Bristol Ridge embedded                        */
  { 0x143F, 0x143F },   /* Raven embedded                                */
  { 0x1506, 0x1506 },   /* Mendocino                                     */
  { 0x150E, 0x150E },   /* Strix                                         */
  { 0x1586, 0x1586 },   /* Strix Halo                                    */
  { 0x15BF, 0x15C8 },   /* Rembrandt / Phoenix family                    */
  { 0x15D8, 0x15DD },   /* Raven Ridge / Picasso                         */
  { 0x15E7, 0x15E7 },   /* Barcelo                                       */
  { 0x1636, 0x1638 },   /* Renoir / Cezanne                              */
  { 0x164C, 0x164E },   /* Lucienne / Phoenix / Raphael iGPU             */
  { 0x1681, 0x1681 },   /* Rembrandt                                     */
  { 0x1900, 0x1901 },   /* Phoenix2                                      */
  { 0x9610, 0x9616 },   /* RS880 IGP                                     */
  { 0x9640, 0x964F },   /* Trinity / Richland IGP                        */
  { 0x9710, 0x9715 },   /* RS880M IGP                                    */
  { 0x9802, 0x980A },   /* Wrestler / Llano IGP                          */
  { 0x9830, 0x9853 },   /* Kabini / Temash IGP                           */
  { 0x9854, 0x9877 },   /* Mullins / Carrizo / Stoney IGP                */
  { 0x98E4, 0x98E4 },   /* Stoney Ridge                                  */
  { 0x9900, 0x990F },   /* Trinity IGP                                   */
};

STATIC
BOOLEAN
IsKnownAmdIntegrated (
  UINT16  DeviceId
  )
{
  UINTN  i;

  for (i = 0; i < ARRAY_SIZE (mAmdIntegratedRanges); i++) {
    if ((DeviceId >= mAmdIntegratedRanges[i][0]) &&
        (DeviceId <= mAmdIntegratedRanges[i][1]))
    {
      return TRUE;
    }
  }
  return FALSE;
}

STATIC
GPU_TYPE
ClassifyGpu (
  CONST GPU_DEVICE_INFO  *Info
  )
{
  /*
    1. A Root Complex Integrated Endpoint is integrated by definition.
  */
  if (Info->PcieCapValid && (Info->PcieDevicePortType == PCIE_PORT_TYPE_RC_INTEGRATED)) {
    return GpuTypeIntegrated;
  }

  /*
    2. Bus 0 of segment 0 is the root complex itself.  An add-in card always
       sits behind a root port on a non-zero bus, so anything on bus 0 is
       part of the chipset/SoC.  This covers Intel iGPUs at 00:02.0.
  */
  if ((Info->Segment == 0) && (Info->Bus == 0)) {
    return GpuTypeIntegrated;
  }

  /*
    3. Vendor-specific knowledge, for devices on a non-zero bus.
  */
  switch (Info->VendorId) {
    case PCI_VENDOR_AMD_ATI:
      if (IsKnownAmdIntegrated (Info->DeviceId)) {
        return GpuTypeIntegrated;         /* APU graphics on an internal bus */
      }
      break;

    case PCI_VENDOR_NVIDIA:
      /* NVIDIA ships no root-complex-integrated graphics on x86 platforms. */
      return GpuTypeDiscrete;

    case PCI_VENDOR_INTEL:
      /* Intel iGPUs are always 00:02.x, handled above.  This is an add-in. */
      return GpuTypeDiscrete;

    default:
      break;
  }

  /*
    4. An expansion ROM means the board carries its own video BIOS in flash,
       which integrated graphics never do - their VBIOS lives in system
       firmware.  Combined with a non-zero bus this is a strong discrete
       signal, so accept it for any remaining vendor (including unknown AMD
       device IDs, which is how a brand-new Radeon gets classified).
  */
  if ((Info->RomSize > 0) &&
      Info->PcieCapValid &&
      ((Info->PcieDevicePortType == PCIE_PORT_TYPE_ENDPOINT) ||
       (Info->PcieDevicePortType == PCIE_PORT_TYPE_LEGACY_ENDPOINT)))
  {
    return GpuTypeDiscrete;
  }

  /*
    5. Not enough evidence.  Say so rather than guess - the raw BDF, VID:DID
       and class on screen are what actually matter.
  */
  return GpuTypeUnknown;
}

/* -------------------------------------------------------------------------
 * Result list management
 * ---------------------------------------------------------------------- */

STATIC
BOOLEAN
AlreadyRecorded (
  CONST GPU_SCAN_RESULTS  *Results,
  UINTN                    Seg,
  UINTN                    Bus,
  UINTN                    Dev,
  UINTN                    Func
  )
{
  UINTN  i;

  for (i = 0; i < Results->Count; i++) {
    if ((Results->Devices[i].Segment  == Seg) &&
        (Results->Devices[i].Bus      == Bus) &&
        (Results->Devices[i].Device   == Dev) &&
        (Results->Devices[i].Function == Func))
    {
      return TRUE;
    }
  }
  return FALSE;
}

/**
  Decode a config header into Info and, if it is a display controller,
  append it to Results.  Returns TRUE if a display controller was appended.
**/
STATIC
BOOLEAN
RecordIfDisplayDevice (
  GPU_SCAN_RESULTS  *Results,
  CFG_CTX           *Ctx,
  CONST UINT8       *Header,
  UINTN              Segment,
  UINTN              Bus,
  UINTN              Dev,
  UINTN              Func,
  UINT64             RomSize,
  SCAN_SOURCE        Source
  )
{
  GPU_DEVICE_INFO  *Info;

  if (Header[PCI_OFF_BASE_CLASS] != PCI_CLASS_DISPLAY) {
    return FALSE;
  }
  if (AlreadyRecorded (Results, Segment, Bus, Dev, Func)) {
    return FALSE;
  }
  if (Results->Count >= GPUCHECK_MAX_DISPLAY_DEVICES) {
    Results->Overflow = TRUE;
    return FALSE;
  }

  Info = &Results->Devices[Results->Count];
  ZeroMem (Info, sizeof (*Info));

  Info->Segment       = Segment;
  Info->Bus           = Bus;
  Info->Device        = Dev;
  Info->Function      = Func;
  Info->VendorId      = Rd16 (Header, PCI_OFF_VENDOR_ID);
  Info->DeviceId      = Rd16 (Header, PCI_OFF_DEVICE_ID);
  Info->RevisionId    = Header[PCI_OFF_REVISION_ID];
  Info->ProgInterface = Header[PCI_OFF_PROG_IF];
  Info->SubClass      = Header[PCI_OFF_SUBCLASS];
  Info->BaseClass     = Header[PCI_OFF_BASE_CLASS];
  Info->HeaderType    = Header[PCI_OFF_HEADER_TYPE];
  Info->RomSize       = RomSize;
  Info->Source        = Source;

  if ((Info->HeaderType & PCI_HEADER_TYPE_MASK) == 0) {
    Info->SubsysVendorId = Rd16 (Header, PCI_OFF_SUBSYS_VENDOR);
    Info->SubsysDeviceId = Rd16 (Header, PCI_OFF_SUBSYS_DEVICE);
  }

  ParsePcieCapability (Ctx, Header, Info);
  Info->Type = ClassifyGpu (Info);

  Results->Count++;
  return TRUE;
}

/**
  Stable sort of the result list by segment/bus/device/function so that the
  display order does not depend on firmware handle ordering.
**/
STATIC
VOID
SortByBdf (
  GPU_SCAN_RESULTS  *Results
  )
{
  UINTN            i;
  UINTN            j;
  GPU_DEVICE_INFO  Tmp;

  for (i = 1; i < Results->Count; i++) {
    CopyMem (&Tmp, &Results->Devices[i], sizeof (Tmp));
    j = i;
    while (j > 0) {
      GPU_DEVICE_INFO  *Prev = &Results->Devices[j - 1];
      UINT64            KeyPrev;
      UINT64            KeyTmp;

      KeyPrev = ((UINT64)Prev->Segment << 32) | ((UINT64)Prev->Bus << 16) |
                ((UINT64)Prev->Device << 8) | (UINT64)Prev->Function;
      KeyTmp = ((UINT64)Tmp.Segment << 32) | ((UINT64)Tmp.Bus << 16) |
               ((UINT64)Tmp.Device << 8) | (UINT64)Tmp.Function;

      if (KeyPrev <= KeyTmp) {
        break;
      }
      CopyMem (&Results->Devices[j], Prev, sizeof (Tmp));
      j--;
    }
    CopyMem (&Results->Devices[j], &Tmp, sizeof (Tmp));
  }
}

/* -------------------------------------------------------------------------
 * Pass 1: EFI_PCI_IO_PROTOCOL (mandatory path)
 * ---------------------------------------------------------------------- */

STATIC
EFI_STATUS
ScanViaPciIo (
  GPU_SCAN_RESULTS  *Results
  )
{
  EFI_STATUS            Status;
  EFI_HANDLE           *Handles = NULL;
  UINTN                 HandleCount = 0;
  UINTN                 Index;
  EFI_PCI_IO_PROTOCOL  *PciIo;
  UINT8                 Header[64];
  CFG_CTX               Ctx;
  UINTN                 Seg, Bus, Dev, Func;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiPciIoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    Results->PciIoHandleCount = 0;
    return Status;
  }

  Results->PciIoHandleCount = HandleCount;

  for (Index = 0; Index < HandleCount; Index++) {
    PciIo = NULL;
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiPciIoProtocolGuid, (VOID **)&PciIo))) {
      continue;
    }
    if (PciIo == NULL) {
      continue;
    }

    Seg = Bus = Dev = Func = 0;
    if (EFI_ERROR (PciIo->GetLocation (PciIo, &Seg, &Bus, &Dev, &Func))) {
      continue;
    }

    ZeroMem (&Ctx, sizeof (Ctx));
    Ctx.PciIo    = PciIo;
    Ctx.Bus      = Bus;
    Ctx.Device   = Dev;
    Ctx.Function = Func;

    ZeroMem (Header, sizeof (Header));
    if (EFI_ERROR (CfgReadHeader (&Ctx, Header))) {
      continue;
    }

    if (Rd16 (Header, PCI_OFF_VENDOR_ID) == 0xFFFF) {
      continue;
    }

    Results->TotalPciFunctions++;

    RecordIfDisplayDevice (
      Results,
      &Ctx,
      Header,
      Seg, Bus, Dev, Func,
      PciIo->RomSize,
      ScanSourcePciIo
      );
  }

  FreePool (Handles);
  return EFI_SUCCESS;
}

/* -------------------------------------------------------------------------
 * Pass 2: EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL brute force (fallback only)
 * ---------------------------------------------------------------------- */

#pragma pack(1)
/*
  ACPI 6.x QWORD Address Space Descriptor, as returned by
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL.Configuration().

    byte  0      0x8A
    bytes 1-2    Length (0x2B)
    byte  3      Resource Type (0 = memory, 1 = I/O, 2 = bus)
    byte  4      General Flags
    byte  5      Type Specific Flags
    bytes 6-13   Address Space Granularity   <- QWORD, not a byte
    bytes 14-21  Address Range Minimum
    bytes 22-29  Address Range Maximum
    bytes 30-37  Address Translation Offset
    bytes 38-45  Address Length

  Total 46 bytes. Granularity being a QWORD matters: declaring it as a byte
  shifts every following field down by 7 and makes the decoded bus range
  garbage.
*/
typedef struct {
  UINT8   Desc;
  UINT16  Len;
  UINT8   ResType;
  UINT8   GenFlag;
  UINT8   SpecificFlag;
  UINT64  AddrSpaceGranularity;
  UINT64  AddrRangeMin;
  UINT64  AddrRangeMax;
  UINT64  AddrTranslationOffset;
  UINT64  AddrLen;
} GPUCHECK_ACPI_ADDRESS_SPACE_DESC;
#pragma pack()

#define ACPI_QWORD_DESC_MIN_LEN  0x2B

#define ACPI_QWORD_ADDRESS_SPACE_DESC  0x8A
#define ACPI_END_TAG_DESC              0x79
#define ACPI_RESTYPE_BUS               0x02

/**
  Ask the root bridge which bus numbers it actually owns, so the brute-force
  scan never pokes config space outside the bridge's own range.
  Falls back to 0..255 if the descriptor cannot be read.
**/
STATIC
VOID
GetRootBridgeBusRange (
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL  *RootBridge,
  UINTN                            *BusMin,
  UINTN                            *BusMax
  )
{
  VOID                              *Resources = NULL;
  GPUCHECK_ACPI_ADDRESS_SPACE_DESC  *Desc;
  UINTN                              Guard;

  *BusMin = 0;
  *BusMax = 255;

  if (RootBridge->Configuration == NULL) {
    return;
  }
  if (EFI_ERROR (RootBridge->Configuration (RootBridge, &Resources))) {
    return;
  }
  if (Resources == NULL) {
    return;
  }

  Desc = (GPUCHECK_ACPI_ADDRESS_SPACE_DESC *)Resources;
  for (Guard = 0; (Guard < 32) && (Desc->Desc == ACPI_QWORD_ADDRESS_SPACE_DESC); Guard++) {
    /*
      Never trust the length field. Too small and the QWORD field reads below
      run off the end of the firmware's buffer; absurdly large and the pointer
      advance walks into unmapped memory.
    */
    if ((Desc->Len < ACPI_QWORD_DESC_MIN_LEN) || (Desc->Len > 0x100)) {
      return;
    }

    if (Desc->ResType == ACPI_RESTYPE_BUS) {
      /* Apply the range atomically, so a half-valid descriptor cannot leave
         BusMin/BusMax inconsistent with each other. */
      if ((Desc->AddrRangeMin <= 255) &&
          (Desc->AddrRangeMax <= 255) &&
          (Desc->AddrRangeMin <= Desc->AddrRangeMax))
      {
        *BusMin = (UINTN)Desc->AddrRangeMin;
        *BusMax = (UINTN)Desc->AddrRangeMax;
      }
      return;
    }

    Desc = (GPUCHECK_ACPI_ADDRESS_SPACE_DESC *)((UINT8 *)Desc + Desc->Len + 3);
    if (((UINT8 *)Desc)[0] == ACPI_END_TAG_DESC) {
      break;
    }
  }
}

STATIC
EFI_STATUS
ScanViaRootBridge (
  GPU_SCAN_RESULTS  *Results
  )
{
  EFI_STATUS                        Status;
  EFI_HANDLE                       *Handles = NULL;
  UINTN                             HandleCount = 0;
  UINTN                             Index;
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL  *RootBridge;
  UINT8                             Header[64];
  CFG_CTX                           Ctx;
  UINTN                             Bus, Dev, Func;
  UINTN                             BusMin, BusMax;
  UINT32                            VendorDevice;
  UINT8                             HeaderType;
  BOOLEAN                           MultiFunction;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiPciRootBridgeIoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    RootBridge = NULL;
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiPciRootBridgeIoProtocolGuid, (VOID **)&RootBridge))) {
      continue;
    }
    if ((RootBridge == NULL) || (RootBridge->Pci.Read == NULL)) {
      continue;
    }

    GetRootBridgeBusRange (RootBridge, &BusMin, &BusMax);

    for (Bus = BusMin; Bus <= BusMax; Bus++) {
      for (Dev = 0; Dev < 32; Dev++) {
        for (Func = 0; Func < 8; Func++) {
          ZeroMem (&Ctx, sizeof (Ctx));
          Ctx.RootBridge = RootBridge;
          Ctx.Bus        = Bus;
          Ctx.Device     = Dev;
          Ctx.Function   = Func;

          VendorDevice = 0xFFFFFFFF;
          if (EFI_ERROR (CfgRead (&Ctx, PCI_OFF_VENDOR_ID, 4, &VendorDevice))) {
            break;
          }
          if ((UINT16)VendorDevice == 0xFFFF) {
            if (Func == 0) {
              break;                       /* no function 0 => no device */
            }
            continue;
          }

          /*
            Read the header type on its own first. If the full header read
            fails part way we still need this to decide whether to probe
            functions 1-7 - devices that do not decode AD[10:8] alias every
            function back to function 0 and would otherwise be listed eight
            times over.
          */
          HeaderType = 0;
          if (EFI_ERROR (CfgRead (&Ctx, PCI_OFF_HEADER_TYPE, 1, &HeaderType))) {
            HeaderType = 0;
          }
          MultiFunction = (BOOLEAN)((HeaderType & PCI_HEADER_MULTI_FUNC) != 0);

          ZeroMem (Header, sizeof (Header));
          if (EFI_ERROR (CfgReadHeader (&Ctx, Header))) {
            if ((Func == 0) && !MultiFunction) {
              break;
            }
            continue;
          }

          Results->TotalPciFunctions++;

          RecordIfDisplayDevice (
            Results,
            &Ctx,
            Header,
            RootBridge->SegmentNumber,
            Bus, Dev, Func,
            0,                                /* RomSize unknown on this path */
            ScanSourceRootBridge
            );

          if ((Func == 0) && !MultiFunction) {
            break;                            /* single-function device */
          }
        }
      }
    }
  }

  FreePool (Handles);
  return EFI_SUCCESS;
}

/* -------------------------------------------------------------------------
 * Force the PCI bus driver to bind, if nothing exposed PciIo yet
 * ---------------------------------------------------------------------- */

STATIC
VOID
ConnectAllControllers (
  VOID
  )
{
  EFI_STATUS   Status;
  EFI_HANDLE  *Handles = NULL;
  UINTN        HandleCount = 0;
  UINTN        Index;

  Status = gBS->LocateHandleBuffer (AllHandles, NULL, NULL, &HandleCount, &Handles);
  if (EFI_ERROR (Status)) {
    return;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    gBS->ConnectController (Handles[Index], NULL, NULL, TRUE);
  }

  FreePool (Handles);
}

/* -------------------------------------------------------------------------
 * Public entry point
 * ---------------------------------------------------------------------- */

EFI_STATUS
GpuScanPci (
  GPU_SCAN_RESULTS  *Results
  )
{
  if (Results == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  ZeroMem (Results, sizeof (*Results));

  ScanViaPciIo (Results);

  /*
    Some firmware only binds the PCI bus driver lazily.  If nothing at all
    turned up, ask the platform to connect its drivers - the same thing the
    boot manager does - and try once more.  This is not a bus reset.
  */
  if (Results->PciIoHandleCount == 0) {
    Results->ConnectAllAttempted = TRUE;
    ConnectAllControllers ();
    ZeroMem (Results, sizeof (*Results));
    Results->ConnectAllAttempted = TRUE;
    ScanViaPciIo (Results);
  }

  /*
    Still no display controller?  Fall back to reading configuration space
    directly through the root bridge, so a device the PCI bus driver refused
    to enumerate is still reported.  Read-only.
  */
  if (Results->Count == 0) {
    Results->UsedRootBridgeFallback = TRUE;
    ScanViaRootBridge (Results);
  }

  SortByBdf (Results);
  return EFI_SUCCESS;
}

BOOLEAN
GpuHasProbableDiscrete (
  CONST GPU_SCAN_RESULTS  *Results
  )
{
  UINTN  i;

  if (Results == NULL) {
    return FALSE;
  }
  for (i = 0; i < Results->Count; i++) {
    if (Results->Devices[i].Type == GpuTypeDiscrete) {
      return TRUE;
    }
  }
  return FALSE;
}

/* -------------------------------------------------------------------------
 * Name tables
 * ---------------------------------------------------------------------- */

CONST CHAR16 *
GpuVendorName (
  UINT16  VendorId
  )
{
  switch (VendorId) {
    case PCI_VENDOR_NVIDIA:           return L"NVIDIA";
    case PCI_VENDOR_AMD_ATI:          return L"AMD / ATI";
    case PCI_VENDOR_INTEL:            return L"Intel";
    case PCI_VENDOR_AMD:              return L"AMD";
    case PCI_VENDOR_MATROX:           return L"Matrox";
    case PCI_VENDOR_ASPEED:           return L"ASPEED";
    case PCI_VENDOR_VMWARE:           return L"VMware";
    case PCI_VENDOR_REDHAT:           return L"Red Hat / virtio";
    case PCI_VENDOR_REDHAT_QUMRANET:  return L"Red Hat (QEMU)";
    case PCI_VENDOR_S3:               return L"S3 Graphics";
    case PCI_VENDOR_CIRRUS:           return L"Cirrus Logic";
    case PCI_VENDOR_XGI:              return L"XGI";
    case PCI_VENDOR_SIS:              return L"SiS";
    case PCI_VENDOR_VIA:              return L"VIA";
    default:                          return L"Unknown vendor";
  }
}

CONST CHAR16 *
GpuClassName (
  UINT8  BaseClass,
  UINT8  SubClass
  )
{
  if (BaseClass != PCI_CLASS_DISPLAY) {
    return L"Not a display controller";
  }
  switch (SubClass) {
    case PCI_SUBCLASS_VGA:    return L"VGA Compatible Controller";
    case PCI_SUBCLASS_XGA:    return L"XGA Controller";
    case PCI_SUBCLASS_3D:     return L"3D Controller";
    case PCI_SUBCLASS_OTHER:  return L"Other Display Controller";
    default:                  return L"Display Controller (reserved subclass)";
  }
}

CONST CHAR16 *
GpuLinkSpeedName (
  UINT8  EncodedSpeed
  )
{
  switch (EncodedSpeed) {
    case 1:  return L"Gen1";
    case 2:  return L"Gen2";
    case 3:  return L"Gen3";
    case 4:  return L"Gen4";
    case 5:  return L"Gen5";
    case 6:  return L"Gen6";
    case 7:  return L"Gen7";
    default: return L"Gen?";
  }
}

CONST CHAR16 *
GpuTypeName (
  GPU_TYPE  Type
  )
{
  switch (Type) {
    case GpuTypeIntegrated:  return L"Integrated";
    case GpuTypeDiscrete:    return L"Discrete";
    default:                 return L"Unknown";
  }
}

CONST CHAR16 *
GpuPortTypeName (
  UINT8  PortType
  )
{
  switch (PortType) {
    case PCIE_PORT_TYPE_ENDPOINT:         return L"PCIe Endpoint";
    case PCIE_PORT_TYPE_LEGACY_ENDPOINT:  return L"Legacy PCIe Endpoint";
    case PCIE_PORT_TYPE_ROOT_PORT:        return L"Root Port";
    case PCIE_PORT_TYPE_UPSTREAM:         return L"Upstream Port";
    case PCIE_PORT_TYPE_DOWNSTREAM:       return L"Downstream Port";
    case PCIE_PORT_TYPE_RC_INTEGRATED:    return L"Root Complex Integrated";
    default:                              return L"Other";
  }
}
