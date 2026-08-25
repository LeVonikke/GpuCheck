/** @file
  GpuCheck.c - Pre-boot PCI/PCIe display adapter diagnostic.

  Runs from the UEFI boot manager, before any operating system driver has
  touched the hardware, and answers exactly one question:

      did the firmware enumerate this GPU as a PCI display controller?

  It then chainloads the preserved Windows Boot Manager.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "GpuCheck.h"
#include "PciScan.h"
#include "WindowsBoot.h"
#include "GpuConfig.h"
#include "GpuLog.h"

#define RULE_WIDTH  60

typedef enum {
  ActionBoot = 0,
  ActionRescan,
  ActionPowerOff,
  ActionDetails,
  ActionNextPage
} USER_ACTION;

typedef enum {
  LayoutDetailed = 0,     /* labelled block, 9 rows per adapter */
  LayoutCompact,          /* 3 rows + blank per adapter         */
  LayoutTable             /* 1 row per adapter                  */
} LAYOUT_MODE;

STATIC UINTN  mColumns = 80;
STATIC UINTN  mRows    = 25;
STATIC UINTN  mPage    = 0;

/* ------------------------------------------------------------------------
 * Console helpers
 * --------------------------------------------------------------------- */

STATIC
VOID
SetColor (
  UINTN  Attribute
  )
{
  if ((gST != NULL) && (gST->ConOut != NULL)) {
    gST->ConOut->SetAttribute (gST->ConOut, Attribute);
  }
}

STATIC
VOID
ResetColor (
  VOID
  )
{
  SetColor (EFI_TEXT_ATTR (EFI_LIGHTGRAY, EFI_BACKGROUND_BLACK));
}

STATIC
VOID
Rule (
  CHAR16  Ch
  )
{
  CHAR16  Line[RULE_WIDTH + 1];
  UINTN   i;

  for (i = 0; i < RULE_WIDTH; i++) {
    Line[i] = Ch;
  }
  Line[RULE_WIDTH] = L'\0';
  Print (L"%s\n", Line);
}

/**
  Prefer a text mode tall enough for the detailed layout, but do not grab the
  largest mode available - on a 4K panel that produces unreadably small text.
  Smallest mode with at least 34 rows wins; otherwise the current mode stays.
**/
STATIC
VOID
SelectTextMode (
  VOID
  )
{
  INT32  Index;
  INT32  Best     = -1;
  UINTN  BestRows = 0;
  UINTN  Cols, Rws;

  mColumns = 80;
  mRows    = 25;

  if ((gST == NULL) || (gST->ConOut == NULL) || (gST->ConOut->Mode == NULL)) {
    return;
  }

  for (Index = 0; Index < gST->ConOut->Mode->MaxMode; Index++) {
    if (EFI_ERROR (gST->ConOut->QueryMode (gST->ConOut, (UINTN)Index, &Cols, &Rws))) {
      continue;
    }
    if ((Cols < 80) || (Rws < 34)) {
      continue;
    }
    if ((Best < 0) || (Rws < BestRows)) {
      Best     = Index;
      BestRows = Rws;
    }
  }

  if ((Best >= 0) && (Best != gST->ConOut->Mode->Mode)) {
    gST->ConOut->SetMode (gST->ConOut, (UINTN)Best);
  }

  if (!EFI_ERROR (gST->ConOut->QueryMode (gST->ConOut, (UINTN)gST->ConOut->Mode->Mode, &Cols, &Rws))) {
    mColumns = Cols;
    mRows    = Rws;
  }
}

STATIC
VOID
ClearScreen (
  VOID
  )
{
  ResetColor ();
  if ((gST != NULL) && (gST->ConOut != NULL)) {
    gST->ConOut->ClearScreen (gST->ConOut);
    gST->ConOut->EnableCursor (gST->ConOut, FALSE);
  }
}

/* ------------------------------------------------------------------------
 * Verdict
 * --------------------------------------------------------------------- */

typedef struct {
  BOOLEAN  UsingExpectation;
  BOOLEAN  ExpectedFound;
  BOOLEAN  DiscreteFound;
  BOOLEAN  TargetPresent;      /* the thing we care about is there */
} VERDICT;

