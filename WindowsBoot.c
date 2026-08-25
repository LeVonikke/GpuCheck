/** @file
  WindowsBoot.c - Locate and chainload the preserved Windows Boot Manager.

  GpuCheck never parses NTFS and never touches the Windows partition.  It
  hands a device path to the firmware's own LoadImage()/StartImage(), which
  is the only supported way to transfer control to another UEFI loader.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "WindowsBoot.h"

/*
  Search order.  The first entry is what the installer creates; the rest are
  tolerated so a manually prepared disk still works.

  "\EFI\Microsoft\Boot\bootmgfw.efi" is intentionally LAST and is guarded:
  in Mode A that path is GpuCheck itself, and loading it would recurse
  forever.  See IsOurself().
*/
STATIC CONST CHAR16  *mCandidatePaths[] = {
  L"\\EFI\\Microsoft\\Boot\\bootmgfw-original.efi",
  L"\\EFI\\Microsoft\\Boot\\bootmgfw_original.efi",
  L"\\EFI\\GPUCHECK\\bootmgfw-original.efi",
  L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi.orig",
  L"\\EFI\\BOOT\\BOOTX64-ORIGINAL.EFI",
  L"\\EFI\\BOOT\\BOOTX64-BACKUP.EFI",
  L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi"
};

UINTN
WinBootCandidateCount (
  VOID
  )
{
  return ARRAY_SIZE (mCandidatePaths);
}

CONST CHAR16 *
WinBootCandidatePath (
  UINTN  Index
  )
{
  if (Index >= ARRAY_SIZE (mCandidatePaths)) {
    return NULL;
  }
  return mCandidatePaths[Index];
}

/* ------------------------------------------------------------------------ */

STATIC
CHAR16
ToUpper16 (
  CHAR16  Ch
  )
{
  if ((Ch >= L'a') && (Ch <= L'z')) {
    return (CHAR16)(Ch - L'a' + L'A');
  }
  return Ch;
}

/**
  Case-insensitive CHAR16 compare that also treats '/' and '\' as equal,
  since firmware and installers are inconsistent about the separator.
**/
STATIC
BOOLEAN
PathEqual (
  CONST CHAR16  *A,
  CONST CHAR16  *B
  )
{
  if ((A == NULL) || (B == NULL)) {
    return FALSE;
  }
  for ( ; ; A++, B++) {
    CHAR16  Ca = ToUpper16 (*A);
    CHAR16  Cb = ToUpper16 (*B);

    if (Ca == L'/') {
      Ca = L'\\';
    }
    if (Cb == L'/') {
      Cb = L'\\';
    }
    if (Ca != Cb) {
      return FALSE;
    }
    if (Ca == L'\0') {
      return TRUE;
    }
  }
}

/* ------------------------------------------------------------------------ */

STATIC
EFI_LOADED_IMAGE_PROTOCOL *
GetOwnLoadedImage (
  VOID
  )
{
  EFI_LOADED_IMAGE_PROTOCOL  *Loaded = NULL;

  if (gImageHandle == NULL) {
    return NULL;
  }
  if (EFI_ERROR (gBS->HandleProtocol (gImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded))) {
    return NULL;
  }
  return Loaded;
}

EFI_HANDLE
WinBootOwnDeviceHandle (
  VOID
  )
{
  EFI_LOADED_IMAGE_PROTOCOL  *Loaded = GetOwnLoadedImage ();

  if (Loaded == NULL) {
    return NULL;
  }
  return Loaded->DeviceHandle;
}

