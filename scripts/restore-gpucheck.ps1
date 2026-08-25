<#
.SYNOPSIS
    Removes GpuCheck and restores the original Windows Boot Manager.

.DESCRIPTION
    Copies \EFI\Microsoft\Boot\bootmgfw-original.efi back over bootmgfw.efi,
    and (optionally) \EFI\Boot\BOOTX64-BACKUP.EFI back over BOOTX64.EFI.

    It touches nothing else. No partition is created, deleted, resized or
    formatted; the BCD is not modified; timestamped backups are left in place.

    This script is deliberately self-contained so it can be copied to a USB
    stick and run from a recovery environment.

.PARAMETER DiskNumber
    Physical disk holding the ESP to repair.

.PARAMETER KeepGpuCheckFiles
    Leave \EFI\GPUCHECK\ in place (default is to leave it; pass
    -RemoveGpuCheckFiles to delete it).

.PARAMETER RemoveGpuCheckFiles
    Also delete \EFI\GPUCHECK\GpuCheck.efi, gpucheck.cfg and lastscan.txt.

.PARAMETER Force
    Skip the interactive confirmation prompt.

.EXAMPLE
    .\restore-gpucheck.ps1 -DiskNumber 2
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][int]$DiskNumber,
    [switch]$RemoveGpuCheckFiles,
    [switch]$Force,
    [string]$LogFile
)

$ErrorActionPreference = 'Stop'

if ($LogFile) { try { Start-Transcript -Path $LogFile -Force | Out-Null } catch {} }

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
function Abort($m) { Bad $m; throw $m }

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
function Test-IsGpuCheckImage { param([string]$P) return (Test-FileContainsUtf16 -Path $P -Needle 'GPU PCIe DIAGNOSTIC') }

# Genuine Microsoft loader. Matched on the Authenticode signer and on the
# non-localised version-resource fields - NOT on a display string, because
# modern bootmgfw.efi keeps its UI text in .mui files and ProductName is
# translated. See the same function in install-gpucheck.ps1.
function Test-IsWindowsBootManager {
    param([string]$P)

    if (-not (Test-Path -LiteralPath $P)) { return $false }
    if (Test-IsGpuCheckImage -Path $P)    { return $false }

    $len = (Get-Item -LiteralPath $P).Length
    if ($len -lt 200KB -or $len -gt 16MB) { return $false }

    try {
        $sig = Get-AuthenticodeSignature -LiteralPath $P -ErrorAction Stop
        if ($sig -and $sig.SignerCertificate) {
            $subject = "$($sig.SignerCertificate.Subject)"
            if ($subject -match 'Microsoft\s+Corporation' -or $subject -match 'CN=Microsoft') { return $true }
        }
    } catch {}

    try {
        $vi = (Get-Item -LiteralPath $P).VersionInfo
        if ($vi) {
            $comp = "$($vi.CompanyName)"
            if ("$($vi.OriginalFilename)" -match '^bootmgr(fw)?\.(exe|efi)$' -and $comp -match 'Microsoft') { return $true }
            if ("$($vi.InternalName)"     -match '^bootmgr'                  -and $comp -match 'Microsoft') { return $true }
        }
    } catch {}

    return $false
}
function Get-Sha256 { param([string]$P) return (Get-FileHash -LiteralPath $P -Algorithm SHA256).Hash }

function Dismount-Esp {
    if (-not $script:MountedAccessPath) { return }
    try {
        Remove-PartitionAccessPath -DiskNumber $script:MountedDisk `
                                   -PartitionNumber $script:MountedPartition `
                                   -AccessPath $script:MountedAccessPath -ErrorAction Stop
        Ok "ESP unmounted from $($script:MountedAccessPath)"
    } catch {
        Warn "Remove the access path manually: mountvol $($script:MountedAccessPath) /D"
    }
    $script:MountedAccessPath = $null
}

# ==========================================================================
try {

Step 'Environment'
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) { Abort 'Administrator rights are required. Re-run from an elevated PowerShell.' }
Ok 'Running elevated'