STATIC
VOID
ComputeVerdict (
  CONST GPU_SCAN_RESULTS  *Results,
  CONST GPUCHECK_CONFIG   *Config,
  VERDICT                 *Verdict
  )
{
  UINTN  i;

  ZeroMem (Verdict, sizeof (*Verdict));
  Verdict->DiscreteFound = GpuHasProbableDiscrete (Results);

  /*
    Either key on its own is a valid pin. Requiring EXPECT_VENDOR would mean a
    config with only EXPECT_DEVICE=2684 silently falls back to the generic
    heuristic - the tool would report "Discrete GPU detected" for some other
    card and boot, having ignored the exact board it was told to watch.
  */
  if (Config->HasExpectVendor || Config->HasExpectDevice) {
    Verdict->UsingExpectation = TRUE;
    for (i = 0; i < Results->Count; i++) {
      CONST GPU_DEVICE_INFO  *D = &Results->Devices[i];

      if (Config->HasExpectVendor && (D->VendorId != Config->ExpectVendor)) {
        continue;
      }
      if (Config->HasExpectDevice && !Config->ExpectDeviceAny &&
          (D->DeviceId != Config->ExpectDevice))
      {
        continue;
      }
      Verdict->ExpectedFound = TRUE;
      break;
    }
    Verdict->TargetPresent = Verdict->ExpectedFound;
  } else {
    Verdict->TargetPresent = Verdict->DiscreteFound;
  }

  if (Results->Count == 0) {
    Verdict->TargetPresent = FALSE;
  }
}

/* ------------------------------------------------------------------------
 * Rendering
 * --------------------------------------------------------------------- */

STATIC
VOID
PrintBdf (
  CONST GPU_DEVICE_INFO  *D
  )
{
  Print (L"%04X:%02X:%02X.%X", (UINTN)D->Segment, (UINTN)D->Bus, (UINTN)D->Device, (UINTN)D->Function);
}

STATIC
VOID
RenderDeviceDetailed (
  UINTN                   Index,
  CONST GPU_DEVICE_INFO  *D
  )
{
  Print (L"[%d]\n", (UINTN)Index);

  Print (L"BDF       : ");
  PrintBdf (D);
  Print (L"\n");

  Print (L"Vendor    : %s\n", GpuVendorName (D->VendorId));
  Print (L"VID:DID   : %04X:%04X\n", (UINTN)D->VendorId, (UINTN)D->DeviceId);
  Print (
    L"Class     : %02X:%02X %s\n",
    (UINTN)D->BaseClass,
    (UINTN)D->SubClass,
    GpuClassName (D->BaseClass, D->SubClass)
    );

  if (D->Type == GpuTypeDiscrete) {
    SetColor (EFI_TEXT_ATTR (EFI_LIGHTGREEN, EFI_BACKGROUND_BLACK));
  } else if (D->Type == GpuTypeUnknown) {
    SetColor (EFI_TEXT_ATTR (EFI_YELLOW, EFI_BACKGROUND_BLACK));
  }
  Print (L"Type      : %s\n", GpuTypeName (D->Type));
  ResetColor ();

  if (D->PcieCapValid && (D->CurrentLinkSpeed != 0)) {
    Print (
      L"Link      : %s x%d\n",
      GpuLinkSpeedName (D->CurrentLinkSpeed),
      (UINTN)D->CurrentLinkWidth
      );
    Print (
      L"Capability: %s x%d\n",
      GpuLinkSpeedName (D->MaxLinkSpeed),
      (UINTN)D->MaxLinkWidth
      );
  } else {
    Print (L"Link      : (no PCIe capability reported)\n");
  }

  Print (L"\n");
}

STATIC
VOID
RenderDeviceCompact (
  UINTN                   Index,
  CONST GPU_DEVICE_INFO  *D
  )
{
  Print (L"[%d] ", (UINTN)Index);
  PrintBdf (D);
  Print (L"  %04X:%04X  %s\n", (UINTN)D->VendorId, (UINTN)D->DeviceId, GpuVendorName (D->VendorId));

  Print (
    L"    Class %02X:%02X %s\n",
    (UINTN)D->BaseClass,
    (UINTN)D->SubClass,
    GpuClassName (D->BaseClass, D->SubClass)
    );

  Print (L"    Type  %-11s", GpuTypeName (D->Type));
  if (D->PcieCapValid && (D->CurrentLinkSpeed != 0)) {
    Print (
      L" Link %s x%d (max %s x%d)",
      GpuLinkSpeedName (D->CurrentLinkSpeed),
      (UINTN)D->CurrentLinkWidth,
      GpuLinkSpeedName (D->MaxLinkSpeed),
      (UINTN)D->MaxLinkWidth
      );
  }
  Print (L"\n\n");
}

