/** @file
  StandaloneUefi.c - EDK II-compatible shims for the bare-MSVC build.

  Provides gST/gBS/gRT, a small Print(), pool/string/memory helpers and
  device-path construction, all under the same names EDK II uses, so the
  GpuCheck sources are identical in both build modes.

  Not compiled when GPUCHECK_EDK2 is defined.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "StandaloneUefi.h"

/* -------------------------------------------------------------------------
 * Compiler intrinsics.  MSVC emits calls to these for structure assignment
 * and array initialisation even in a freestanding build, so we must supply
 * them ourselves - there is no CRT linked in.
 * ---------------------------------------------------------------------- */

void *memset (void *Dest, int Ch, unsigned __int64 Count);
void *memcpy (void *Dest, const void *Src, unsigned __int64 Count);
int   memcmp (const void *A, const void *B, unsigned __int64 Count);

#pragma function(memset)
void *memset (void *Dest, int Ch, unsigned __int64 Count)
{
  unsigned char *D = (unsigned char *)Dest;
  while (Count-- != 0) {
    *D++ = (unsigned char)Ch;
  }
  return Dest;
}

#pragma function(memcpy)
void *memcpy (void *Dest, const void *Src, unsigned __int64 Count)
{
  unsigned char       *D = (unsigned char *)Dest;
  const unsigned char *S = (const unsigned char *)Src;
  while (Count-- != 0) {
    *D++ = *S++;
  }
  return Dest;
}

#pragma function(memcmp)
int memcmp (const void *A, const void *B, unsigned __int64 Count)
{
  const unsigned char *P = (const unsigned char *)A;
  const unsigned char *Q = (const unsigned char *)B;
  while (Count-- != 0) {
    if (*P != *Q) {
      return (int)*P - (int)*Q;
    }
    P++;
    Q++;
  }
  return 0;
}

/* -------------------------------------------------------------------------
 * Protocol GUIDs
 * ---------------------------------------------------------------------- */

EFI_GUID  gEfiPciIoProtocolGuid =
  { 0x4CF5B200, 0x68B8, 0x4CA5, { 0x9E, 0xEC, 0xB2, 0x3E, 0x3F, 0x50, 0x02, 0x9A } };

EFI_GUID  gEfiPciRootBridgeIoProtocolGuid =
  { 0x2F707EBB, 0x4A1A, 0x11D4, { 0x9A, 0x38, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D } };

EFI_GUID  gEfiLoadedImageProtocolGuid =
  { 0x5B1B31A1, 0x9562, 0x11D2, { 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } };

EFI_GUID  gEfiDevicePathProtocolGuid =
  { 0x09576E91, 0x6D3F, 0x11D2, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } };

EFI_GUID  gEfiSimpleFileSystemProtocolGuid =
  { 0x964E5B22, 0x6459, 0x11D2, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } };

EFI_GUID  gEfiFileInfoGuid =
  { 0x09576E92, 0x6D3F, 0x11D2, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } };

EFI_GUID  gEfiGlobalVariableGuid =
  { 0x8BE4DF61, 0x93CA, 0x11D2, { 0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C } };

/* -------------------------------------------------------------------------
 * Globals
 * ---------------------------------------------------------------------- */

EFI_SYSTEM_TABLE     *gST          = NULL;
EFI_BOOT_SERVICES    *gBS          = NULL;
EFI_RUNTIME_SERVICES *gRT          = NULL;
EFI_HANDLE            gImageHandle = NULL;

VOID
StandaloneUefiInit (
  EFI_HANDLE         ImageHandle,
  EFI_SYSTEM_TABLE  *SystemTable
  )
{
  gImageHandle = ImageHandle;
  gST          = SystemTable;
  gBS          = SystemTable->BootServices;
  gRT          = SystemTable->RuntimeServices;
}

/* -------------------------------------------------------------------------
 * Memory helpers
 * ---------------------------------------------------------------------- */

VOID *
AllocatePool (
  UINTN  Size
  )
{
  VOID        *Buffer = NULL;
  EFI_STATUS   Status;

  if (Size == 0) {
    return NULL;
  }
  Status = gBS->AllocatePool (EfiLoaderData, Size, &Buffer);
  if (EFI_ERROR (Status)) {
    return NULL;
  }
  return Buffer;
}

VOID *
AllocateZeroPool (
  UINTN  Size
  )
{
  VOID *Buffer = AllocatePool (Size);

  if (Buffer != NULL) {
    memset (Buffer, 0, Size);
  }
  return Buffer;
}

VOID
FreePool (
  VOID  *Buffer
  )
{
  if (Buffer != NULL) {
    gBS->FreePool (Buffer);
  }
}

