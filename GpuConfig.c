/** @file
  GpuConfig.c - Minimal KEY=VALUE parser for gpucheck.cfg.

  Deliberately tiny: ASCII, line oriented, '#' and ';' start a comment,
  unknown keys are ignored.  Nothing here can fail in a way that prevents
  PCI enumeration from running.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "GpuConfig.h"

VOID
GpuConfigSetDefaults (
  GPUCHECK_CONFIG  *Config
  )
{
  if (Config == NULL) {
    return;
  }
  ZeroMem (Config, sizeof (*Config));
  Config->AutobootWhenFound = TRUE;
  Config->AutobootDelay     = 3;
  Config->HaltWhenMissing   = TRUE;
  Config->LogEnable         = TRUE;
}

/* ------------------------------------------------------------------------ */

STATIC
BOOLEAN
IsSpace (
  CHAR8  Ch
  )
{
  return (BOOLEAN)((Ch == ' ') || (Ch == '\t') || (Ch == '\r') || (Ch == '\n'));
}

STATIC
CHAR8
UpperA (
  CHAR8  Ch
  )
{
  if ((Ch >= 'a') && (Ch <= 'z')) {
    return (CHAR8)(Ch - 'a' + 'A');
  }
  return Ch;
}

STATIC
BOOLEAN
KeyIs (
  CONST CHAR8  *Key,
  UINTN         KeyLen,
  CONST CHAR8  *Name
  )
{
  UINTN  i;

  for (i = 0; i < KeyLen; i++) {
    if (Name[i] == '\0') {
      return FALSE;
    }
    if (UpperA (Key[i]) != Name[i]) {
      return FALSE;
    }
  }
  return (BOOLEAN)(Name[KeyLen] == '\0');
}

/**
  Parse a hexadecimal value, tolerating an optional "0x" prefix.
  Returns FALSE if there was not at least one hex digit.
**/
STATIC
BOOLEAN
ParseHex16 (
  CONST CHAR8  *Str,
  UINTN         Len,
  UINT16       *Value
  )
{
  UINTN    i     = 0;
  UINT32   Acc   = 0;
  UINTN    Count = 0;
  UINTN    Seen  = 0;

  if ((Len >= 2) && (Str[0] == '0') && (UpperA (Str[1]) == 'X')) {
    i = 2;
  }

  for ( ; i < Len; i++) {
    CHAR8  Ch = UpperA (Str[i]);
    UINT32 Digit;

    if ((Ch >= '0') && (Ch <= '9')) {
      Digit = (UINT32)(Ch - '0');
    } else if ((Ch >= 'A') && (Ch <= 'F')) {
      Digit = (UINT32)(Ch - 'A' + 10);
    } else {
      return FALSE;
    }
    Seen++;

    /*
      Count significant digits only, so a zero-padded but perfectly valid
      value such as 0x0010DE is accepted rather than silently rejected -
      rejection would leave HasExpectVendor FALSE and quietly fall back to
      the generic heuristic, defeating the whole point of pinning a GPU.
    */
    if ((Digit != 0) || (Count > 0)) {
      Acc = (Acc << 4) | Digit;
      Count++;
      if (Count > 4) {
        return FALSE;
      }
    }
  }

  if (Seen == 0) {
    return FALSE;
  }
  *Value = (UINT16)Acc;
  return TRUE;
}

STATIC
BOOLEAN
ParseDec (
  CONST CHAR8  *Str,
  UINTN         Len,
  UINTN        *Value
  )
{
  UINTN  i;
  UINTN  Acc   = 0;
  UINTN  Count = 0;

  for (i = 0; i < Len; i++) {
    if ((Str[i] < '0') || (Str[i] > '9')) {
      return FALSE;
    }
    Acc = (Acc * 10) + (UINTN)(Str[i] - '0');
    Count++;
    if (Acc > 3600) {
      return FALSE;
    }
  }
  if (Count == 0) {
    return FALSE;
  }
  *Value = Acc;
  return TRUE;
}