STATIC
VOID
RenderDeviceTableRow (
  UINTN                   Index,
  CONST GPU_DEVICE_INFO  *D
  )
{
  Print (L"%2d  ", (UINTN)Index);
  PrintBdf (D);
  Print (
    L"  %04X:%04X  %02X:%02X  %-11s",
    (UINTN)D->VendorId,
    (UINTN)D->DeviceId,
    (UINTN)D->BaseClass,
    (UINTN)D->SubClass,
    GpuTypeName (D->Type)
    );
  if (D->PcieCapValid && (D->CurrentLinkSpeed != 0)) {
    Print (L" %s x%d", GpuLinkSpeedName (D->CurrentLinkSpeed), (UINTN)D->CurrentLinkWidth);
  }
  Print (L"\n");
}

/**
  Pick the richest layout whose rendered height still fits the console, so
  nothing ever scrolls the verdict off the top of the screen.
**/
STATIC
LAYOUT_MODE
ChooseLayout (
  UINTN  Count,
  UINTN  Rows,
  UINTN  Overhead
  )
{
  if (Count == 0) {
    return LayoutDetailed;
  }
  if ((Overhead + (Count * 9)) <= Rows) {
    return LayoutDetailed;
  }
  if ((Overhead + (Count * 4)) <= Rows) {
    return LayoutCompact;
  }
  return LayoutTable;
}

STATIC
VOID
RenderHeader (
  CONST GPU_SCAN_RESULTS  *Results
  )
{
  SetColor (EFI_TEXT_ATTR (EFI_WHITE, EFI_BACKGROUND_BLACK));
  Rule (L'=');
  Print (L"                  " GPUCHECK_IMAGE_MARKER L"\n");
  Rule (L'=');
  ResetColor ();
  Print (L"\n");

  if (Results->Count == 0) {
    SetColor (EFI_TEXT_ATTR (EFI_LIGHTRED, EFI_BACKGROUND_BLACK));
    Print (L"NO PCI DISPLAY CONTROLLERS DETECTED\n");
    ResetColor ();
    Print (L"\nPCI functions seen: %d\n", (UINTN)Results->TotalPciFunctions);
  } else {
    Print (L"PCI display adapters detected: %d\n", (UINTN)Results->Count);
  }

  if (Results->UsedRootBridgeFallback && (Results->Count > 0)) {
    SetColor (EFI_TEXT_ATTR (EFI_YELLOW, EFI_BACKGROUND_BLACK));
    Print (L"(found via root-bridge fallback scan, not PciIo)\n");
    ResetColor ();
  }
  if (Results->Overflow) {
    Print (L"(more than %d adapters present; list truncated)\n", (UINTN)GPUCHECK_MAX_DISPLAY_DEVICES);
  }

  Print (L"\n");
}