VOID
CopyMem (
  VOID        *Dest,
  CONST VOID  *Src,
  UINTN        Length
  )
{
  memcpy (Dest, Src, Length);
}

VOID
ZeroMem (
  VOID   *Buffer,
  UINTN   Length
  )
{
  memset (Buffer, 0, Length);
}

VOID
SetMem (
  VOID   *Buffer,
  UINTN   Length,
  UINT8   Value
  )
{
  memset (Buffer, Value, Length);
}

INTN
CompareMem (
  CONST VOID  *A,
  CONST VOID  *B,
  UINTN        Length
  )
{
  return (INTN)memcmp (A, B, Length);
}

/* -------------------------------------------------------------------------
 * String helpers
 * ---------------------------------------------------------------------- */

UINTN
StrLen (
  CONST CHAR16  *S
  )
{
  UINTN  Len = 0;

  if (S == NULL) {
    return 0;
  }
  while (S[Len] != L'\0') {
    Len++;
  }
  return Len;
}

UINTN
AsciiStrLen (
  CONST CHAR8  *S
  )
{
  UINTN  Len = 0;

  if (S == NULL) {
    return 0;
  }
  while (S[Len] != '\0') {
    Len++;
  }
  return Len;
}

INTN
StrCmp (
  CONST CHAR16  *A,
  CONST CHAR16  *B
  )
{
  while ((*A != L'\0') && (*A == *B)) {
    A++;
    B++;
  }
  return (INTN)*A - (INTN)*B;
}

VOID
StrCpy16 (
  CHAR16        *Dest,
  UINTN          DestMax,
  CONST CHAR16  *Src
  )
{
  UINTN  i = 0;

  if ((Dest == NULL) || (DestMax == 0)) {
    return;
  }
  if (Src != NULL) {
    while ((i + 1 < DestMax) && (Src[i] != L'\0')) {
      Dest[i] = Src[i];
      i++;
    }
  }
  Dest[i] = L'\0';
}

VOID
StrCat16 (
  CHAR16        *Dest,
  UINTN          DestMax,
  CONST CHAR16  *Src
  )
{
  UINTN  Len;

  if ((Dest == NULL) || (DestMax == 0)) {
    return;
  }
  Len = StrLen (Dest);
  if (Len >= DestMax) {
    return;
  }
  StrCpy16 (Dest + Len, DestMax - Len, Src);
}

/* -------------------------------------------------------------------------
 * Print
 *
 * Supported (EDK II-compatible subset):
 *   %s  CHAR16 *      %a  CHAR8 *     %c  CHAR16
 *   %d  INTN          %u  UINTN       %x / %X  hex (uppercase, EDK II style)
 *   %%  literal '%'
 * Flags: '-' left align, '0' zero pad, decimal field width.
 * All integer arguments are consumed as UINTN/INTN slots, matching EDK II.
 * ---------------------------------------------------------------------- */

#define PRINT_BUFFER_CHARS  1024

STATIC CHAR16  mPrintBuffer[PRINT_BUFFER_CHARS];

STATIC
VOID
PbPut (
  CHAR16  *Buf,
  UINTN   *Pos,
  CHAR16   Ch
  )
{
  if (*Pos < (PRINT_BUFFER_CHARS - 1)) {
    Buf[(*Pos)++] = Ch;
  }
}

STATIC
UINTN
UintnToStr (
  UINT64   Value,
  UINTN    Radix,
  BOOLEAN  Upper,
  CHAR16  *Out,
  UINTN    OutMax
  )
{
  CHAR16        Tmp[24];
  UINTN         Len = 0;
  UINTN         i;
  CONST CHAR16 *DigitsUpper = L"0123456789ABCDEF";
  CONST CHAR16 *DigitsLower = L"0123456789abcdef";
  CONST CHAR16 *Digits      = Upper ? DigitsUpper : DigitsLower;

  if (Value == 0) {
    Tmp[Len++] = L'0';
  } else {
    while ((Value != 0) && (Len < ARRAY_SIZE (Tmp))) {
      Tmp[Len++] = Digits[Value % Radix];
      Value /= Radix;
    }
  }

  for (i = 0; (i < Len) && (i < OutMax); i++) {
    Out[i] = Tmp[Len - 1 - i];
  }
  return Len;
}

