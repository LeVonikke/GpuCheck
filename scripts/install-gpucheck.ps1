<#
.SYNOPSIS
    Installs GpuCheck.efi onto the EFI System Partition of a specific,
    positively identified physical disk.

.DESCRIPTION
    Performs only small file operations inside the existing ESP. It never
    formats, repartitions, initialises, cleans or rewrites the GPT, and it
    refuses to run at all unless the target disk matches every identity
    check it was given.

    Mode A - Windows Boot Manager interception (default, most reliable):
        \EFI\Microsoft\Boot\bootmgfw.efi is backed up to
        bootmgfw-original.efi plus a timestamped copy, then replaced by
        GpuCheck.efi. GpuCheck chainloads bootmgfw-original.efi.
        Firmware picks "Windows Boot Manager" -> GpuCheck -> Windows.

    Mode B - UEFI fallback path:
        \EFI\GPUCHECK\GpuCheck.efi and \EFI\BOOT\BOOTX64.EFI (the existing
        BOOTX64.EFI is backed up first).
        Firmware picks the generic UEFI disk entry -> GpuCheck -> Windows.

    The backup is always verified before the original loader is overwritten.

.PARAMETER DiskNumber
    Physical disk number to target. Required, and cross-checked against the
    other Expected* parameters - the number alone is never trusted, because
    disk enumeration can change between boots.

.PARAMETER ExpectedModel
    Substring that must appear in the disk's FriendlyName.

.PARAMETER ExpectedSizeGB
    Disk size in GB, matched within a 1 GB tolerance.

.PARAMETER ExpectedSerial
    Substring that must appear in the disk's SerialNumber.

.PARAMETER ExpectedEspGuid
    GPT partition GUID of the ESP, e.g. {b14ae41b-...}. The strongest check
    available: it is unique to this partition on this disk.

.PARAMETER RequireBusType
    Bus type the disk must report. Defaults to USB.

.PARAMETER Mode
    A, B, or Both. Default Both.

.PARAMETER DryRun
    Perform every check and report the plan, but write nothing.

.PARAMETER Force
    Skip the interactive confirmation prompt.

.PARAMETER AcceptBitLockerRisk
    Proceed even if the Windows volume on the target disk is BitLocker
    protected. Altering the boot chain can trigger a recovery-key prompt.

.EXAMPLE
    .\install-gpucheck.ps1 -DiskNumber 2 -ExpectedModel 'JMicron' -ExpectedSizeGB 931.51 -DryRun
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][int]$DiskNumber,
    [string]$ExpectedModel,
    [double]$ExpectedSizeGB = 0,
    [string]$ExpectedSerial,
    [string]$ExpectedEspGuid,
    [string]$RequireBusType = 'USB',
    [ValidateSet('A', 'B', 'Both')][string]$Mode = 'Both',
    [string]$EfiSource,
    [string]$ConfigSource,
    [switch]$DryRun,
    [switch]$Force,
    [switch]$AcceptBitLockerRisk,
    [string]$LogFile
)

$ErrorActionPreference = 'Stop'

if ($LogFile) {
    try { Start-Transcript -Path $LogFile -Force | Out-Null } catch {}
}

# GPT type GUID of an EFI System Partition. This, not partition size, is what
# identifies the ESP.
$ESP_TYPE_GUID = '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'

$script:MountedAccessPath = $null
$script:MountedDisk       = $null
$script:MountedPartition  = $null
$script:ExitCode          = 0

function Say  ($m) { Write-Host $m }
function Step ($m) { Write-Host "`n=== $m ===" -ForegroundColor Cyan }
function Ok   ($m) { Write-Host "  [ok]   $m" -ForegroundColor Green }
function Warn ($m) { Write-Host "  [warn] $m" -ForegroundColor Yellow }
function Bad  ($m) { Write-Host "  [FAIL] $m" -ForegroundColor Red }

function Abort($m) {
    Bad $m
    throw $m
}