/**
  Draw the whole screen.  Returns the console row the countdown line lives
  on, so it can be refreshed in place without a full repaint.
**/
STATIC
UINTN
RenderScreen (
  CONST GPU_SCAN_RESULTS  *Results,
  CONST GPUCHECK_CONFIG   *Config,
  CONST VERDICT           *Verdict,
  BOOLEAN                  WillAutoboot,
  UINTN                   *TotalPages
  )
{
  LAYOUT_MODE  Layout;
  UINTN        HeaderRows;
  UINTN        Overhead;
  UINTN        i;
  UINTN        PerPage;
  UINTN        First;
  UINTN        Shown = 0;
  UINTN        CountdownRow;

  ClearScreen ();
  RenderHeader (Results);

  /*
    Row budget. Everything printed outside the adapter list has to be counted,
    otherwise the list overruns the console and scrolls the verdict - the one
    line that matters - off the top where it can never be read back.

      header  : rule, title, rule, blank, count, blank              = 6
      footer  : rule, blank, verdict, blank, 4 legend lines,
                autoboot blank, countdown                           = 10
  */
  HeaderRows = (Results->Count == 0) ? 8 : 6;
  if (Config->Loaded && (Config->Encoding != ConfigEncodingOk)) {
    SetColor (EFI_TEXT_ATTR (EFI_YELLOW, EFI_BACKGROUND_BLACK));
    Print (L"gpucheck.cfg is UTF-16 and was ignored - save it as ANSI/UTF-8.\n");
    ResetColor ();
    HeaderRows++;
  }

  Overhead = HeaderRows + 10;

  Layout = ChooseLayout (Results->Count, mRows, Overhead);

  if (Layout == LayoutTable) {
    UINTN  RowsForList;

    Print (L" #  BDF           VID:DID    CLASS  TYPE        LINK\n");
    Print (L"---------------------------------------------------------\n");

    /* table heading (2) + page indicator (2) + the extra "N" legend line (1) */
    Overhead += 5;

    RowsForList = (mRows > Overhead) ? (mRows - Overhead) : 1;
    PerPage     = RowsForList;
    if (PerPage == 0) {
      PerPage = 1;
    }
  } else {
    PerPage = Results->Count;
    if (PerPage == 0) {
      PerPage = 1;
    }
  }

  *TotalPages = (Results->Count + PerPage - 1) / PerPage;
  if (*TotalPages == 0) {
    *TotalPages = 1;
  }
  if (mPage >= *TotalPages) {
    mPage = 0;
  }
  First = mPage * PerPage;

  for (i = First; (i < Results->Count) && (Shown < PerPage); i++, Shown++) {
    switch (Layout) {
      case LayoutDetailed:  RenderDeviceDetailed (i, &Results->Devices[i]);  break;
      case LayoutCompact:   RenderDeviceCompact (i, &Results->Devices[i]);   break;
      default:              RenderDeviceTableRow (i, &Results->Devices[i]);  break;
    }
  }

  if (*TotalPages > 1) {
    Print (L"\n-- page %d of %d --\n", (UINTN)(mPage + 1), (UINTN)*TotalPages);
  }

  Rule (L'-');
  Print (L"\n");

  /* ---- verdict ---- */
  if (Results->Count == 0) {
    SetColor (EFI_TEXT_ATTR (EFI_WHITE, EFI_BACKGROUND_RED));
    Print (L" NO PCI DISPLAY CONTROLLERS DETECTED ");
    ResetColor ();
    Print (L"\n");
  } else if (Verdict->UsingExpectation) {
    if (Verdict->ExpectedFound) {
      SetColor (EFI_TEXT_ATTR (EFI_LIGHTGREEN, EFI_BACKGROUND_BLACK));
      Print (L"Expected GPU present");
      if (Config->HasExpectDevice && !Config->ExpectDeviceAny) {
        Print (L" (%04X:%04X).", (UINTN)Config->ExpectVendor, (UINTN)Config->ExpectDevice);
      } else {
        Print (L" (vendor %04X).", (UINTN)Config->ExpectVendor);
      }
      Print (L"\n");
    } else {
      SetColor (EFI_TEXT_ATTR (EFI_WHITE, EFI_BACKGROUND_RED));
      if (Config->HasExpectDevice && !Config->ExpectDeviceAny) {
        Print (
          L" WARNING: EXPECTED GPU %04X:%04X NOT FOUND ",
          (UINTN)Config->ExpectVendor,
          (UINTN)Config->ExpectDevice
          );
      } else {
        Print (L" WARNING: NO GPU FROM VENDOR %04X FOUND ", (UINTN)Config->ExpectVendor);
      }
      Print (L"\n");
    }
    ResetColor ();
  } else if (Verdict->DiscreteFound) {
    SetColor (EFI_TEXT_ATTR (EFI_LIGHTGREEN, EFI_BACKGROUND_BLACK));
    Print (L"Discrete GPU detected.\n");
    ResetColor ();
  } else {
    SetColor (EFI_TEXT_ATTR (EFI_WHITE, EFI_BACKGROUND_RED));
    Print (L" WARNING: NO DISCRETE DISPLAY ADAPTER DETECTED ");
    ResetColor ();
    Print (L"\n");
  }

  Print (L"\n");

  /* ---- key legend ---- */
  if (Verdict->TargetPresent) {
    Print (L"ENTER = Boot Windows\n");
  } else {
    Print (L"ENTER = Boot Windows anyway\n");
  }
  Print (L"R     = Rescan PCI\n");
  Print (L"P     = Power Off\n");
  Print (L"ESC   = Details\n");
  if (*TotalPages > 1) {
    Print (L"N     = Next page\n");
  }

  CountdownRow = (UINTN)gST->ConOut->Mode->CursorRow;

  if (WillAutoboot) {
    Print (L"\n");
    CountdownRow = (UINTN)gST->ConOut->Mode->CursorRow;
  }

  return CountdownRow;
}

