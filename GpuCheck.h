/** @file
  GpuCheck.h - Environment umbrella.

  Selects between a real EDK II environment (-D GPUCHECK_EDK2) and the
  self-contained MSVC environment in Compat/.  Every other source file in
  this project includes only this header, so both builds compile the exact
  same .c files.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef GPUCHECK_H_
#define GPUCHECK_H_

#ifdef GPUCHECK_EDK2

  #include <Uefi.h>
  #include <Library/UefiLib.h>
  #include <Library/UefiBootServicesTableLib.h>
  #include <Library/UefiRuntimeServicesTableLib.h>
  #include <Library/UefiApplicationEntryPoint.h>
  #include <Library/BaseLib.h>
  #include <Library/BaseMemoryLib.h>
  #include <Library/MemoryAllocationLib.h>
  #include <Library/PrintLib.h>
  #include <Library/DevicePathLib.h>
  #include <Protocol/PciIo.h>
  #include <Protocol/PciRootBridgeIo.h>
  #include <Protocol/LoadedImage.h>
  #include <Protocol/SimpleFileSystem.h>
  #include <Protocol/DevicePath.h>
  #include <Guid/FileInfo.h>

  #define StrCpy16(Dest, DestMax, Src)  StrCpyS ((Dest), (DestMax), (Src))
  #define StrCat16(Dest, DestMax, Src)  StrCatS ((Dest), (DestMax), (Src))

  #ifndef EFI_PCI_ADDRESS
    #define EFI_PCI_ADDRESS(bus, dev, func, reg)             \
      ((UINT64)((((UINT64)(UINT8)(bus))  << 24) |            \
                (((UINT64)(UINT8)(dev))  << 16) |            \
                (((UINT64)(UINT8)(func)) <<  8) |            \
                (((UINT64)(UINT32)(reg)) < 256               \
                   ? ((UINT64)(UINT32)(reg))                 \
                   : (((UINT64)(UINT32)(reg)) << 32))))
  #endif

#else

  #include "Compat/StandaloneUefi.h"

#endif

#ifndef ARRAY_SIZE
  #define ARRAY_SIZE(a)  (sizeof (a) / sizeof ((a)[0]))
#endif

#define GPUCHECK_VERSION  L"1.0.0"

/**
  A literal that is guaranteed to appear, as UTF-16, inside every GpuCheck
  binary - it is part of the banner GpuCheck.c prints.

  Three separate things depend on it:
    * WindowsBoot.c refuses to chainload any image containing it, which is
      what makes a Mode A self-boot loop impossible even when device handles
      or file paths cannot be compared reliably;
    * install-gpucheck.ps1 uses it to tell GpuCheck apart from the genuine
      Windows Boot Manager before overwriting anything;
    * restore-gpucheck.ps1 and inspect-esp.ps1 use it to report ESP state.

  If you change this string, change it in the scripts too.
**/
#define GPUCHECK_IMAGE_MARKER  L"GPU PCIe DIAGNOSTIC"

#endif /* GPUCHECK_H_ */