STATIC
BOOLEAN
ParseBool (
  CONST CHAR8  *Str,
  UINTN         Len,
  BOOLEAN      *Value
  )
{
  if ((Len == 1) && (Str[0] == '1')) {
    *Value = TRUE;
    return TRUE;
  }
  if ((Len == 1) && (Str[0] == '0')) {
    *Value = FALSE;
    return TRUE;
  }
  if (KeyIs (Str, Len, "TRUE") || KeyIs (Str, Len, "YES") || KeyIs (Str, Len, "ON")) {
    *Value = TRUE;
    return TRUE;
  }
  if (KeyIs (Str, Len, "FALSE") || KeyIs (Str, Len, "NO") || KeyIs (Str, Len, "OFF")) {
    *Value = FALSE;
    return TRUE;
  }
  return FALSE;
}

STATIC
VOID
ApplySetting (
  GPUCHECK_CONFIG  *Config,
  CONST CHAR8      *Key,
  UINTN             KeyLen,
  CONST CHAR8      *Val,
  UINTN             ValLen
  )
{
  UINT16   U16;
  UINTN    Num;
  BOOLEAN  Flag;

  if ((KeyLen == 0) || (ValLen == 0)) {
    return;
  }

  if (KeyIs (Key, KeyLen, "EXPECT_VENDOR")) {
    if (ParseHex16 (Val, ValLen, &U16)) {
      Config->ExpectVendor    = U16;
      Config->HasExpectVendor = TRUE;
    }
    return;
  }

  if (KeyIs (Key, KeyLen, "EXPECT_DEVICE")) {
    if (KeyIs (Val, ValLen, "ANY")) {
      Config->ExpectDeviceAny = TRUE;
      Config->HasExpectDevice = TRUE;
    } else if (ParseHex16 (Val, ValLen, &U16)) {
      Config->ExpectDevice    = U16;
      Config->ExpectDeviceAny = FALSE;
      Config->HasExpectDevice = TRUE;
    }
    return;
  }

  if (KeyIs (Key, KeyLen, "AUTOBOOT_WHEN_FOUND")) {
    if (ParseBool (Val, ValLen, &Flag)) {
      Config->AutobootWhenFound = Flag;
    }
    return;
  }

  if (KeyIs (Key, KeyLen, "AUTOBOOT_DELAY")) {
    if (ParseDec (Val, ValLen, &Num)) {
      /*
        Never allow zero. At zero the screen is painted and control leaves
        for Windows before a single keystroke can be sampled, so R/P/ESC
        become unreachable - and on a Mode A install that means there is no
        way back into the tool without editing the ESP from another machine.
      */
      Config->AutobootDelay = (Num == 0) ? 1 : Num;
    }
    return;
  }

  if (KeyIs (Key, KeyLen, "HALT_WHEN_MISSING")) {
    if (ParseBool (Val, ValLen, &Flag)) {
      Config->HaltWhenMissing = Flag;
    }
    return;
  }

  if (KeyIs (Key, KeyLen, "LOG_ENABLE")) {
    if (ParseBool (Val, ValLen, &Flag)) {
      Config->LogEnable = Flag;
    }
    return;
  }

  if (KeyIs (Key, KeyLen, "BOOT_PATH")) {
    UINTN  i;
    UINTN  Max = ARRAY_SIZE (Config->BootPath) - 1;

    if (ValLen > Max) {
      ValLen = Max;
    }
    for (i = 0; i < ValLen; i++) {
      Config->BootPath[i] = (CHAR16)(UINT8)(Val[i] == '/' ? '\\' : Val[i]);
    }
    Config->BootPath[ValLen] = L'\0';
    Config->HasBootPath      = (BOOLEAN)(ValLen > 0);
    return;
  }
}