STATIC
VOID
DrawCountdown (
  UINTN  Row,
  UINTN  Seconds
  )
{
  if ((gST == NULL) || (gST->ConOut == NULL)) {
    return;
  }
  gST->ConOut->SetCursorPosition (gST->ConOut, 0, Row);
  Print (L"Automatic boot in %d seconds...      ", (UINTN)Seconds);
}

STATIC
VOID
ClearCountdown (
  UINTN  Row
  )
{
  if ((gST == NULL) || (gST->ConOut == NULL)) {
    return;
  }
  gST->ConOut->SetCursorPosition (gST->ConOut, 0, Row);
  Print (L"Automatic boot cancelled - waiting.   ");
}

/* ------------------------------------------------------------------------
 * Input
 * --------------------------------------------------------------------- */

/**
  Discard anything already sitting in the firmware's input queue.

  Without this, a keystroke typed long before GpuCheck started - holding ENTER
  to pick a boot entry, mashing a key during POST - is still queued when we
  begin waiting, and is consumed as though it were a deliberate answer to the
  screen we just painted. On the "discrete GPU missing" halt screen that stray
  ENTER would boot Windows immediately, defeating the one guarantee this tool
  is supposed to provide.
**/
STATIC
VOID
DrainKeystrokes (
  VOID
  )
{
  EFI_INPUT_KEY  Key;
  UINTN          Guard;

  if ((gST == NULL) || (gST->ConIn == NULL)) {
    return;
  }
  for (Guard = 0; Guard < 256; Guard++) {
    if (EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
      return;
    }
  }
}

STATIC
USER_ACTION
WaitForAction (
  BOOLEAN  UseTimeout,
  UINTN    Seconds,
  UINTN    CountdownRow
  )
{
  EFI_INPUT_KEY  Key;
  EFI_STATUS     Status;
  UINTN          Remaining = Seconds;
  UINTN          Ticks     = 0;
  BOOLEAN        HaveConIn;

  HaveConIn = (BOOLEAN)((gST != NULL) && (gST->ConIn != NULL));

  DrainKeystrokes ();

  if (UseTimeout) {
    DrawCountdown (CountdownRow, Remaining);
  }

  /*
    With no input device at all there is nothing to wait for. Boot if a
    countdown was requested; otherwise honour the halt and idle here, which
    is the documented behaviour when the expected GPU is missing.
  */
  if (!HaveConIn) {
    if (UseTimeout) {
      while (Remaining > 0) {
        gBS->Stall (1000000);
        Remaining--;
        DrawCountdown (CountdownRow, Remaining);
      }
      return ActionBoot;
    }
    for ( ; ; ) {
      gBS->Stall (1000000);
    }
  }

  for ( ; ; ) {
    Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
    if (!EFI_ERROR (Status)) {
      /*
        ESC first: some firmware and every serial-console redirection path
        deliver it as UnicodeChar 0x1B with ScanCode 0, so testing ScanCode
        alone would let it fall through to the "any other key" branch.
      */
      if ((Key.ScanCode == SCAN_ESC) || (Key.UnicodeChar == 0x1B)) {
        return ActionDetails;
      }
      if ((Key.UnicodeChar == CHAR_CARRIAGE_RETURN) || (Key.UnicodeChar == CHAR_LINEFEED)) {
        return ActionBoot;
      }
      if ((Key.UnicodeChar == L'r') || (Key.UnicodeChar == L'R')) {
        return ActionRescan;
      }
      if ((Key.UnicodeChar == L'p') || (Key.UnicodeChar == L'P')) {
        return ActionPowerOff;
      }
      if ((Key.UnicodeChar == L'n') || (Key.UnicodeChar == L'N')) {
        return ActionNextPage;
      }

      /* Any other key means "a human is here" - stop the countdown. */
      if (UseTimeout) {
        UseTimeout = FALSE;
        ClearCountdown (CountdownRow);
      }
      continue;
    }

    /*
      The zero check lives here, after at least one ReadKeyStroke, so even a
      zero-second countdown samples the keyboard once and leaves an escape
      hatch instead of leaving for Windows before anything can be pressed.
    */
    if (UseTimeout && (Remaining == 0)) {
      return ActionBoot;
    }

    gBS->Stall (50000);                    /* 50 ms */

    if (UseTimeout) {
      Ticks++;
      if (Ticks >= 20) {                   /* one second */
        Ticks = 0;
        if (Remaining > 0) {
          Remaining--;
        }
        DrawCountdown (CountdownRow, Remaining);
      }
    }
  }
}

