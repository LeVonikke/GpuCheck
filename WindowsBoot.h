/** @file
  WindowsBoot.h - Locate and chainload the preserved Windows Boot Manager.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef GPUCHECK_WINDOWSBOOT_H_
#define GPUCHECK_WINDOWSBOOT_H_

#include "GpuCheck.h"

#define WINBOOT_MAX_PATH  256

/* The path GpuCheck's installer preserves the genuine loader to. */
#define WINBOOT_PREFERRED_PATH  L"\\EFI\\Microsoft\\Boot\\bootmgfw-original.efi"

typedef struct {
  EFI_HANDLE  Device;                    /* filesystem handle holding it     */
  CHAR16      Path[WINBOOT_MAX_PATH];    /* path within that filesystem      */
  BOOLEAN     Found;
  BOOLEAN     OnOwnDevice;               /* same volume GpuCheck booted from */
} WINBOOT_TARGET;

/**
  Return the device handle GpuCheck itself was loaded from, or NULL.
**/
EFI_HANDLE
WinBootOwnDeviceHandle (
  VOID
  );

/**
  Copy GpuCheck's own file path (e.g. "\EFI\Microsoft\Boot\bootmgfw.efi")
  into Buffer.  Returns FALSE if it could not be determined.
**/
BOOLEAN
WinBootOwnFilePath (
  CHAR16  *Buffer,
  UINTN    BufferChars
  );

/**
  Test whether Path exists on the filesystem behind Device.
**/
BOOLEAN
WinBootFileExists (
  EFI_HANDLE     Device,
  CONST CHAR16  *Path
  );

/**
  Search for a usable Windows Boot Manager.

  Looks on GpuCheck's own volume first, then on every other volume exposing
  a Simple File System.  The stock "\EFI\Microsoft\Boot\bootmgfw.efi" is only
  ever accepted when it is provably not GpuCheck itself, so an installation
  in Mode A can never chainload into an endless loop.
**/
EFI_STATUS
WinBootFindLoader (
  CONST CHAR16    *PreferredPath OPTIONAL,
  WINBOOT_TARGET  *Target
  );

/**
  LoadImage() + StartImage() the given loader.  Only returns on failure, or
  if the loaded image itself returned control.
**/
EFI_STATUS
WinBootChainload (
  CONST WINBOOT_TARGET  *Target
  );

/**
  The candidate list, exposed so the UI can show exactly what was searched.
**/
CONST CHAR16 *
WinBootCandidatePath (
  UINTN  Index
  );

UINTN
WinBootCandidateCount (
  VOID
  );

#endif /* GPUCHECK_WINDOWSBOOT_H_ */