Step 'Target disk'
$disk = Get-Disk -Number $DiskNumber -ErrorAction SilentlyContinue
if (-not $disk) { Abort "No physical disk with number $DiskNumber." }

Say "  Model  : $($disk.FriendlyName)"
Say "  Serial : $($disk.SerialNumber)"
Say "  Size   : $([math]::Round($disk.Size/1GB,2)) GB"
Say "  Bus    : $($disk.BusType)"

if ($disk.IsBoot)   { Abort 'Target disk is the BOOT disk of this machine. Refusing.' }
if ($disk.IsSystem) { Abort 'Target disk is the SYSTEM disk of this machine. Refusing.' }
Ok 'Target is not this machine''s boot/system disk'

$esp = Get-Partition -DiskNumber $DiskNumber | Where-Object { $_.GptType -eq $ESP_TYPE_GUID }
if (-not $esp)        { Abort 'No EFI System Partition on this disk.' }
if ($esp -is [array]) { Abort 'Multiple EFI System Partitions found. Refusing to guess.' }
Ok "ESP is partition $($esp.PartitionNumber), GUID $($esp.Guid)"

Step 'Mounting ESP (temporary)'
$used = [System.IO.DriveInfo]::GetDrives() | ForEach-Object { $_.Name.Substring(0,1).ToUpper() }
$letter = $null
foreach ($c in [char[]]'STUVWXYZQRNOPKLM') {
    if ($used -notcontains "$c") { $letter = "$c"; break }
}
if (-not $letter) { Abort 'No free drive letter available.' }
$mountPath = "${letter}:\"

Add-PartitionAccessPath -DiskNumber $DiskNumber -PartitionNumber $esp.PartitionNumber -AccessPath $mountPath
$script:MountedAccessPath = $mountPath
$script:MountedDisk       = $DiskNumber
$script:MountedPartition  = $esp.PartitionNumber
Start-Sleep -Milliseconds 700
Ok "ESP mounted at $mountPath"

$espRoot    = "${letter}:"
$msDir      = Join-Path $espRoot 'EFI\Microsoft\Boot'
$bootmgfw   = Join-Path $msDir 'bootmgfw.efi'
$original   = Join-Path $msDir 'bootmgfw-original.efi'
$bootDir    = Join-Path $espRoot 'EFI\Boot'
$bootx64    = Join-Path $bootDir 'BOOTX64.EFI'
$bootx64Bak = Join-Path $bootDir 'BOOTX64-BACKUP.EFI'
$gpuDir     = Join-Path $espRoot 'EFI\GPUCHECK'

Step 'Current state'
Say "  bootmgfw.efi          : $(if (Test-Path $bootmgfw) { "$((Get-Item $bootmgfw).Length) bytes, GpuCheck=$(Test-IsGpuCheckImage $bootmgfw)" } else { 'ABSENT' })"
Say "  bootmgfw-original.efi : $(if (Test-Path $original) { "$((Get-Item $original).Length) bytes, WinBootMgr=$(Test-IsWindowsBootManager $original)" } else { 'absent' })"
Say "  BOOTX64.EFI           : $(if (Test-Path $bootx64) { "$((Get-Item $bootx64).Length) bytes, GpuCheck=$(Test-IsGpuCheckImage $bootx64)" } else { 'absent' })"
Say "  BOOTX64-BACKUP.EFI    : $(if (Test-Path $bootx64Bak) { "$((Get-Item $bootx64Bak).Length) bytes" } else { 'absent' })"

$plan = @()
if (Test-Path -LiteralPath $original) {
    if (Test-IsWindowsBootManager -Path $original) {
        $plan += 'restore bootmgfw-original.efi -> bootmgfw.efi'
    } else {
        Abort 'bootmgfw-original.efi is not a genuine Windows Boot Manager. Refusing to restore it. Use: bcdboot <WinDrive>\Windows /s <ESP> /f UEFI'
    }
} else {
    if (Test-IsGpuCheckImage -Path $bootmgfw) {
        Abort 'bootmgfw.efi is GpuCheck but there is no backup to restore. Recover with: bcdboot <WinDrive>\Windows /s <ESP> /f UEFI'
    }
    Warn 'No bootmgfw-original.efi present - Mode A was not installed, or Windows already restored its own loader.'
}