STATIC
VOID
WaitForAnyKey (
  VOID
  )
{
  EFI_INPUT_KEY  Key;

  if ((gST == NULL) || (gST->ConIn == NULL)) {
    gBS->Stall (5000000);                  /* no input device: pause 5 s */
    return;
  }

  DrainKeystrokes ();

  for ( ; ; ) {
    if (!EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
      return;
    }
    gBS->Stall (50000);
  }
}

/* ------------------------------------------------------------------------
 * Details screen
 * --------------------------------------------------------------------- */

STATIC
VOID
ShowDetails (
  CONST GPU_SCAN_RESULTS  *Results,
  CONST GPUCHECK_CONFIG   *Config,
  CONST WINBOOT_TARGET    *Target
  )
{
  STATIC CHAR16  OwnPath[WINBOOT_MAX_PATH];
  UINTN          i;

  ClearScreen ();
  SetColor (EFI_TEXT_ATTR (EFI_WHITE, EFI_BACKGROUND_BLACK));
  Rule (L'=');
  Print (L"                       DETAILS\n");
  Rule (L'=');
  ResetColor ();
  Print (L"\n");

  Print (L"GpuCheck version    : %s\n", GPUCHECK_VERSION);
  if ((gST != NULL) && (gST->FirmwareVendor != NULL)) {
    Print (L"Firmware vendor     : %s\n", gST->FirmwareVendor);
    Print (L"Firmware revision   : %08X\n", (UINTN)gST->FirmwareRevision);
  }
  Print (L"UEFI table revision : %08X\n", (UINTN)gST->Hdr.Revision);
  Print (L"Console size        : %d x %d\n", (UINTN)mColumns, (UINTN)mRows);
  Print (L"\n");

  Print (L"PciIo handles       : %d\n", (UINTN)Results->PciIoHandleCount);
  Print (L"PCI functions seen  : %d\n", (UINTN)Results->TotalPciFunctions);
  Print (L"Display adapters    : %d\n", (UINTN)Results->Count);
  Print (L"ConnectAll retried  : %s\n", Results->ConnectAllAttempted ? L"yes" : L"no");
  Print (L"Root-bridge fallback: %s\n", Results->UsedRootBridgeFallback ? L"yes" : L"no");
  Print (L"\n");

  if (WinBootOwnFilePath (OwnPath, ARRAY_SIZE (OwnPath))) {
    Print (L"Running as          : %s\n", OwnPath);
  }
  if (Target->Found) {
    Print (L"Windows loader      : %s\n", Target->Path);
    Print (L"On boot volume      : %s\n", Target->OnOwnDevice ? L"yes" : L"no");
  } else {
    SetColor (EFI_TEXT_ATTR (EFI_LIGHTRED, EFI_BACKGROUND_BLACK));
    Print (L"Windows loader      : NOT FOUND\n");
    ResetColor ();
  }
  Print (L"\n");

  Print (L"Config file         : %s\n", Config->Loaded ? GPUCHECK_CONFIG_PATH : L"(none - using defaults)");
  if (Config->HasExpectVendor) {
    Print (L"  EXPECT_VENDOR     : %04X\n", (UINTN)Config->ExpectVendor);
  }
  if (Config->HasExpectDevice) {
    if (Config->ExpectDeviceAny) {
      Print (L"  EXPECT_DEVICE     : ANY\n");
    } else {
      Print (L"  EXPECT_DEVICE     : %04X\n", (UINTN)Config->ExpectDevice);
    }
  }
  Print (L"  AUTOBOOT_WHEN_FOUND: %s\n", Config->AutobootWhenFound ? L"1" : L"0");
  Print (L"  AUTOBOOT_DELAY     : %d\n", (UINTN)Config->AutobootDelay);
  Print (L"  HALT_WHEN_MISSING  : %s\n", Config->HaltWhenMissing ? L"1" : L"0");

  Print (L"\nSubsystem IDs:\n");
  for (i = 0; i < Results->Count; i++) {
    CONST GPU_DEVICE_INFO  *D = &Results->Devices[i];

    Print (L"  [%d] ", (UINTN)i);
    PrintBdf (D);
    Print (
      L"  subsys %04X:%04X  rev %02X  progif %02X  rom %d\n",
      (UINTN)D->SubsysVendorId,
      (UINTN)D->SubsysDeviceId,
      (UINTN)D->RevisionId,
      (UINTN)D->ProgInterface,
      (UINTN)D->RomSize
      );
    if (D->PcieCapValid) {
      Print (
        L"      PCIe cap @%02X v%d  port type %s\n",
        (UINTN)D->PcieCapOffset,
        (UINTN)D->PcieCapVersion,
        GpuPortTypeName (D->PcieDevicePortType)
        );
    }
  }

  Print (L"\nPress any key to return.\n");
  WaitForAnyKey ();
}