/**
  Reconstruct our own file path by concatenating every MEDIA_FILEPATH node in
  LoadedImage->FilePath.

  Firmware is free to split a file path across several MEDIA_FILEPATH nodes,
  e.g. {"\EFI\Microsoft\Boot", "bootmgfw.efi"}, and is not required to embed
  the separator. Joining the nodes blindly would yield
  "\EFI\Microsoft\Bootbootmgfw.efi", which then fails to match anything and
  silently disarms the self-boot guard - so insert the separator explicitly.
**/
BOOLEAN
WinBootOwnFilePath (
  CHAR16  *Buffer,
  UINTN    BufferChars
  )
{
  EFI_LOADED_IMAGE_PROTOCOL  *Loaded;
  EFI_DEVICE_PATH_PROTOCOL   *Node;
  BOOLEAN                     GotAny = FALSE;
  UINTN                       Guard;
  UINTN                       Len;

  if ((Buffer == NULL) || (BufferChars == 0)) {
    return FALSE;
  }
  Buffer[0] = L'\0';

  Loaded = GetOwnLoadedImage ();
  if ((Loaded == NULL) || (Loaded->FilePath == NULL)) {
    return FALSE;
  }

  Node = Loaded->FilePath;
  for (Guard = 0; (Guard < 1024) && !IsDevicePathEnd (Node); Guard++) {
    UINTN  NodeLen = DevicePathNodeLength (Node);

    /* A node shorter than its own header is malformed; advancing would
       either not move or move backwards, hanging the machine. */
    if (NodeLen < sizeof (EFI_DEVICE_PATH_PROTOCOL)) {
      break;
    }

    if ((DevicePathType (Node) == MEDIA_DEVICE_PATH) &&
        (DevicePathSubType (Node) == MEDIA_FILEPATH_DP))
    {
      CONST CHAR16  *Segment = (CONST CHAR16 *)((UINT8 *)Node + sizeof (EFI_DEVICE_PATH_PROTOCOL));

      Len = StrLen (Buffer);
      if ((Len > 0) && (Buffer[Len - 1] != L'\\') && (Segment[0] != L'\\')) {
        StrCat16 (Buffer, BufferChars, L"\\");
      } else if ((Len > 0) && (Buffer[Len - 1] == L'\\') && (Segment[0] == L'\\')) {
        Segment++;                     /* avoid a doubled separator */
      }

      StrCat16 (Buffer, BufferChars, Segment);
      GotAny = TRUE;
    }

    Node = (EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)Node + NodeLen);
  }

  /* Normalise to a rooted path so comparisons against our candidate list work. */
  if (GotAny && (Buffer[0] != L'\\')) {
    STATIC CHAR16  Rooted[WINBOOT_MAX_PATH];

    Rooted[0] = L'\0';
    StrCat16 (Rooted, ARRAY_SIZE (Rooted), L"\\");
    StrCat16 (Rooted, ARRAY_SIZE (Rooted), Buffer);
    StrCpy16 (Buffer, BufferChars, Rooted);
  }

  return GotAny;
}

/* ------------------------------------------------------------------------ */

#define GPUCHECK_SELFSCAN_BYTES  (256 * 1024)

/**
  Read the start of a candidate image and look for GpuCheck's own marker.

  This is the authoritative self-boot-loop guard. Comparing device handles or
  reconstructed file paths is best-effort - firmware may hand us a whole-disk
  handle instead of the partition handle, may split or spell paths differently,
  or may expose the same volume through more than one handle. Looking at the
  bytes cannot be fooled by any of that: if the file we are about to chainload
  contains our banner, it is a GpuCheck build and must never be started.
**/
STATIC
BOOLEAN
FileLooksLikeGpuCheck (
  EFI_HANDLE     Device,
  CONST CHAR16  *Path
  )
{
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs     = NULL;
  EFI_FILE_PROTOCOL                *Root   = NULL;
  EFI_FILE_PROTOCOL                *File   = NULL;
  UINT8                            *Buffer = NULL;
  BOOLEAN                           Found  = FALSE;
  CONST CHAR16                     *Marker = GPUCHECK_IMAGE_MARKER;
  UINTN                             MarkerBytes;
  UINTN                             Size;
  UINTN                             i;

  if ((Device == NULL) || (Path == NULL)) {
    return FALSE;
  }

  MarkerBytes = StrLen (Marker) * sizeof (CHAR16);
  if (MarkerBytes == 0) {
    return FALSE;
  }

  if (EFI_ERROR (gBS->HandleProtocol (Device, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs)) || (Fs == NULL)) {
    return FALSE;
  }
  if (EFI_ERROR (Fs->OpenVolume (Fs, &Root)) || (Root == NULL)) {
    return FALSE;
  }
  if (EFI_ERROR (Root->Open (Root, &File, (CHAR16 *)Path, EFI_FILE_MODE_READ, 0)) || (File == NULL)) {
    Root->Close (Root);
    return FALSE;
  }

  Buffer = (UINT8 *)AllocatePool (GPUCHECK_SELFSCAN_BYTES);
  if (Buffer != NULL) {
    Size = GPUCHECK_SELFSCAN_BYTES;
    if (!EFI_ERROR (File->Read (File, &Size, Buffer)) && (Size >= MarkerBytes)) {
      for (i = 0; i + MarkerBytes <= Size; i++) {
        if (CompareMem (Buffer + i, Marker, MarkerBytes) == 0) {
          Found = TRUE;
          break;
        }
      }
    }
    FreePool (Buffer);
  }

  File->Close (File);
  Root->Close (Root);
  return Found;
}

