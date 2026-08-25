# GpuCheck

A small standalone **x86-64 UEFI application** that runs *before* Windows starts,
enumerates PCI/PCIe display adapters directly from PCI configuration space, shows
you what the firmware actually found, and then chainloads the real Windows Boot
Manager.

It exists to answer one question, fast, for GPU and motherboard repair work:

> **Did the firmware enumerate this GPU as a PCI/PCIe display controller?**

If the card enumerated, you see its BDF, vendor:device ID, class/subclass and
PCIe link state within a couple of seconds of power-on. If it did not, the boot
**stops** and tells you so, instead of making you wait for Windows to come up and
then hunting through Device Manager.

No drivers, no OS, no WMI, no DirectX, no `lspci`. Just PCI config space.

---

## Boot flow

```text
UEFI firmware
    |
    v
GpuCheck.efi          <- enumerates PCI, shows display adapters
    |
    v
Windows Boot Manager  <- the preserved original
    |
    v
Windows
```

---

## What it reports

For every PCI function whose **Base Class is 0x03** (display controller):

| Field | Source |
|---|---|
| Segment / Bus / Device / Function | `EFI_PCI_IO_PROTOCOL.GetLocation()` |
| Vendor ID / Device ID | config space `0x00` / `0x02` |
| Base class / subclass / prog-IF / revision | config space `0x0B` / `0x0A` / `0x09` / `0x08` |
| Subsystem vendor / device | config space `0x2C` / `0x2E` |
| PCIe current link speed / width | PCIe capability, Link Status |
| PCIe max link speed / width | PCIe capability, Link Capabilities (+ Link Cap 2) |
| Expansion ROM size | `EFI_PCI_IO_PROTOCOL.RomSize` |

Recognised vendors: `10DE` NVIDIA, `1002` AMD/ATI, `8086` Intel, `1022` AMD, plus
Matrox, ASPEED, VMware, Red Hat/QEMU, S3, Cirrus, XGI, SiS, VIA. **Anything else
is still shown**, with its raw hex IDs — an unknown vendor is never hidden.

Subclasses are interpreted as `03:00` VGA compatible, `03:01` XGA, `03:02` 3D,
`03:80` other display controller.

### Detection is deliberately not GOP-based

A GPU is reported purely because its PCI Base Class is `0x03`. GpuCheck never
infers a GPU from Graphics Output Protocol handles, and never requires the card
to expose a framebuffer. **This is the whole point**: a card can enumerate on PCI
while failing to initialise any display output, and that distinction is precisely
what tells you whether you have a dead GPU or a dead output stage.

### Enumeration passes

1. **`EFI_PCI_IO_PROTOCOL`** — the mandatory path. Every handle exposing PciIo is
   queried and its configuration space read directly.
2. **Connect-all retry** — if *no* PciIo handles exist at all, GpuCheck asks the
   platform to connect its drivers (the same thing the boot manager does) and
   rescans. This is not a bus reset.
3. **`EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL` brute force** — only if passes 1–2 found
   *zero* display controllers. Reads config space directly across the bus range
   the root bridge reports owning, so a device the PCI bus driver skipped is
   still surfaced. Marked on screen as a fallback result.

All three passes are strictly **read-only**. No BAR is written, no bus is reset,
no device is re-enumerated.

---

## Integrated vs discrete

Reported as `Integrated`, `Discrete`, or `Unknown`. The rules, in order:

| Signal | Verdict |
|---|---|
| PCIe port type = Root Complex Integrated Endpoint (0x9) | Integrated |
| Segment 0, **bus 0** (the root complex itself) | Integrated |
| AMD/ATI device ID in the known APU/IGP table | Integrated |
| NVIDIA on a non-zero bus | Discrete |
| Intel on a non-zero bus (iGPUs are always `00:02.x`) | Discrete |
| Expansion ROM present + PCIe endpoint + non-zero bus | Discrete |
| anything else | **Unknown** |

The expansion-ROM signal works because add-in boards carry their own video BIOS
in on-card flash, while integrated graphics get theirs from system firmware.

**`Unknown` is a real answer, not a failure.** When the signals do not add up
GpuCheck says so rather than inventing a classification. Vendor ID alone never
decides this — AMD ships both APU graphics and add-in Radeons under `1002`.

Classification is **presentation only**. It never gates enumeration or reporting;
the raw BDF, VID:DID and class are always what matter, and are always shown.