/* ------------------------------------------------------------------------
 * Boot failure screen
 * --------------------------------------------------------------------- */

STATIC
USER_ACTION
ShowBootError (
  EFI_STATUS             Status,
  CONST WINBOOT_TARGET  *Target
  )
{
  UINTN  i;

  ClearScreen ();
  SetColor (EFI_TEXT_ATTR (EFI_WHITE, EFI_BACKGROUND_RED));
  if (Target->Found) {
    Print (L" ERROR: Windows Boot Manager could not be started. ");
  } else {
    Print (L" ERROR: Windows Boot Manager not found. ");
  }
  ResetColor ();
  Print (L"\n\n");

  if (Target->Found) {
    Print (L"Loader  : %s\n", Target->Path);
    Print (L"Status  : %016X\n", (UINTN)Status);
    Print (L"\n");
    Print (L"If Secure Boot is enabled, disable it in firmware setup:\n");
    Print (L"an unsigned chainloaded image will be refused.\n");
  } else {
    Print (L"Expected:\n");
    Print (L"%s\n", WINBOOT_PREFERRED_PATH);
    Print (L"\nAlso searched, on every mounted volume:\n");
    for (i = 1; i < WinBootCandidateCount (); i++) {
      Print (L"  %s\n", WinBootCandidatePath (i));
    }
    Print (L"\nRe-run scripts\\install-gpucheck.ps1 to restore the backup.\n");
  }

  Print (L"\n");
  Rule (L'-');
  Print (L"\n");
  Print (L"R = retry\n");
  Print (L"P = power off\n");

  for ( ; ; ) {
    USER_ACTION  Action = WaitForAction (FALSE, 0, 0);

    if ((Action == ActionRescan) || (Action == ActionPowerOff)) {
      return Action;
    }
  }
}

/* ------------------------------------------------------------------------
 * Power off
 * --------------------------------------------------------------------- */

STATIC
VOID
PowerOff (
  VOID
  )
{
  Print (L"\nShutting down...\n");

  if ((gRT != NULL) && (gRT->ResetSystem != NULL)) {
    gRT->ResetSystem (EfiResetShutdown, EFI_SUCCESS, 0, NULL);
  }

  /* Only reached if the platform refused to power off. */
  SetColor (EFI_TEXT_ATTR (EFI_YELLOW, EFI_BACKGROUND_BLACK));
  Print (L"Firmware did not honour the shutdown request.\n");
  Print (L"Power the machine off manually.\n");
  ResetColor ();
}