STATIC
VOID
ParseBuffer (
  GPUCHECK_CONFIG  *Config,
  CONST CHAR8      *Buf,
  UINTN             Size
  )
{
  UINTN  Pos = 0;

  /*
    This file is normally authored on Windows, where Notepad writes a UTF-8
    BOM and PowerShell redirection can produce UTF-16. Neither is an error
    worth failing on, but both must be handled: an unhandled BOM silently
    corrupts the FIRST key - typically EXPECT_VENDOR, the safety-critical one -
    while every later line parses fine, so the operator has no clue the pin
    was dropped.
  */
  if ((Size >= 3) &&
      ((UINT8)Buf[0] == 0xEF) && ((UINT8)Buf[1] == 0xBB) && ((UINT8)Buf[2] == 0xBF))
  {
    Buf  += 3;
    Size -= 3;
  }

  /* UTF-16 cannot be parsed as ASCII at all; reject it rather than half-read it. */
  if ((Size >= 2) &&
      ((((UINT8)Buf[0] == 0xFF) && ((UINT8)Buf[1] == 0xFE)) ||
       (((UINT8)Buf[0] == 0xFE) && ((UINT8)Buf[1] == 0xFF))))
  {
    Config->Encoding = ConfigEncodingUtf16Rejected;
    return;
  }

  while (Pos < Size) {
    UINTN  LineStart = Pos;
    UINTN  LineEnd;
    UINTN  Eq;
    UINTN  KeyStart, KeyEnd, ValStart, ValEnd;

    while ((Pos < Size) && (Buf[Pos] != '\n')) {
      Pos++;
    }
    LineEnd = Pos;
    if (Pos < Size) {
      Pos++;                              /* consume the newline */
    }

    /* Trim, and cut the line at a comment marker. */
    for (Eq = LineStart; Eq < LineEnd; Eq++) {
      if ((Buf[Eq] == '#') || (Buf[Eq] == ';')) {
        LineEnd = Eq;
        break;
      }
    }

    /* Find '='. */
    Eq = LineStart;
    while ((Eq < LineEnd) && (Buf[Eq] != '=')) {
      Eq++;
    }
    if (Eq >= LineEnd) {
      continue;                           /* no '=' on this line */
    }

    KeyStart = LineStart;
    KeyEnd   = Eq;
    ValStart = Eq + 1;
    ValEnd   = LineEnd;

    while ((KeyStart < KeyEnd) && IsSpace (Buf[KeyStart])) {
      KeyStart++;
    }
    while ((KeyEnd > KeyStart) && IsSpace (Buf[KeyEnd - 1])) {
      KeyEnd--;
    }
    while ((ValStart < ValEnd) && IsSpace (Buf[ValStart])) {
      ValStart++;
    }
    while ((ValEnd > ValStart) && IsSpace (Buf[ValEnd - 1])) {
      ValEnd--;
    }

    ApplySetting (Config, &Buf[KeyStart], KeyEnd - KeyStart, &Buf[ValStart], ValEnd - ValStart);
  }
}

/* ------------------------------------------------------------------------ */

EFI_STATUS
GpuConfigLoad (
  EFI_HANDLE        Device,
  GPUCHECK_CONFIG  *Config
  )
{
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs   = NULL;
  EFI_FILE_PROTOCOL                *Root = NULL;
  EFI_FILE_PROTOCOL                *File = NULL;
  EFI_STATUS                        Status;
  CHAR8                            *Buffer;
  UINTN                             Size;

  if (Config == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  GpuConfigSetDefaults (Config);

  if (Device == NULL) {
    return EFI_NOT_FOUND;
  }

  Status = gBS->HandleProtocol (Device, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
  if (EFI_ERROR (Status) || (Fs == NULL)) {
    return EFI_NOT_FOUND;
  }

  Status = Fs->OpenVolume (Fs, &Root);
  if (EFI_ERROR (Status) || (Root == NULL)) {
    return EFI_NOT_FOUND;
  }

  Status = Root->Open (Root, &File, GPUCHECK_CONFIG_PATH, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status) || (File == NULL)) {
    Root->Close (Root);
    return EFI_NOT_FOUND;
  }

  Buffer = (CHAR8 *)AllocateZeroPool (GPUCHECK_CFG_MAX_SIZE);
  if (Buffer == NULL) {
    File->Close (File);
    Root->Close (Root);
    return EFI_OUT_OF_RESOURCES;
  }

  Size   = GPUCHECK_CFG_MAX_SIZE - 1;
  Status = File->Read (File, &Size, Buffer);

  File->Close (File);
  Root->Close (Root);

  if (!EFI_ERROR (Status) && (Size > 0)) {
    Buffer[Size]   = '\0';
    Config->Loaded = TRUE;
    ParseBuffer (Config, Buffer, Size);
  }

  FreePool (Buffer);
  return Config->Loaded ? EFI_SUCCESS : EFI_NOT_FOUND;
}