For repair work, don't rely on the heuristic at all — pin the exact card with
`EXPECT_VENDOR` / `EXPECT_DEVICE` (see [Configuration](#configuration)). That
removes all guessing.

---

## Boot behaviour

| Situation | Default action |
|---|---|
| Expected / probable discrete GPU **present** | auto-boot Windows after 3 s |
| Expected / probable discrete GPU **missing** | **STOP and wait indefinitely** |
| No class-03 devices at all | **STOP**, red `NO PCI DISPLAY CONTROLLERS DETECTED` |

GpuCheck **never** auto-continues into Windows when the GPU it was looking for is
absent. That is the entire safety property of the tool.

Keys: `ENTER` boot · `R` rescan · `P` power off · `ESC` details · `N` next page

`P` uses `gRT->ResetSystem(EfiResetShutdown, ...)`. It does not halt the CPU.

`R` re-runs enumeration and redraws. Note that firmware will not retrain or
re-enumerate hardware just because the application scans again — rescan refreshes
*what firmware currently exposes*. It deliberately performs no bus resets.

---

## Installation modes

### Mode A — Windows Boot Manager interception (preferred)

```text
Firmware picks "Windows Boot Manager"
        |
        v
   GpuCheck  (installed as bootmgfw.efi)
        |
        v
   Windows   (via bootmgfw-original.efi)
```

`\EFI\Microsoft\Boot\bootmgfw.efi` is backed up to `bootmgfw-original.efi` plus a
timestamped copy, then replaced by GpuCheck. This is the reliable mode: it runs
whenever the firmware boots Windows normally.

### Mode B — UEFI fallback path

```text
Firmware picks the generic UEFI disk / "UEFI OS" entry
        |
        v
   \EFI\BOOT\BOOTX64.EFI  (GpuCheck)
        |
        v
   Windows
```

Installs `\EFI\GPUCHECK\GpuCheck.efi` and `\EFI\BOOT\BOOTX64.EFI` (backing up any
existing `BOOTX64.EFI` to `BOOTX64-BACKUP.EFI` first). Useful when you select the
raw disk from a firmware boot menu.

Both modes can be installed together; that is the default.

### Why not an NVRAM `Boot####` entry?

Because it would not travel with the disk. A `Boot####` variable belongs to the
motherboard it was created on, and this SSD is meant to be moved between test
machines. The interception therefore lives **on the disk itself**.

---

## Requirements on the test machine

### Secure Boot must be OFF

GpuCheck is unsigned. With Secure Boot enabled the firmware will refuse to load
it (and, in Mode A, refuse to boot at all — because GpuCheck *is* the loader).

**Disable Secure Boot in firmware setup on the test motherboard.**

GpuCheck does not attempt to disable Secure Boot, does not modify Secure Boot
keys, and does not delete Microsoft's keys. It cannot and should not.

### BitLocker

Altering the boot chain can make Windows ask for the **BitLocker recovery key**
on the next boot, because the measured boot path changed.

The installer checks BitLocker state and refuses to proceed on a protected volume
unless you pass `-AcceptBitLockerRisk`. It never disables encryption, and never
touches TPM or PCR configuration.

> On the SSD this was built for, BitLocker protection was verified **Off**.

---

## Windows updates will eventually undo Mode A

This is expected and acceptable for a diagnostic disk. Windows servicing,
**Startup Repair**, `bcdboot`, in-place upgrades and some cumulative updates
re-deploy Microsoft's own `bootmgfw.efi`, silently replacing GpuCheck and
restoring the normal boot path.

Symptom: the machine boots straight into Windows with no GPU screen.

Fix — just reinstall:

```powershell
.\scripts\install-gpucheck.ps1 -DiskNumber <N> -ExpectedModel 'JMicron' -ExpectedSizeGB 931.51
```

The installer handles this case explicitly: if it finds a *genuine* Microsoft
loader back at `bootmgfw.efi` while `bootmgfw-original.efi` already exists, it
refreshes the backup from it before reinstalling, so the preserved loader never
goes stale.

---

## Configuration

Optional, at `\EFI\GPUCHECK\gpucheck.cfg`. If absent, GpuCheck shows every
display adapter it found and uses built-in defaults.

```ini
EXPECT_VENDOR=10DE
EXPECT_DEVICE=2489        # or ANY for any GPU from that vendor
AUTOBOOT_WHEN_FOUND=1
AUTOBOOT_DELAY=3
HALT_WHEN_MISSING=1
LOG_ENABLE=1
#BOOT_PATH=\EFI\Microsoft\Boot\bootmgfw-original.efi
```

`KEY=VALUE`, one per line. `#` and `;` start a comment. Unknown keys are ignored.
Either `EXPECT_VENDOR` or `EXPECT_DEVICE` alone is enough to pin a card.

**Save it as ANSI or UTF-8 without a BOM.** A UTF-8 BOM is skipped automatically;
a UTF-16 file (what PowerShell redirection produces by default) cannot be parsed
and GpuCheck will say so on screen rather than silently ignoring your settings.

`AUTOBOOT_DELAY=0` is clamped to 1 second, so there is always a window in which
`R`/`P`/`ESC` can be pressed.

---

## Logging

If `LOG_ENABLE=1`, each scan is written to `\EFI\GPUCHECK\lastscan.txt`:

```text
2026-08-22 19:52:00
GpuCheck 1.0.0
pci_functions_seen=94
display_controllers=2
scan_source=pci_io

0000:00:02.0
8086:9BC8
03:00
type=Integrated

0000:01:00.0
10DE:2489
03:00
type=Discrete
PCIe Gen4 x16 (max Gen4 x16)
```

Logging is **best effort and never required**. Some firmware presents the ESP
read-only at this point; that is not an error and the on-screen result is
unaffected. The screen is the primary output.

---

## Building

### Standalone (MSVC — what this repo was built with)

```powershell
.\scripts\build.ps1 -Clean
```

Requires Visual Studio Build Tools with the C++ x64 toolset. No EDK II tree, no
NASM, no Python needed to compile (Python is used only for PE verification).

Produces `build\GpuCheck.efi`: a freestanding PE32+ x86-64 image, subsystem 10
(EFI application), **no CRT, no imports, no DLL dependents**.

Compiled with `/GS-` (no stack cookie), `/Gs1048576` (no stack probes), `/Zl`,
and linked `/NODEFAULTLIB /SUBSYSTEM:EFI_APPLICATION /ENTRY:EfiMain /FIXED:NO`.
`/DYNAMICBASE` is rejected outright with the EFI subsystem (LNK1295), so it is
explicitly `:NO`; relocations come from `/FIXED:NO`, which keeps the `.reloc`
section the firmware loader needs.

`memset` / `memcpy` / `memcmp` are supplied by `Compat/StandaloneUefi.c` because
MSVC emits calls to them even in a freestanding build.

### EDK II

The `.c` files are unmodified between the two builds. Drop the directory into an
EDK II workspace as `GpuCheckPkg/` and:

```sh
build -a X64 -t VS2022 -p GpuCheckPkg/GpuCheckPkg.dsc -b RELEASE
```

`GpuCheck.h` switches on `GPUCHECK_EDK2` (set by `GpuCheck.inf`) to include the
real MdePkg headers instead of the bundled `Compat/` ones. Everything in
`Compat/` is used *only* by the standalone build.

**Why the project ships both:** EDK II was the stated preference, but it could not
build on the development machine — it needs NASM (absent; `BaseLib` has mandatory
`X64/*.nasm` sources) and a `tools_def` entry for MSVC 14.50 / VS 2026, a
toolchain tag EDK II does not yet ship. Rather than gamble the deliverable on
that yak-shave, the sources were written in EDK II idiom against a thin compat
layer and linked with MSVC directly. The artifact is the same class of image
either way; the EDK II path stays available on any machine that has the tree.

---

## Scripts

| Script | Elevation | Purpose |
|---|---|---|
| `build.ps1` | no | Compile and verify `GpuCheck.efi` |
| `verify-pe.py` | no | Independently parse the PE headers and validate the image |
| `inspect-esp.ps1` | **yes** | **Read-only** ESP inspection. Writes nothing. |
| `install-gpucheck.ps1` | **yes** | Back up and install |
| `restore-gpucheck.ps1` | **yes** | Roll back |

### Safety design of the installer

Disk numbers are **not trusted**. They change: during development, the target was
disk 2, and after a single unplug/replug cycle disk 2 was a completely different
SSD. The installer therefore cross-checks, and hard-refuses on any mismatch:

- disk is not `IsBoot` and not `IsSystem`
- disk does not host this machine's `%SystemDrive%`
- bus type (default `USB`)
- model substring, serial substring, capacity (±1 GB)
- **ESP located by GPT type GUID `c12a7328-f81f-11d2-ba4b-00a0c93ec93b`**, never by size
- optional ESP partition GUID match — the strongest check available
- ESP filesystem must be FAT
- `\EFI\Microsoft\Boot` must exist

It then requires you to type the disk number back before writing anything.

It **never** formats, repartitions, initialises, cleans, rewrites the GPT,
resizes anything, or runs `diskpart clean`. The only changes are small file
copies inside the existing ESP.

The backup is **verified genuine before the original is overwritten** — by
Authenticode signer and version resource, not by a display string (modern
`bootmgfw.efi` keeps its UI text in `.mui` files, so matching on
`"Windows Boot Manager"` produces false negatives). If `bootmgfw.efi` is already
GpuCheck but `bootmgfw-original.efi` is missing, the installer refuses to touch
anything at all.

---

## Rollback

```powershell
.\scripts\restore-gpucheck.ps1 -DiskNumber <N>
```

Restores `bootmgfw-original.efi` over `bootmgfw.efi`, restores
`BOOTX64-BACKUP.EFI` if present, and touches nothing else. Timestamped
`bootmgfw-backup-*.efi` files are left in place. Add `-RemoveGpuCheckFiles` to
also delete `\EFI\GPUCHECK\`.

### Manual recovery

If the scripts are unavailable, from any elevated Windows shell:

```powershell
# 1. Find the ESP and give it a letter
Get-Partition -DiskNumber <N>
Add-PartitionAccessPath -DiskNumber <N> -PartitionNumber <ESP#> -AccessPath 'S:\'

# 2. Put the original loader back
Copy-Item S:\EFI\Microsoft\Boot\bootmgfw-original.efi `
          S:\EFI\Microsoft\Boot\bootmgfw.efi -Force

# 3. Unmount
Remove-PartitionAccessPath -DiskNumber <N> -PartitionNumber <ESP#> -AccessPath 'S:\'
```

### Last resort — no backup left

If `bootmgfw-original.efi` is gone, rebuild the boot files from the Windows
installation itself. Windows keeps a pristine copy at
`<WinDrive>\Windows\Boot\EFI\bootmgfw.efi`:

```powershell
bcdboot <WinDrive>:\Windows /s S: /f UEFI
```

The disk this was built for has that pristine copy at
`E:\Windows\Boot\EFI\bootmgfw.efi` (1,604,016 bytes).

Booting the SSD is also never required to recover it — put it back on a USB
adapter and run the restore script from any Windows machine.

---

## Failure modes and what they mean

| Screen | Meaning |
|---|---|
| `NO PCI DISPLAY CONTROLLERS DETECTED` | Not one class-03 device on any bus. Suspect the slot, riser, power, or a firmware that has not run PCI enumeration. |
| `WARNING: NO DISCRETE DISPLAY ADAPTER DETECTED` | Only integrated graphics enumerated. The card did not appear on PCI at all. |
| `WARNING: EXPECTED GPU xxxx:xxxx NOT FOUND` | Other adapters enumerated but not the pinned one. |
| `(found via root-bridge fallback scan, not PciIo)` | The PCI bus driver did not publish PciIo for the device, but it *is* present in config space. Unusual and diagnostically interesting. |
| `ERROR: Windows Boot Manager not found` | The chainload target is missing. GpuCheck lists every path it searched. Re-run the installer or restore. |

A device appearing with `10DE:xxxx`, class `03:00`, at `01:00.0` is proof the GPU
reached PCI enumeration — even if it produces no image.

---

## Headless / no-output-GPU use

The firmware console may be rendered by the motherboard iGPU or a second card
rather than by the GPU under test. GpuCheck never assumes the diagnostic GPU is
the one drawing the console — that separation is intentional and is what makes it
useful when the card under test outputs nothing at all.

If the platform exposes no console input device, GpuCheck does not crash: it
still runs the countdown and boots when a countdown was requested.

---

## Project layout

```text
GpuCheck/
├── GpuCheck.c / .h          main app: UI, layout, timeout, keys, boot flow
├── PciScan.c / .h           PCI enumeration, PCIe caps, iGPU/dGPU heuristic
├── WindowsBoot.c / .h       loader discovery + chainload, self-boot-loop guard
├── GpuConfig.c / .h         gpucheck.cfg parser
├── GpuLog.c / .h            lastscan.txt writer
├── Compat/                  standalone UEFI env (MSVC build only)
│   ├── StandaloneUefi.h     UEFI types, tables, protocols
│   └── StandaloneUefi.c     Print, pool/string helpers, FileDevicePath, memset
├── GpuCheck.inf             EDK II module
├── GpuCheckPkg.dsc          EDK II package
├── esp-staging/
│   └── gpucheck.cfg         sample config
├── scripts/
│   ├── build.ps1
│   ├── verify-pe.py
│   ├── inspect-esp.ps1
│   ├── install-gpucheck.ps1
│   └── restore-gpucheck.ps1
└── build/
    └── GpuCheck.efi
```

---

## Notes on the self-boot-loop guard

In Mode A, GpuCheck **is** `\EFI\Microsoft\Boot\bootmgfw.efi`. Chainloading that
path would restart GpuCheck forever, and the only recovery would be physically
pulling the disk. Two independent guards prevent it:

1. **Content check (authoritative).** Before chainloading, GpuCheck reads the
   candidate image and rejects it if it contains GpuCheck's own banner marker.
   This cannot be fooled by handle or path ambiguity.
2. **Identity check (fallback).** The candidate path is compared against
   GpuCheck's own reconstructed path on its own device handle.

The stock `bootmgfw.efi` is deliberately **last** in the search order, and is only
ever accepted when both guards agree it is not GpuCheck.

---

## Licence

BSD-2-Clause-Patent (the EDK II licence), per the file headers.
