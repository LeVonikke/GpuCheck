/** @file
  GpuLog.c - Best-effort scan log writer.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "GpuLog.h"

#define LOG_BUFFER_SIZE  4096

STATIC CHAR8  mLogBuffer[LOG_BUFFER_SIZE];
STATIC UINTN  mLogPos;

/* ------------------------------------------------------------------------ */

STATIC
VOID
LogChar (
  CHAR8  Ch
  )
{
  if (mLogPos < (LOG_BUFFER_SIZE - 1)) {
    mLogBuffer[mLogPos++] = Ch;
  }
}

STATIC
VOID
LogStr (
  CONST CHAR8  *Str
  )
{
  if (Str == NULL) {
    return;
  }
  while (*Str != '\0') {
    LogChar (*Str++);
  }
}

STATIC
VOID
LogStr16 (
  CONST CHAR16  *Str
  )
{
  if (Str == NULL) {
    return;
  }
  while (*Str != L'\0') {
    /* The log is ASCII; anything outside it becomes '?'. */
    LogChar ((*Str < 0x80) ? (CHAR8)*Str : '?');
    Str++;
  }
}

STATIC
VOID
LogHex (
  UINT64  Value,
  UINTN   Digits
  )
{
  CONST CHAR8  *Hex = "0123456789ABCDEF";
  UINTN         i;

  if (Digits == 0) {
    Digits = 1;
  }
  if (Digits > 16) {
    Digits = 16;
  }
  for (i = Digits; i > 0; i--) {
    LogChar (Hex[(Value >> ((i - 1) * 4)) & 0xF]);
  }
}

STATIC
VOID
LogDec (
  UINT64  Value
  )
{
  CHAR8  Tmp[24];
  UINTN  Len = 0;

  if (Value == 0) {
    LogChar ('0');
    return;
  }
  while ((Value != 0) && (Len < sizeof (Tmp))) {
    Tmp[Len++] = (CHAR8)('0' + (Value % 10));
    Value     /= 10;
  }
  while (Len > 0) {
    LogChar (Tmp[--Len]);
  }
}

STATIC
VOID
LogDec2 (
  UINTN  Value
  )
{
  LogChar ((CHAR8)('0' + ((Value / 10) % 10)));
  LogChar ((CHAR8)('0' + (Value % 10)));
}

/* ------------------------------------------------------------------------ */

STATIC
VOID
BuildLog (
  CONST GPU_SCAN_RESULTS  *Results
  )
{
  EFI_TIME  Now;
  UINTN     i;

  mLogPos = 0;

  ZeroMem (&Now, sizeof (Now));
  if ((gRT != NULL) && !EFI_ERROR (gRT->GetTime (&Now, NULL))) {
    LogDec (Now.Year);
    LogChar ('-');
    LogDec2 (Now.Month);
    LogChar ('-');
    LogDec2 (Now.Day);
    LogChar (' ');
    LogDec2 (Now.Hour);
    LogChar (':');
    LogDec2 (Now.Minute);
    LogChar (':');
    LogDec2 (Now.Second);
  } else {
    LogStr ("<no rtc>");
  }
  LogStr ("\r\n");

  LogStr ("GpuCheck ");
  LogStr16 (GPUCHECK_VERSION);
  LogStr ("\r\n");

  LogStr ("pci_functions_seen=");
  LogDec (Results->TotalPciFunctions);
  LogStr ("\r\n");

  LogStr ("display_controllers=");
  LogDec (Results->Count);
  LogStr ("\r\n");

  if (Results->UsedRootBridgeFallback) {
    LogStr ("scan_source=root_bridge_fallback\r\n");
  } else {
    LogStr ("scan_source=pci_io\r\n");
  }
  LogStr ("\r\n");

  if (Results->Count == 0) {
    LogStr ("NO PCI DISPLAY CONTROLLERS DETECTED\r\n");
    return;
  }

  for (i = 0; i < Results->Count; i++) {
    CONST GPU_DEVICE_INFO  *D = &Results->Devices[i];

    LogHex (D->Segment, 4);
    LogChar (':');
    LogHex (D->Bus, 2);
    LogChar (':');
    LogHex (D->Device, 2);
    LogChar ('.');
    LogHex (D->Function, 1);
    LogStr ("\r\n");

    LogHex (D->VendorId, 4);
    LogChar (':');
    LogHex (D->DeviceId, 4);
    LogStr ("\r\n");

    LogHex (D->BaseClass, 2);
    LogChar (':');
    LogHex (D->SubClass, 2);
    LogStr ("\r\n");

    LogStr ("type=");
    LogStr16 (GpuTypeName (D->Type));
    LogStr ("\r\n");

    if (D->PcieCapValid && (D->CurrentLinkSpeed != 0)) {
      LogStr ("PCIe ");
      LogStr16 (GpuLinkSpeedName (D->CurrentLinkSpeed));
      LogStr (" x");
      LogDec (D->CurrentLinkWidth);
      LogStr (" (max ");
      LogStr16 (GpuLinkSpeedName (D->MaxLinkSpeed));
      LogStr (" x");
      LogDec (D->MaxLinkWidth);
      LogStr (")\r\n");
    }

    LogStr ("\r\n");
  }
}

/* ------------------------------------------------------------------------ */

EFI_STATUS
GpuLogWriteScan (
  EFI_HANDLE               Device,
  CONST GPU_SCAN_RESULTS  *Results
  )
{
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs   = NULL;
  EFI_FILE_PROTOCOL                *Root = NULL;
  EFI_FILE_PROTOCOL                *Dir  = NULL;
  EFI_FILE_PROTOCOL                *File = NULL;
  EFI_STATUS                        Status;
  UINTN                             Size;

  if ((Device == NULL) || (Results == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  BuildLog (Results);
  if (mLogPos == 0) {
    return EFI_ABORTED;
  }

  Status = gBS->HandleProtocol (Device, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
  if (EFI_ERROR (Status) || (Fs == NULL)) {
    return EFI_NOT_FOUND;
  }

  Status = Fs->OpenVolume (Fs, &Root);
  if (EFI_ERROR (Status) || (Root == NULL)) {
    return EFI_NOT_FOUND;
  }

  /* Make sure \EFI\GPUCHECK exists; ignore "already there". */
  Status = Root->Open (
                   Root,
                   &Dir,
                   GPUCHECK_LOG_DIR,
                   EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                   EFI_FILE_DIRECTORY
                   );
  if (!EFI_ERROR (Status) && (Dir != NULL)) {
    Dir->Close (Dir);
  }

  /* Replace any previous scan rather than appending to it. */
  Status = Root->Open (Root, &File, GPUCHECK_LOG_PATH, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
  if (!EFI_ERROR (Status) && (File != NULL)) {
    File->Delete (File);                 /* Delete() also closes the handle */
    File = NULL;
  }

  Status = Root->Open (
                   Root,
                   &File,
                   GPUCHECK_LOG_PATH,
                   EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                   0
                   );
  if (EFI_ERROR (Status) || (File == NULL)) {
    Root->Close (Root);
    return Status;
  }

  Size   = mLogPos;
  Status = File->Write (File, &Size, mLogBuffer);
  File->Flush (File);
  File->Close (File);
  Root->Close (Root);

  return Status;
}