UINTN
VPrint (
  CONST CHAR16  *Format,
  va_list        Marker
  )
{
  UINTN    Pos = 0;
  CHAR16   Num[32];
  UINTN    NumLen;
  BOOLEAN  LeftAlign;
  BOOLEAN  ZeroPad;
  UINTN    Width;
  UINTN    i;

  if ((Format == NULL) || (gST == NULL) || (gST->ConOut == NULL)) {
    return 0;
  }

  while (*Format != L'\0') {
    if (*Format != L'%') {
      if (*Format == L'\n') {
        PbPut (mPrintBuffer, &Pos, L'\r');
      }
      PbPut (mPrintBuffer, &Pos, *Format);
      Format++;
      continue;
    }

    Format++;                       /* skip '%' */
    if (*Format == L'\0') {
      break;
    }
    if (*Format == L'%') {
      PbPut (mPrintBuffer, &Pos, L'%');
      Format++;
      continue;
    }

    LeftAlign = FALSE;
    ZeroPad   = FALSE;
    Width     = 0;

    while ((*Format == L'-') || (*Format == L'0')) {
      if (*Format == L'-') {
        LeftAlign = TRUE;
      } else {
        ZeroPad = TRUE;
      }
      Format++;
    }
    while ((*Format >= L'0') && (*Format <= L'9')) {
      Width = (Width * 10) + (UINTN)(*Format - L'0');
      Format++;
    }

    switch (*Format) {
      case L's':
      {
        CHAR16  *Str = va_arg (Marker, CHAR16 *);
        UINTN    Len;

        if (Str == NULL) {
          Str = L"<null>";
        }
        Len = StrLen (Str);
        if (!LeftAlign) {
          for (i = Len; i < Width; i++) {
            PbPut (mPrintBuffer, &Pos, L' ');
          }
        }
        while (*Str != L'\0') {
          PbPut (mPrintBuffer, &Pos, *Str++);
        }
        if (LeftAlign) {
          for (i = Len; i < Width; i++) {
            PbPut (mPrintBuffer, &Pos, L' ');
          }
        }
        break;
      }

      case L'a':
      {
        CHAR8  *Str = va_arg (Marker, CHAR8 *);
        UINTN   Len;

        if (Str == NULL) {
          Str = "<null>";
        }
        Len = AsciiStrLen (Str);
        if (!LeftAlign) {
          for (i = Len; i < Width; i++) {
            PbPut (mPrintBuffer, &Pos, L' ');
          }
        }
        while (*Str != '\0') {
          PbPut (mPrintBuffer, &Pos, (CHAR16)(UINT8)*Str++);
        }
        if (LeftAlign) {
          for (i = Len; i < Width; i++) {
            PbPut (mPrintBuffer, &Pos, L' ');
          }
        }
        break;
      }

      case L'c':
      {
        CHAR16  Ch = (CHAR16)va_arg (Marker, UINTN);
        PbPut (mPrintBuffer, &Pos, Ch);
        break;
      }

      case L'd':
      {
        INTN     Value = (INTN)va_arg (Marker, INTN);
        BOOLEAN  Neg   = FALSE;
        UINT64   Mag;

        if (Value < 0) {
          Neg = TRUE;
          Mag = (UINT64)(-(Value + 1)) + 1;   /* safe for INTN_MIN */
        } else {
          Mag = (UINT64)Value;
        }
        NumLen = UintnToStr (Mag, 10, TRUE, Num, ARRAY_SIZE (Num));
        if (Neg) {
          NumLen++;
        }
        if (!LeftAlign) {
          for (i = NumLen; i < Width; i++) {
            PbPut (mPrintBuffer, &Pos, ZeroPad ? L'0' : L' ');
          }
        }
        if (Neg) {
          PbPut (mPrintBuffer, &Pos, L'-');
          NumLen--;
        }
        for (i = 0; i < NumLen; i++) {
          PbPut (mPrintBuffer, &Pos, Num[i]);
        }
        if (LeftAlign) {
          for (i = NumLen + (Neg ? 1 : 0); i < Width; i++) {
            PbPut (mPrintBuffer, &Pos, L' ');
          }
        }
        break;
      }

      case L'u':
      case L'x':
      case L'X':
      {
        UINT64   Value = (UINT64)va_arg (Marker, UINTN);
        UINTN    Radix = (*Format == L'u') ? 10 : 16;
        BOOLEAN  Upper = (*Format != L'x');

        NumLen = UintnToStr (Value, Radix, Upper, Num, ARRAY_SIZE (Num));
        if (!LeftAlign) {
          for (i = NumLen; i < Width; i++) {
            PbPut (mPrintBuffer, &Pos, ZeroPad ? L'0' : L' ');
          }
        }
        for (i = 0; i < NumLen; i++) {
          PbPut (mPrintBuffer, &Pos, Num[i]);
        }
        if (LeftAlign) {
          for (i = NumLen; i < Width; i++) {
            PbPut (mPrintBuffer, &Pos, L' ');
          }
        }
        break;
      }

      default:
        PbPut (mPrintBuffer, &Pos, L'%');
        PbPut (mPrintBuffer, &Pos, *Format);
        break;
    }

    Format++;
  }

  mPrintBuffer[Pos] = L'\0';
  gST->ConOut->OutputString (gST->ConOut, mPrintBuffer);
  return Pos;
}