if ((Test-Path -LiteralPath $bootx64) -and (Test-IsGpuCheckImage -Path $bootx64)) {
    if (Test-Path -LiteralPath $bootx64Bak) {
        $plan += 'restore BOOTX64-BACKUP.EFI -> BOOTX64.EFI'
    } else {
        $plan += 'delete BOOTX64.EFI (it is GpuCheck and there was no original)'
    }
}
if ($RemoveGpuCheckFiles -and (Test-Path -LiteralPath $gpuDir)) {
    $plan += 'delete \EFI\GPUCHECK\'
}

if ($plan.Count -eq 0) {
    Ok 'Nothing to restore - this ESP has no GpuCheck installation.'
    $script:ExitCode = 0
} else {
    Step 'Plan'
    foreach ($p in $plan) { Say "    - $p" }
    Say ''
    Say '  Timestamped bootmgfw-backup-*.efi files are left untouched.'

    if (-not $Force) {
        $answer = Read-Host "  Type RESTORE to proceed"
        if ($answer -ne 'RESTORE') { Abort 'Aborted by operator.' }
    }

    Step 'Restoring'

    if (Test-Path -LiteralPath $original) {
        Copy-Item -LiteralPath $original -Destination $bootmgfw -Force
        if ((Get-Sha256 $bootmgfw) -ne (Get-Sha256 $original)) { Abort 'Restore verification failed: hash mismatch.' }
        if (-not (Test-IsWindowsBootManager -Path $bootmgfw))   { Abort 'Restore verification failed: bootmgfw.efi is not a Windows Boot Manager.' }
        Ok "bootmgfw.efi restored ($((Get-Item $bootmgfw).Length) bytes)"

        Remove-Item -LiteralPath $original -Force
        Ok 'bootmgfw-original.efi removed (its content is now bootmgfw.efi)'
    }

    if ((Test-Path -LiteralPath $bootx64) -and (Test-IsGpuCheckImage -Path $bootx64)) {
        if (Test-Path -LiteralPath $bootx64Bak) {
            Copy-Item -LiteralPath $bootx64Bak -Destination $bootx64 -Force
            if ((Get-Sha256 $bootx64) -ne (Get-Sha256 $bootx64Bak)) { Abort 'Restore verification failed for BOOTX64.EFI.' }
            Remove-Item -LiteralPath $bootx64Bak -Force
            Ok 'BOOTX64.EFI restored from backup'
        } else {
            Remove-Item -LiteralPath $bootx64 -Force
            Ok 'BOOTX64.EFI (GpuCheck) removed - there was no original to restore'
        }
    }

    if ($RemoveGpuCheckFiles -and (Test-Path -LiteralPath $gpuDir)) {
        Remove-Item -LiteralPath $gpuDir -Recurse -Force
        Ok '\EFI\GPUCHECK\ removed'
    }

    Step 'Resulting ESP layout'
    Get-ChildItem -LiteralPath $espRoot -Recurse -File -ErrorAction SilentlyContinue |
        Sort-Object FullName |
        ForEach-Object { Say ("  {0,10}  {1}" -f $_.Length, $_.FullName.Substring($espRoot.Length)) }
}

} catch {
    Bad $_.Exception.Message
    $script:ExitCode = 1
} finally {
    Step 'Cleanup'
    Dismount-Esp
    if ($LogFile) { try { Stop-Transcript | Out-Null } catch {} }
}

if ($script:ExitCode -eq 0) {
    Write-Host "`nRESTORE COMPLETE" -ForegroundColor Green
} else {
    Write-Host "`nRESTORE FAILED" -ForegroundColor Red
}
exit $script:ExitCode