/**
  TRUE when (Device, Path) must never be chainloaded because it is GpuCheck.

  Two independent tests, either of which is enough to reject:

    1. Content. The file contains GpuCheck's banner marker. Authoritative and
       immune to handle/path ambiguity, so it is tried first.
    2. Identity. The path matches our own reconstructed path on our own device.
       A cheap fallback for the case where the file could not be read.

  Getting this wrong in Mode A - where GpuCheck IS \EFI\Microsoft\Boot\
  bootmgfw.efi - means an endless boot loop that can only be broken by pulling
  the disk, so both tests deliberately err towards refusing.
**/
STATIC
BOOLEAN
IsOurself (
  EFI_HANDLE     Device,
  CONST CHAR16  *Path
  )
{
  STATIC CHAR16  OwnPath[WINBOOT_MAX_PATH];
  EFI_HANDLE     OwnDevice;

  /* 1. Definitive: does the image itself carry our marker? */
  if (FileLooksLikeGpuCheck (Device, Path)) {
    return TRUE;
  }

  /* 2. Fallback: same path on the volume we were loaded from. */
  OwnDevice = WinBootOwnDeviceHandle ();
  if ((OwnDevice != NULL) && (Device != OwnDevice)) {
    return FALSE;
  }
  if (!WinBootOwnFilePath (OwnPath, ARRAY_SIZE (OwnPath))) {
    /*
      Our own path is unknown. Be conservative: on our own volume, treat the
      stock loader name as us, because in Mode A that is exactly what we are.
    */
    return PathEqual (Path, L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi");
  }
  return PathEqual (Path, OwnPath);
}

/* ------------------------------------------------------------------------ */

BOOLEAN
WinBootFileExists (
  EFI_HANDLE     Device,
  CONST CHAR16  *Path
  )
{
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs   = NULL;
  EFI_FILE_PROTOCOL                *Root = NULL;
  EFI_FILE_PROTOCOL                *File = NULL;
  EFI_STATUS                        Status;

  if ((Device == NULL) || (Path == NULL)) {
    return FALSE;
  }

  Status = gBS->HandleProtocol (Device, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
  if (EFI_ERROR (Status) || (Fs == NULL)) {
    return FALSE;
  }

  Status = Fs->OpenVolume (Fs, &Root);
  if (EFI_ERROR (Status) || (Root == NULL)) {
    return FALSE;
  }

  Status = Root->Open (Root, &File, (CHAR16 *)Path, EFI_FILE_MODE_READ, 0);
  if (!EFI_ERROR (Status) && (File != NULL)) {
    File->Close (File);
    Root->Close (Root);
    return TRUE;
  }

  Root->Close (Root);
  return FALSE;
}

/* ------------------------------------------------------------------------ */

STATIC
BOOLEAN
TryDevice (
  EFI_HANDLE       Device,
  CONST CHAR16    *PreferredPath,
  BOOLEAN          OnOwnDevice,
  WINBOOT_TARGET  *Target
  )
{
  UINTN  i;

  if (Device == NULL) {
    return FALSE;
  }

  /* An explicit BOOT_PATH from gpucheck.cfg wins over everything else. */
  if ((PreferredPath != NULL) && (PreferredPath[0] != L'\0')) {
    if (!IsOurself (Device, PreferredPath) && WinBootFileExists (Device, PreferredPath)) {
      Target->Device      = Device;
      Target->OnOwnDevice = OnOwnDevice;
      Target->Found       = TRUE;
      StrCpy16 (Target->Path, ARRAY_SIZE (Target->Path), PreferredPath);
      return TRUE;
    }
  }

  for (i = 0; i < ARRAY_SIZE (mCandidatePaths); i++) {
    if (IsOurself (Device, mCandidatePaths[i])) {
      continue;                          /* never chainload ourselves */
    }
    if (WinBootFileExists (Device, mCandidatePaths[i])) {
      Target->Device      = Device;
      Target->OnOwnDevice = OnOwnDevice;
      Target->Found       = TRUE;
      StrCpy16 (Target->Path, ARRAY_SIZE (Target->Path), mCandidatePaths[i]);
      return TRUE;
    }
  }

  return FALSE;
}

EFI_STATUS
WinBootFindLoader (
  CONST CHAR16    *PreferredPath OPTIONAL,
  WINBOOT_TARGET  *Target
  )
{
  EFI_HANDLE  *Handles    = NULL;
  UINTN        HandleCount = 0;
  UINTN        Index;
  EFI_HANDLE   OwnDevice;
  EFI_STATUS   Status;

  if (Target == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  ZeroMem (Target, sizeof (*Target));

  /* 1. Our own volume - the normal case. */
  OwnDevice = WinBootOwnDeviceHandle ();
  if (TryDevice (OwnDevice, PreferredPath, TRUE, Target)) {
    return EFI_SUCCESS;
  }

  /* 2. Every other volume that exposes a filesystem. */
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiSimpleFileSystemProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    return EFI_NOT_FOUND;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    if (Handles[Index] == OwnDevice) {
      continue;                          /* already tried */
    }
    if (TryDevice (Handles[Index], PreferredPath, FALSE, Target)) {
      FreePool (Handles);
      return EFI_SUCCESS;
    }
  }

  FreePool (Handles);
  return EFI_NOT_FOUND;
}

/* ------------------------------------------------------------------------ */

EFI_STATUS
WinBootChainload (
  CONST WINBOOT_TARGET  *Target
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *DevPath;
  EFI_HANDLE                 NewImage     = NULL;
  UINTN                      ExitDataSize = 0;
  CHAR16                    *ExitData     = NULL;
  EFI_STATUS                 Status;

  if ((Target == NULL) || !Target->Found) {
    return EFI_NOT_FOUND;
  }

  DevPath = FileDevicePath (Target->Device, Target->Path);
  if (DevPath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  /*
    BootPolicy FALSE: we are naming an exact file, not asking the firmware to
    apply boot-manager policy.  If the platform refuses under Secure Boot we
    retry with TRUE, which lets the image be authorised as a boot option.
  */
  Status = gBS->LoadImage (FALSE, gImageHandle, DevPath, NULL, 0, &NewImage);

  if ((Status == EFI_ACCESS_DENIED) || (Status == EFI_SECURITY_VIOLATION)) {
    if ((Status == EFI_SECURITY_VIOLATION) && (NewImage != NULL)) {
      /* Loaded but not authorised - the spec requires us to unload it. */
      gBS->UnloadImage (NewImage);
      NewImage = NULL;
    }
    Status = gBS->LoadImage (TRUE, gImageHandle, DevPath, NULL, 0, &NewImage);
  }

  FreePool (DevPath);

  if (EFI_ERROR (Status)) {
    if ((Status == EFI_SECURITY_VIOLATION) && (NewImage != NULL)) {
      gBS->UnloadImage (NewImage);
    }
    return Status;
  }

  /* Do not let the 5-minute watchdog fire inside Windows' loader. */
  gBS->SetWatchdogTimer (0, 0, 0, NULL);

  Status = gBS->StartImage (NewImage, &ExitDataSize, &ExitData);

  /* Only reached if the Windows loader returned control to us. */
  if (ExitData != NULL) {
    FreePool (ExitData);
  }

  return Status;
}