UINTN
Print (
  CONST CHAR16  *Format,
  ...
  )
{
  va_list  Marker;
  UINTN    Count;

  va_start (Marker, Format);
  Count = VPrint (Format, Marker);
  va_end (Marker);
  return Count;
}

/* -------------------------------------------------------------------------
 * Device path helpers
 * ---------------------------------------------------------------------- */

EFI_DEVICE_PATH_PROTOCOL *
DevicePathFromHandle (
  EFI_HANDLE  Handle
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *DevPath = NULL;

  if (Handle == NULL) {
    return NULL;
  }
  if (EFI_ERROR (gBS->HandleProtocol (Handle, &gEfiDevicePathProtocolGuid, (VOID **)&DevPath))) {
    return NULL;
  }
  return DevPath;
}

/**
  Build <device path of Device> + MEDIA_FILEPATH(FileName) + END.
  Caller frees with FreePool().  Returns NULL on failure.
**/
EFI_DEVICE_PATH_PROTOCOL *
FileDevicePath (
  EFI_HANDLE     Device,
  CONST CHAR16  *FileName
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Base;
  EFI_DEVICE_PATH_PROTOCOL  *Node;
  EFI_DEVICE_PATH_PROTOCOL  *Result;
  UINT8                     *Ptr;
  UINTN                      BaseLen   = 0;
  UINTN                      NameBytes;
  UINTN                      FileNodeLen;
  UINTN                      TotalLen;

  if (FileName == NULL) {
    return NULL;
  }

  NameBytes   = (StrLen (FileName) + 1) * sizeof (CHAR16);
  FileNodeLen = sizeof (EFI_DEVICE_PATH_PROTOCOL) + NameBytes;

  /* A device path node length field is 16 bits. */
  if (FileNodeLen > 0xFFFF) {
    return NULL;
  }

  Base = DevicePathFromHandle (Device);
  if (Base != NULL) {
    UINTN  Guard;

    Node = Base;
    for (Guard = 0; (Guard < 1024) && !IsDevicePathEnd (Node); Guard++) {
      UINTN  NodeLen = DevicePathNodeLength (Node);

      /*
        A node whose length is smaller than the node header is malformed.
        Advancing by it would not move the pointer forward, and this walk
        runs with the watchdog already disabled - the machine would hang
        with no console output and no way out. Treat the path as unusable.
      */
      if (NodeLen < sizeof (EFI_DEVICE_PATH_PROTOCOL)) {
        return NULL;
      }
      Node = (EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)Node + NodeLen);
    }

    if (!IsDevicePathEnd (Node)) {
      return NULL;                      /* cyclic or absurdly long path */
    }
    BaseLen = (UINTN)((UINT8 *)Node - (UINT8 *)Base);
  }

  TotalLen = BaseLen + FileNodeLen + sizeof (EFI_DEVICE_PATH_PROTOCOL);

  Result = (EFI_DEVICE_PATH_PROTOCOL *)AllocateZeroPool (TotalLen);
  if (Result == NULL) {
    return NULL;
  }

  Ptr = (UINT8 *)Result;
  if (BaseLen != 0) {
    CopyMem (Ptr, Base, BaseLen);
    Ptr += BaseLen;
  }

  /* File path node */
  ((EFI_DEVICE_PATH_PROTOCOL *)Ptr)->Type    = MEDIA_DEVICE_PATH;
  ((EFI_DEVICE_PATH_PROTOCOL *)Ptr)->SubType = MEDIA_FILEPATH_DP;
  SetDevicePathNodeLength (Ptr, FileNodeLen);
  CopyMem (Ptr + sizeof (EFI_DEVICE_PATH_PROTOCOL), FileName, NameBytes);
  Ptr += FileNodeLen;

  /* End node */
  ((EFI_DEVICE_PATH_PROTOCOL *)Ptr)->Type    = END_DEVICE_PATH_TYPE;
  ((EFI_DEVICE_PATH_PROTOCOL *)Ptr)->SubType = END_ENTIRE_DEVICE_PATH_SUBTYPE;
  SetDevicePathNodeLength (Ptr, sizeof (EFI_DEVICE_PATH_PROTOCOL));

  return Result;
}
