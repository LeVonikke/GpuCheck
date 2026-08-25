<#
.SYNOPSIS
    Read-only inspection of a disk's EFI System Partition.

.DESCRIPTION
    Mounts the ESP temporarily, lists its contents, identifies each EFI
    executable it finds, reports BitLocker state, then unmounts.

    Writes nothing. Creates nothing. Deletes nothing.
    Use this before install-gpucheck.ps1 to confirm the layout is what you
    expect.

.EXAMPLE
    .\inspect-esp.ps1 -DiskNumber 2
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][int]$DiskNumber,
    [string]$LogFile
)

$ErrorActionPreference = 'Stop'
if ($LogFile) { try { Start-Transcript -Path $LogFile -Force | Out-Null } catch {} }

$ESP_TYPE_GUID = '{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}'

$script:MountedAccessPath = $null
$script:MountedDisk       = $null
$script:MountedPartition  = $null

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

function Get-PeInfo {
    param([string]$Path)
    $r = [ordered]@{ Kind = 'unknown'; Subsystem = ''; Machine = '' }
    try {
        $b = [System.IO.File]::ReadAllBytes($Path)
        if ($b.Length -lt 0x40 -or $b[0] -ne 0x4D -or $b[1] -ne 0x5A) { $r.Kind = 'not PE'; return $r }
        $pe = [BitConverter]::ToInt32($b, 0x3C)
        if ($pe + 0x60 -ge $b.Length) { $r.Kind = 'truncated'; return $r }
        $mach = [BitConverter]::ToUInt16($b, $pe + 4)
        $optOff = $pe + 24
        $sub  = [BitConverter]::ToUInt16($b, $optOff + 68)
        $r.Machine = '0x{0:X4}' -f $mach
        $r.Subsystem = switch ($sub) { 10 { 'EFI_APPLICATION' } 11 { 'EFI_BOOT_DRIVER' } 12 { 'EFI_RUNTIME_DRIVER' } default { "$sub" } }
    } catch { $r.Kind = 'parse error' }

    if (Test-FileContainsUtf16 -Path $Path -Needle 'GPU PCIe DIAGNOSTIC') {
        $r.Kind = 'GpuCheck'
        return $r
    }

    # Identify Microsoft images by signer / version resource, not by a display
    # string - bootmgfw.efi keeps its UI text in .mui files.
    $signer = ''
    try {
        $sig = Get-AuthenticodeSignature -LiteralPath $Path -ErrorAction Stop
        if ($sig -and $sig.SignerCertificate) { $signer = "$($sig.SignerCertificate.Subject)" }
    } catch {}

    $orig = ''; $comp = ''
    try {
        $vi = (Get-Item -LiteralPath $Path).VersionInfo
        if ($vi) { $orig = "$($vi.OriginalFilename)"; $comp = "$($vi.CompanyName)" }
    } catch {}

    if ($orig -match '^bootmgr(fw)?\.(exe|efi)$' -and $comp -match 'Microsoft') {
        $r.Kind = 'Windows Boot Manager'
    } elseif ($orig -match '^winresume' ) {
        $r.Kind = 'Windows Resume Loader'
    } elseif ($orig -match '^memtest') {
        $r.Kind = 'Windows Memory Diagnostic'
    } elseif ($signer -match 'Microsoft') {
        $r.Kind = "Microsoft-signed ($orig)"
    } else {
        $r.Kind = 'other EFI image'
    }
    return $r
}

function Dismount-Esp {
    if (-not $script:MountedAccessPath) { return }
    try {
        Remove-PartitionAccessPath -DiskNumber $script:MountedDisk -PartitionNumber $script:MountedPartition -AccessPath $script:MountedAccessPath -ErrorAction Stop
        Ok "ESP unmounted from $($script:MountedAccessPath)"
    } catch {
        Warn "Remove manually: mountvol $($script:MountedAccessPath) /D"
    }
    $script:MountedAccessPath = $null
}

try {

Step 'Environment'
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) { Abort 'Administrator rights are required to mount the ESP.' }
Ok 'Running elevated'

Step 'Disk'
$disk = Get-Disk -Number $DiskNumber -ErrorAction SilentlyContinue
if (-not $disk) { Abort "No disk $DiskNumber." }
Say "  Number         : $($disk.Number)"
Say "  Model          : $($disk.FriendlyName)"
Say "  Serial         : $($disk.SerialNumber)"
Say "  Size           : $([math]::Round($disk.Size/1GB,2)) GB"
Say "  Bus            : $($disk.BusType)"
Say "  Partition style: $($disk.PartitionStyle)"
Say "  IsBoot/IsSystem: $($disk.IsBoot) / $($disk.IsSystem)"
Say "  Unique Id      : $($disk.UniqueId)"
Say "  Guid           : $($disk.Guid)"

if ($disk.IsBoot -or $disk.IsSystem) { Abort 'This is the boot/system disk. Refusing even to mount.' }

Step 'Partitions'
$parts = Get-Partition -DiskNumber $DiskNumber | Sort-Object PartitionNumber
foreach ($p in $parts) {
    $v = $null; try { $v = $p | Get-Volume -ErrorAction Stop } catch {}
    $fs = if ($v) { $v.FileSystemType } else { '<unmounted>' }
    Say ("  {0}  {1,-3} {2,10:N2} GB  {3,-12} {4}" -f $p.PartitionNumber,
        $(if ($p.DriveLetter) { "$($p.DriveLetter):" } else { '' }),
        ($p.Size / 1GB), $fs, $p.GptType)
}