# --------------------------------------------------------------------------
# Byte-pattern search, used to tell a genuine Windows Boot Manager apart from
# GpuCheck. Comparing file sizes alone is not good enough when the whole point
# is to avoid overwriting the real loader with our own backup of ourselves.
# --------------------------------------------------------------------------
function Test-FileContainsUtf16 {
    param([string]$Path, [string]$Needle)

    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    $bytes  = [System.IO.File]::ReadAllBytes($Path)
    $needleBytes = [System.Text.Encoding]::Unicode.GetBytes($Needle)
    if ($needleBytes.Length -eq 0 -or $bytes.Length -lt $needleBytes.Length) { return $false }

    $last = $bytes.Length - $needleBytes.Length
    for ($i = 0; $i -le $last; $i++) {
        if ($bytes[$i] -ne $needleBytes[0]) { continue }
        $match = $true
        for ($j = 1; $j -lt $needleBytes.Length; $j++) {
            if ($bytes[$i + $j] -ne $needleBytes[$j]) { $match = $false; break }
        }
        if ($match) { return $true }
    }
    return $false
}

function Test-IsGpuCheckImage {
    param([string]$Path)
    return (Test-FileContainsUtf16 -Path $Path -Needle 'GPU PCIe DIAGNOSTIC')
}

<#
    Identify a genuine Microsoft Windows Boot Manager.

    Do NOT match on a display string such as "Windows Boot Manager": modern
    bootmgfw.efi keeps its UI text in the .mui resource files, so that string
    is simply not in the executable, and ProductName in the version resource
    is localised. Both approaches produce false negatives that would abort
    the install.

    What is actually stable:
      * an Authenticode signature whose signer is Microsoft, and
      * a version resource with OriginalFilename "bootmgr.exe" and
        CompanyName "Microsoft Corporation" (neither is localised).

    Either is accepted, so an ESP mounted where signature chaining is
    unavailable still validates.
#>
function Test-IsWindowsBootManager {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    if (Test-IsGpuCheckImage -Path $Path)    { return $false }

    $len = (Get-Item -LiteralPath $Path).Length
    if ($len -lt 200KB -or $len -gt 16MB) { return $false }

    # 1. Authenticode signer.
    try {
        $sig = Get-AuthenticodeSignature -LiteralPath $Path -ErrorAction Stop
        if ($sig -and $sig.SignerCertificate) {
            $subject = "$($sig.SignerCertificate.Subject)"
            if ($subject -match 'Microsoft\s+Corporation' -or $subject -match 'CN=Microsoft') {
                return $true
            }
        }
    } catch {}

    # 2. Version resource (locale-independent fields only).
    try {
        $vi = (Get-Item -LiteralPath $Path).VersionInfo
        if ($vi) {
            $orig = "$($vi.OriginalFilename)"
            $comp = "$($vi.CompanyName)"
            if ($orig -match '^bootmgr(fw)?\.(exe|efi)$' -and $comp -match 'Microsoft') {
                return $true
            }
            if ($vi.InternalName -match '^bootmgr' -and $comp -match 'Microsoft') {
                return $true
            }
        }
    } catch {}

    return $false
}