/* ------------------------------------------------------------------------
 * Entry point
 * --------------------------------------------------------------------- */

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  GPU_SCAN_RESULTS  Results;
  GPUCHECK_CONFIG   Config;
  VERDICT           Verdict;
  WINBOOT_TARGET    Target;
  EFI_HANDLE        OwnDevice;
  EFI_STATUS        Status;
  USER_ACTION       Action;
  BOOLEAN           WillAutoboot;
  BOOLEAN           BootAttempted = FALSE;
  UINTN             CountdownRow;
  UINTN             TotalPages;

  /*
    Both handles reach us through globals (gImageHandle / gST), set by the
    EDK II entry point library or by StandaloneUefiInit().
  */
  (VOID)ImageHandle;
  (VOID)SystemTable;

  /*
    A UEFI application inherits a 5-minute watchdog.  We may sit at the
    "discrete GPU missing" screen far longer than that while a technician
    works on the machine, so disable it immediately.
  */
  gBS->SetWatchdogTimer (0, 0, 0, NULL);

  SelectTextMode ();

  OwnDevice = WinBootOwnDeviceHandle ();
  GpuConfigLoad (OwnDevice, &Config);

  for ( ; ; ) {
    GpuScanPci (&Results);
    ComputeVerdict (&Results, &Config, &Verdict);

    WinBootFindLoader (Config.HasBootPath ? Config.BootPath : NULL, &Target);

    if (Config.LogEnable && (OwnDevice != NULL)) {
      GpuLogWriteScan (OwnDevice, &Results);     /* best effort, ignore result */
    }

    /*
      Autoboot only when what we were looking for is actually present.
      When it is missing we stop and wait, which is the entire point of
      this tool - never quietly continue into Windows.

      AUTOBOOT_WHEN_FOUND stays authoritative in both directions: expressing
      this as one condition avoids the inversion an override statement caused,
      where AUTOBOOT_WHEN_FOUND=0 plus HALT_WHEN_MISSING=0 would wait when the
      GPU was present and boot when it was missing - exactly backwards.

      BootAttempted latches after any chainload that handed control back to
      us (a damaged BCD, or the operator picking "Turn off your PC" in the
      Windows boot menu). Without it the loop re-arms and relaunches the
      Windows loader every few seconds forever.
    */
    WillAutoboot = (BOOLEAN)(
                     !BootAttempted &&
                     Config.AutobootWhenFound &&
                     (Verdict.TargetPresent || !Config.HaltWhenMissing)
                     );

    CountdownRow = RenderScreen (&Results, &Config, &Verdict, WillAutoboot, &TotalPages);

    Action = WaitForAction (WillAutoboot, Config.AutobootDelay, CountdownRow);

    if (Action == ActionRescan) {
      mPage = 0;
      continue;
    }

    if (Action == ActionNextPage) {
      mPage++;
      continue;
    }

    if (Action == ActionDetails) {
      ShowDetails (&Results, &Config, &Target);
      continue;
    }

    if (Action == ActionPowerOff) {
      PowerOff ();
      /*
        Only reached when the firmware refused to power off. Hold the message
        on screen: falling straight back into the loop would clear it within
        milliseconds and re-arm the countdown, turning a shutdown request into
        an unattended boot.
      */
      WaitForAnyKey ();
      continue;
    }

    /* ActionBoot */
    if (!Target.Found) {
      Status = EFI_NOT_FOUND;
    } else {
      ClearScreen ();
      Print (L"Starting Windows Boot Manager:\n%s\n", Target.Path);
      BootAttempted = TRUE;
      Status        = WinBootChainload (&Target);
    }

    if (EFI_ERROR (Status)) {
      Action = ShowBootError (Status, &Target);
      if (Action == ActionPowerOff) {
        PowerOff ();
        WaitForAnyKey ();
      }
      mPage = 0;
      continue;                              /* retry: rescan and redraw */
    }

    /*
      StartImage() returned without an error, meaning the Windows loader
      handed control back to us.  Show the screen again rather than exiting
      into the firmware boot menu.
    */
    mPage = 0;
  }
}

#ifndef GPUCHECK_EDK2

/**
  Standalone entry point.  EDK II supplies its own via
  UefiApplicationEntryPoint, which then calls UefiMain().
**/
EFI_STATUS
EFIAPI
EfiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  StandaloneUefiInit (ImageHandle, SystemTable);
  return UefiMain (ImageHandle, SystemTable);
}

#endif