$esp = $parts | Where-Object { $_.GptType -eq $ESP_TYPE_GUID }
if (-not $esp)        { Abort 'No EFI System Partition found.' }
if ($esp -is [array]) { Abort 'Multiple EFI System Partitions found.' }
Ok "ESP: partition $($esp.PartitionNumber), GUID $($esp.Guid), $([math]::Round($esp.Size/1MB,1)) MB"

Step 'BitLocker'
foreach ($p in ($parts | Where-Object { $_.DriveLetter })) {
    $mp = "$($p.DriveLetter):"
    try {
        $bl = Get-BitLockerVolume -MountPoint $mp -ErrorAction Stop
        Say "  $mp  protection=$($bl.ProtectionStatus)  method=$($bl.EncryptionMethod)  lock=$($bl.LockStatus)  pct=$($bl.EncryptionPercentage)"
        if ($bl.ProtectionStatus -eq 'On') { Warn "$mp is BitLocker protected - altering the boot chain may trigger a recovery prompt" }
        else { Ok "$mp is not BitLocker protected" }
    } catch {
        Warn "$mp : could not query BitLocker ($($_.Exception.Message))"
    }
}

Step 'Mounting ESP read-only inspection'
$used = [System.IO.DriveInfo]::GetDrives() | ForEach-Object { $_.Name.Substring(0,1).ToUpper() }
$letter = $null
foreach ($c in [char[]]'STUVWXYZQRNOPKLM') { if ($used -notcontains "$c") { $letter = "$c"; break } }
if (-not $letter) { Abort 'No free drive letter.' }
$mountPath = "${letter}:\"

Add-PartitionAccessPath -DiskNumber $DiskNumber -PartitionNumber $esp.PartitionNumber -AccessPath $mountPath
$script:MountedAccessPath = $mountPath
$script:MountedDisk       = $DiskNumber
$script:MountedPartition  = $esp.PartitionNumber
Start-Sleep -Milliseconds 700
Ok "Mounted at $mountPath"

$vol = Get-Volume -DriveLetter $letter
Say "  Filesystem : $($vol.FileSystemType)"
Say "  Label      : $($vol.FileSystemLabel)"
Say "  Size       : $([math]::Round($vol.Size/1MB,1)) MB"
Say "  Free       : $([math]::Round($vol.SizeRemaining/1MB,1)) MB"

Step 'ESP contents'
$root = "${letter}:"
$files = Get-ChildItem -LiteralPath $root -Recurse -File -Force -ErrorAction SilentlyContinue | Sort-Object FullName
if (-not $files) {
    Warn 'ESP appears to be empty.'
} else {
    foreach ($f in $files) {
        $rel = $f.FullName.Substring($root.Length)
        Say ("  {0,10}  {1}  {2}" -f $f.Length, $f.LastWriteTime.ToString('yyyy-MM-dd HH:mm'), $rel)
    }
}

Step 'EFI executables identified'
$efis = $files | Where-Object { $_.Extension -match '^\.efi$' }
if (-not $efis) {
    Warn 'No .efi files found on this ESP.'
} else {
    foreach ($f in $efis) {
        $info = Get-PeInfo -Path $f.FullName
        $rel  = $f.FullName.Substring($root.Length)
        Say ("  {0,-52} {1,9} bytes  {2,-16} {3}" -f $rel, $f.Length, $info.Subsystem, $info.Kind)
    }
}

Step 'Key paths'
foreach ($p in @('EFI\Microsoft\Boot\bootmgfw.efi',
                 'EFI\Microsoft\Boot\bootmgfw-original.efi',
                 'EFI\Microsoft\Boot\BCD',
                 'EFI\Boot\bootx64.efi',
                 'EFI\Boot\BOOTX64-BACKUP.EFI',
                 'EFI\GPUCHECK\GpuCheck.efi',
                 'EFI\GPUCHECK\gpucheck.cfg',
                 'EFI\GPUCHECK\lastscan.txt')) {
    $full = Join-Path $root $p
    if (Test-Path -LiteralPath $full) {
        Ok ("{0,-45} present ({1} bytes)" -f "\$p", (Get-Item -LiteralPath $full).Length)
    } else {
        Say ("  [--]   {0,-45} absent" -f "\$p")
    }
}

Step 'Verdict'
$bootmgfw = Join-Path $root 'EFI\Microsoft\Boot\bootmgfw.efi'
$original = Join-Path $root 'EFI\Microsoft\Boot\bootmgfw-original.efi'
if (-not (Test-Path -LiteralPath $bootmgfw)) {
    Bad 'bootmgfw.efi missing - this ESP cannot boot Windows as-is.'
} elseif (Test-FileContainsUtf16 -Path $bootmgfw -Needle 'GPU PCIe DIAGNOSTIC') {
    if (Test-Path -LiteralPath $original) {
        Ok 'GpuCheck is INSTALLED (Mode A) and the original loader is preserved.'
    } else {
        Bad 'GpuCheck is installed but bootmgfw-original.efi is MISSING. Windows cannot be chainloaded.'
    }
} else {
    Ok 'GpuCheck is NOT installed. bootmgfw.efi is the stock Windows Boot Manager.'
}

} catch {
    Bad $_.Exception.Message
} finally {
    Step 'Cleanup'
    Dismount-Esp
    if ($LogFile) { try { Stop-Transcript | Out-Null } catch {} }
}