function Get-Sha256 {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Copy-Verified {
    param([string]$From, [string]$To, [string]$What)

    if ($DryRun) { Say "  [dry]  would copy $What -> $To"; return $true }

    $dir = Split-Path -Parent $To
    if ($dir -and -not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
    Copy-Item -LiteralPath $From -Destination $To -Force

    if (-not (Test-Path -LiteralPath $To)) { Abort "$What : destination missing after copy" }

    $srcHash = Get-Sha256 $From
    $dstHash = Get-Sha256 $To
    $dstLen  = (Get-Item -LiteralPath $To).Length

    if ($srcHash -ne $dstHash) { Abort "$What : SHA256 mismatch after copy" }
    if ($dstLen -eq 0)         { Abort "$What : destination is zero bytes" }

    Ok "$What -> $To  ($dstLen bytes, SHA256 $($dstHash.Substring(0,16))...)"
    return $true
}

function Dismount-Esp {
    if (-not $script:MountedAccessPath) { return }
    try {
        Remove-PartitionAccessPath -DiskNumber $script:MountedDisk `
                                   -PartitionNumber $script:MountedPartition `
                                   -AccessPath $script:MountedAccessPath -ErrorAction Stop
        Ok "ESP unmounted from $($script:MountedAccessPath)"
    } catch {
        Warn "Could not remove access path $($script:MountedAccessPath): $($_.Exception.Message)"
        Warn "Remove it manually with: mountvol $($script:MountedAccessPath) /D"
    }
    $script:MountedAccessPath = $null
}

# ==========================================================================
try {

Step 'Environment'

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Abort 'Administrator rights are required to mount the EFI System Partition. Re-run this script from an elevated PowerShell.'
}
Ok 'Running elevated'

if (-not $EfiSource) {
    $EfiSource = Join-Path (Split-Path -Parent $PSScriptRoot) 'build\GpuCheck.efi'
}
if (-not (Test-Path -LiteralPath $EfiSource)) {
    Abort "GpuCheck.efi not found at: $EfiSource  (run scripts\build.ps1 first)"
}
$efiInfo = Get-Item -LiteralPath $EfiSource
Ok "Source image: $($efiInfo.FullName) ($($efiInfo.Length) bytes)"

if (-not (Test-IsGpuCheckImage -Path $EfiSource)) {
    Abort "$EfiSource does not look like a GpuCheck build (marker string absent). Refusing to install it."
}
Ok 'Source image identified as GpuCheck'

if (-not $ConfigSource) {
    $candidate = Join-Path (Split-Path -Parent $PSScriptRoot) 'esp-staging\gpucheck.cfg'
    if (Test-Path -LiteralPath $candidate) { $ConfigSource = $candidate }
}

# --------------------------------------------------------------------------
Step 'Target disk identification'

$disk = Get-Disk -Number $DiskNumber -ErrorAction SilentlyContinue
if (-not $disk) { Abort "No physical disk with number $DiskNumber." }

$sizeGB = [math]::Round($disk.Size / 1GB, 2)
Say "  Disk number   : $($disk.Number)"
Say "  Model         : $($disk.FriendlyName)"
Say "  Serial        : $($disk.SerialNumber)"
Say "  Size          : $sizeGB GB"
Say "  Bus type      : $($disk.BusType)"
Say "  Partition tbl : $($disk.PartitionStyle)"
Say "  IsBoot        : $($disk.IsBoot)"
Say "  IsSystem      : $($disk.IsSystem)"

# --- Hard refusals. None of these are overridable. ---
if ($disk.IsBoot)   { Abort 'Target disk is the BOOT disk of this machine. Refusing.' }
if ($disk.IsSystem) { Abort 'Target disk is the SYSTEM disk of this machine. Refusing.' }
if ($disk.PartitionStyle -ne 'GPT') { Abort "Target disk is $($disk.PartitionStyle), not GPT. Refusing." }

$winDrive = ($env:SystemDrive).TrimEnd(':')
$hostDiskNumbers = @(Get-Partition -ErrorAction SilentlyContinue |
                     Where-Object { $_.DriveLetter -eq $winDrive } |
                     Select-Object -ExpandProperty DiskNumber)
if ($hostDiskNumbers -contains $DiskNumber) {
    Abort "Target disk hosts this machine's $($env:SystemDrive) volume. Refusing."
}
Ok 'Target is not this machine''s boot/system disk'

# --- Identity cross-checks ---
if ($RequireBusType -and $disk.BusType -ne $RequireBusType) {
    Abort "Bus type is '$($disk.BusType)', expected '$RequireBusType'. Refusing (use -RequireBusType '' to relax)."
}
if ($ExpectedModel -and ($disk.FriendlyName -notlike "*$ExpectedModel*")) {
    Abort "Model '$($disk.FriendlyName)' does not contain '$ExpectedModel'. Refusing."
}
if ($ExpectedSerial -and ($disk.SerialNumber -notlike "*$ExpectedSerial*")) {
    Abort "Serial '$($disk.SerialNumber)' does not contain '$ExpectedSerial'. Refusing."
}
if ($ExpectedSizeGB -gt 0 -and ([math]::Abs($sizeGB - $ExpectedSizeGB) -gt 1.0)) {
    Abort "Size $sizeGB GB does not match expected $ExpectedSizeGB GB. Refusing."
}
Ok 'All disk identity checks passed'

# --------------------------------------------------------------------------
Step 'EFI System Partition'

$parts = Get-Partition -DiskNumber $DiskNumber | Sort-Object PartitionNumber
Say '  Partition table:'
foreach ($p in $parts) {
    $letter = if ($p.DriveLetter) { "$($p.DriveLetter):" } else { '  ' }
    Say ("    {0}  {1}  {2,10:N2} GB  {3}" -f $p.PartitionNumber, $letter, ($p.Size / 1GB), $p.GptType)
}

# Identified by GPT type GUID, never by size.
$esp = $parts | Where-Object { $_.GptType -eq $ESP_TYPE_GUID }
if (-not $esp)             { Abort 'No EFI System Partition (GPT type c12a7328-...) on this disk.' }
if ($esp -is [array])      { Abort "Found $($esp.Count) EFI System Partitions on this disk. Refusing to guess." }
Ok "ESP is partition $($esp.PartitionNumber), GUID $($esp.Guid)"

if ($ExpectedEspGuid -and ($esp.Guid -ne $ExpectedEspGuid)) {
    Abort "ESP partition GUID $($esp.Guid) does not match expected $ExpectedEspGuid. Refusing."
}

# --------------------------------------------------------------------------
Step 'BitLocker check'

$winPart = $parts | Where-Object { $_.DriveLetter } | Select-Object -First 1
if ($winPart) {
    $mp = "$($winPart.DriveLetter):"
    $bl = $null
    try { $bl = Get-BitLockerVolume -MountPoint $mp -ErrorAction Stop } catch {}
    if ($bl) {
        Say "  $mp protection: $($bl.ProtectionStatus), encryption: $($bl.EncryptionMethod), lock: $($bl.LockStatus)"
        if ($bl.ProtectionStatus -eq 'On') {
            Warn 'The Windows volume on this disk is BitLocker protected.'
            Warn 'Changing the boot chain can make Windows ask for the recovery key on next boot.'
            if (-not $AcceptBitLockerRisk) {
                Abort 'Re-run with -AcceptBitLockerRisk once you have the recovery key to hand.'
            }
            Warn 'Continuing because -AcceptBitLockerRisk was given.'
        } else {
            Ok 'BitLocker protection is off on the Windows volume'
        }
    } else {
        Warn "Could not query BitLocker for $mp. Nothing will be encrypted or decrypted by this script."
    }
}

# --------------------------------------------------------------------------
Step 'Confirmation'

Say ''
Say '  About to modify ONLY these files, inside the existing ESP:'
if ($Mode -eq 'A' -or $Mode -eq 'Both') {
    Say '    \EFI\Microsoft\Boot\bootmgfw-original.efi        (create - backup)'
    Say '    \EFI\Microsoft\Boot\bootmgfw-backup-<stamp>.efi  (create - backup)'
    Say '    \EFI\Microsoft\Boot\bootmgfw.efi                 (replace with GpuCheck)'
}
if ($Mode -eq 'B' -or $Mode -eq 'Both') {
    Say '    \EFI\GPUCHECK\GpuCheck.efi                       (create)'
    Say '    \EFI\GPUCHECK\gpucheck.cfg                       (create if absent)'
    Say '    \EFI\Boot\BOOTX64-BACKUP.EFI                     (create - backup)'
    Say '    \EFI\Boot\BOOTX64.EFI                            (replace with GpuCheck)'
}
Say ''
Say "  Target: disk $DiskNumber  '$($disk.FriendlyName)'  $sizeGB GB  ($($disk.BusType))"
Say "  ESP   : partition $($esp.PartitionNumber)  GUID $($esp.Guid)"
Say ''
Say '  No partition will be created, deleted, resized or formatted.'
Say ''

if (-not $Force -and -not $DryRun) {
    $answer = Read-Host "  Type the disk number ($DiskNumber) to proceed, anything else to abort"
    if ($answer -ne "$DiskNumber") { Abort 'Aborted by operator.' }
    Ok 'Confirmed'
} elseif ($Force) {
    Warn '-Force given: skipping interactive confirmation'
}

# --------------------------------------------------------------------------
Step 'Mounting ESP (temporary)'

$used = [System.IO.DriveInfo]::GetDrives() | ForEach-Object { $_.Name.Substring(0,1).ToUpper() }
$letter = $null
foreach ($c in [char[]]'STUVWXYZQRNOPKLM') {
    if ($used -notcontains "$c") { $letter = "$c"; break }
}
if (-not $letter) { Abort 'No free drive letter available to mount the ESP.' }
$mountPath = "${letter}:\"

if ($DryRun) {
    Say "  [dry]  would mount ESP at $mountPath"
} else {
    Add-PartitionAccessPath -DiskNumber $DiskNumber -PartitionNumber $esp.PartitionNumber -AccessPath $mountPath
    $script:MountedAccessPath = $mountPath
    $script:MountedDisk       = $DiskNumber
    $script:MountedPartition  = $esp.PartitionNumber
    Start-Sleep -Milliseconds 700
    Ok "ESP mounted at $mountPath"
}

$espRoot = if ($DryRun) { $null } else { "${letter}:" }

if (-not $DryRun) {
    $vol = Get-Volume -DriveLetter $letter -ErrorAction SilentlyContinue
    if (-not $vol) { Abort 'ESP volume did not appear after mounting.' }
    Say "  Filesystem: $($vol.FileSystemType)  size: $([math]::Round($vol.Size/1MB,1)) MB  free: $([math]::Round($vol.SizeRemaining/1MB,1)) MB"
    if ($vol.FileSystemType -notmatch 'FAT') {
        Abort "ESP filesystem is '$($vol.FileSystemType)', expected FAT32. Refusing."
    }
    Ok 'ESP filesystem is FAT'

    if ($vol.SizeRemaining -lt ($efiInfo.Length * 4 + 2MB)) {
        Abort "Not enough free space on the ESP ($([math]::Round($vol.SizeRemaining/1MB,1)) MB free)."
    }
}

# --------------------------------------------------------------------------
Step 'Pre-install inspection'

if ($DryRun) {
    Warn 'Dry run: ESP contents cannot be inspected without mounting. Re-run without -DryRun to proceed.'
} else {
    $msDir      = Join-Path $espRoot 'EFI\Microsoft\Boot'
    $bootmgfw   = Join-Path $msDir 'bootmgfw.efi'
    $original   = Join-Path $msDir 'bootmgfw-original.efi'
    $bootDir    = Join-Path $espRoot 'EFI\Boot'
    $bootx64    = Join-Path $bootDir 'BOOTX64.EFI'
    $bootx64Bak = Join-Path $bootDir 'BOOTX64-BACKUP.EFI'
    $gpuDir     = Join-Path $espRoot 'EFI\GPUCHECK'

    Say '  Existing ESP layout:'
    Get-ChildItem -LiteralPath $espRoot -Recurse -File -ErrorAction SilentlyContinue |
        ForEach-Object {
            Say ("    {0,10}  {1}" -f $_.Length, $_.FullName.Substring($espRoot.Length))
        }

    if (-not (Test-Path -LiteralPath $msDir)) {
        Abort "\EFI\Microsoft\Boot does not exist on this ESP. This does not look like a Windows install. Refusing."
    }
    Ok '\EFI\Microsoft\Boot present'

    if (-not (Test-Path -LiteralPath $bootmgfw)) {
        Abort '\EFI\Microsoft\Boot\bootmgfw.efi is missing. Refusing to proceed.'
    }

    $bootmgfwIsGpuCheck = Test-IsGpuCheckImage -Path $bootmgfw
    $originalExists     = Test-Path -LiteralPath $original

    Say "  bootmgfw.efi          : $((Get-Item $bootmgfw).Length) bytes  (GpuCheck: $bootmgfwIsGpuCheck)"
    Say "  bootmgfw-original.efi : $(if ($originalExists) { "$((Get-Item $original).Length) bytes" } else { 'absent' })"

    # ----------------------------------------------------------------------
    Step 'Mode A - back up the Windows Boot Manager'

    $doModeA = ($Mode -eq 'A' -or $Mode -eq 'Both')

    if ($doModeA) {
        if ($originalExists) {
            if (-not (Test-IsWindowsBootManager -Path $original)) {
                Abort "$original exists but is not a genuine Windows Boot Manager. Refusing to touch anything - inspect this ESP by hand."
            }
            Ok 'bootmgfw-original.efi already present and verified genuine - keeping it'
            if (-not $bootmgfwIsGpuCheck -and (Test-IsWindowsBootManager -Path $bootmgfw)) {
                Warn 'Windows appears to have restored its own bootmgfw.efi (servicing/Startup Repair).'
                Warn 'Refreshing bootmgfw-original.efi from it before reinstalling.'
                $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
                Copy-Verified -From $bootmgfw -To (Join-Path $msDir "bootmgfw-backup-$stamp.efi") -What 'Timestamped backup' | Out-Null
                Copy-Verified -From $bootmgfw -To $original -What 'Refreshed bootmgfw-original.efi' | Out-Null
            }
        } else {
            if ($bootmgfwIsGpuCheck) {
                Abort 'bootmgfw.efi is already GpuCheck but bootmgfw-original.efi is MISSING. The genuine Windows loader is not on this ESP. Refusing to overwrite anything. Recover it with: bcdboot E:\Windows /s <ESP> /f UEFI'
            }
            if (-not (Test-IsWindowsBootManager -Path $bootmgfw)) {
                Abort 'bootmgfw.efi does not look like a genuine Windows Boot Manager. Refusing to proceed.'
            }
            Ok 'bootmgfw.efi verified as a genuine Windows Boot Manager'

            $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
            Copy-Verified -From $bootmgfw -To $original -What 'Backup (chainload target)' | Out-Null
            Copy-Verified -From $bootmgfw -To (Join-Path $msDir "bootmgfw-backup-$stamp.efi") -What 'Timestamped backup' | Out-Null
        }

        # Re-verify the backup independently before overwriting the original.
        if (-not (Test-Path -LiteralPath $original)) { Abort 'Backup verification failed: bootmgfw-original.efi absent.' }
        if (-not (Test-IsWindowsBootManager -Path $original)) { Abort 'Backup verification failed: bootmgfw-original.efi is not a Windows Boot Manager.' }
        $origLen = (Get-Item -LiteralPath $original).Length
        if ($origLen -lt 200KB) { Abort "Backup verification failed: bootmgfw-original.efi is only $origLen bytes." }
        Ok "Backup verified: bootmgfw-original.efi, $origLen bytes"

        Step 'Mode A - install GpuCheck as bootmgfw.efi'
        Copy-Verified -From $EfiSource -To $bootmgfw -What 'GpuCheck (Mode A)' | Out-Null
    } else {
        Say '  Mode A skipped.'
    }

    # ----------------------------------------------------------------------
    Step 'Mode B - fallback boot path'

    $doModeB = ($Mode -eq 'B' -or $Mode -eq 'Both')

    if ($doModeB) {
        Copy-Verified -From $EfiSource -To (Join-Path $gpuDir 'GpuCheck.efi') -What 'GpuCheck (canonical copy)' | Out-Null

        if (Test-Path -LiteralPath $bootx64) {
            if (Test-IsGpuCheckImage -Path $bootx64) {
                Ok 'BOOTX64.EFI is already GpuCheck - refreshing, backup left untouched'
            } elseif (Test-Path -LiteralPath $bootx64Bak) {
                Warn 'BOOTX64-BACKUP.EFI already exists - keeping the original backup'
            } else {
                Copy-Verified -From $bootx64 -To $bootx64Bak -What 'Backup of existing BOOTX64.EFI' | Out-Null
            }
        } else {
            Say '  No existing \EFI\Boot\BOOTX64.EFI - nothing to back up.'
        }

        Copy-Verified -From $EfiSource -To $bootx64 -What 'GpuCheck (Mode B fallback)' | Out-Null
    } else {
        Say '  Mode B skipped.'
    }

    # ----------------------------------------------------------------------
    Step 'Configuration file'

    $cfgTarget = Join-Path $gpuDir 'gpucheck.cfg'
    if (Test-Path -LiteralPath $cfgTarget) {
        Ok 'gpucheck.cfg already present - left unchanged'
    } elseif ($ConfigSource -and (Test-Path -LiteralPath $ConfigSource)) {
        Copy-Verified -From $ConfigSource -To $cfgTarget -What 'gpucheck.cfg' | Out-Null
    } else {
        Say '  No gpucheck.cfg supplied - GpuCheck will use built-in defaults.'
    }

    # ----------------------------------------------------------------------
    Step 'Post-install verification'

    $problems = @()

    if ($doModeA) {
        if (-not (Test-IsGpuCheckImage -Path $bootmgfw))       { $problems += 'bootmgfw.efi is not GpuCheck' }
        if (-not (Test-IsWindowsBootManager -Path $original))  { $problems += 'bootmgfw-original.efi is not a Windows Boot Manager' }
        if ((Get-Sha256 $bootmgfw) -ne (Get-Sha256 $EfiSource)) { $problems += 'bootmgfw.efi does not match the built image' }
    }
    if ($doModeB) {
        if (-not (Test-IsGpuCheckImage -Path $bootx64))                       { $problems += 'BOOTX64.EFI is not GpuCheck' }
        if (-not (Test-IsGpuCheckImage -Path (Join-Path $gpuDir 'GpuCheck.efi'))) { $problems += 'EFI\GPUCHECK\GpuCheck.efi missing' }
    }

    if ($problems.Count -gt 0) {
        foreach ($p in $problems) { Bad $p }
        Abort 'Post-install verification failed. Run scripts\restore-gpucheck.ps1 to roll back.'
    }
    Ok 'All installed files verified'

    Step 'Resulting ESP layout'
    Get-ChildItem -LiteralPath $espRoot -Recurse -File -ErrorAction SilentlyContinue |
        Sort-Object FullName |
        ForEach-Object {
            Say ("  {0,10}  {1}" -f $_.Length, $_.FullName.Substring($espRoot.Length))
        }
}

$script:ExitCode = 0

} catch {
    Bad $_.Exception.Message
    $script:ExitCode = 1
} finally {
    Step 'Cleanup'
    Dismount-Esp
    if ($LogFile) { try { Stop-Transcript | Out-Null } catch {} }
}

if ($script:ExitCode -eq 0) {
    Write-Host "`nINSTALL COMPLETE" -ForegroundColor Green
} else {
    Write-Host "`nINSTALL FAILED - nothing further was changed" -ForegroundColor Red
}
exit $script:ExitCode
