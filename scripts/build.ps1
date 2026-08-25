<#
.SYNOPSIS
    Builds GpuCheck.efi - a freestanding x86-64 PE32+ UEFI application - with
    the MSVC toolchain, without requiring an EDK II tree.

.DESCRIPTION
    Compiles every .c file with no CRT, no default libraries and no stack
    checks, then links with /SUBSYSTEM:EFI_APPLICATION so the firmware
    recognises the result as a UEFI application (PE subsystem 10).

    The same sources build under EDK II with -D GPUCHECK_EDK2; see
    GpuCheck.inf.

.PARAMETER Clean
    Delete intermediate objects before building.
#>
[CmdletBinding()]
param(
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir    = Join-Path $ProjectRoot 'build'
$ObjDir      = Join-Path $BuildDir 'obj'
$OutFile     = Join-Path $BuildDir 'GpuCheck.efi'
$MapFile     = Join-Path $BuildDir 'GpuCheck.map'

function Write-Step($m) { Write-Host "[build] $m" -ForegroundColor Cyan }
function Write-Ok($m)   { Write-Host "[ ok  ] $m" -ForegroundColor Green }
function Write-Err($m)  { Write-Host "[fail ] $m" -ForegroundColor Red }

# --------------------------------------------------------------------------
# Locate MSVC
# --------------------------------------------------------------------------
Write-Step 'Locating MSVC toolchain...'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot  = $null
if (Test-Path $vswhere) {
    $vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    if ($vsRoot -is [array]) { $vsRoot = $vsRoot[0] }
}
if (-not $vsRoot) {
    $guess = Get-ChildItem 'C:\Program Files (x86)\Microsoft Visual Studio','C:\Program Files\Microsoft Visual Studio' -Directory -ErrorAction SilentlyContinue |
             ForEach-Object { Get-ChildItem $_.FullName -Directory -ErrorAction SilentlyContinue } |
             Where-Object { Test-Path (Join-Path $_.FullName 'VC\Tools\MSVC') }
    if ($guess) { $vsRoot = $guess[0].FullName }
}
if (-not $vsRoot -or -not (Test-Path $vsRoot)) {
    Write-Err 'No Visual Studio / Build Tools installation found.'
    exit 1
}

$msvcRoot = Join-Path $vsRoot 'VC\Tools\MSVC'
$msvcVer  = (Get-ChildItem $msvcRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
$binDir   = Join-Path $msvcRoot "$msvcVer\bin\Hostx64\x64"
$incDir   = Join-Path $msvcRoot "$msvcVer\include"

$cl      = Join-Path $binDir 'cl.exe'
$linkExe = Join-Path $binDir 'link.exe'
$dumpbin = Join-Path $binDir 'dumpbin.exe'

foreach ($t in @($cl, $linkExe)) {
    if (-not (Test-Path $t)) { Write-Err "Missing tool: $t"; exit 1 }
}

Write-Ok "MSVC $msvcVer"
Write-Host "        cl   : $cl"
Write-Host "        link : $linkExe"

# Only the compiler's own headers are exposed. No Windows SDK, no CRT libs:
# this is a freestanding build and must not accidentally pick up OS headers.
$env:INCLUDE = $incDir
$env:LIB     = ''
$env:LIBPATH = ''
$env:PATH    = "$binDir;$env:PATH"

# --------------------------------------------------------------------------
# Prepare output dirs
# --------------------------------------------------------------------------
if ($Clean -and (Test-Path $ObjDir)) { Remove-Item $ObjDir -Recurse -Force }
foreach ($d in @($BuildDir, $ObjDir)) {
    if (-not (Test-Path $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
}

# --------------------------------------------------------------------------
# Compile
# --------------------------------------------------------------------------
$sources = @(
    (Join-Path $ProjectRoot 'GpuCheck.c'),
    (Join-Path $ProjectRoot 'PciScan.c'),
    (Join-Path $ProjectRoot 'WindowsBoot.c'),
    (Join-Path $ProjectRoot 'GpuConfig.c'),
    (Join-Path $ProjectRoot 'GpuLog.c'),
    (Join-Path $ProjectRoot 'Compat\StandaloneUefi.c')
)

foreach ($s in $sources) {
    if (-not (Test-Path $s)) { Write-Err "Missing source: $s"; exit 1 }
}

# /GS-        no stack security cookie (there is no CRT to host it)
# /Gs1048576  effectively disables stack probes (__chkstk is unavailable)
# /Zl         do not embed a default library name in the object
# /Gy         COMDAT functions, so /OPT:REF can drop what we do not use
# /GR- /EHs-c- no RTTI, no exceptions
# /Oi-        do not inline-expand intrinsics we deliberately define ourselves
$clFlags = @(
    '/nologo', '/c', '/TC', '/W4', '/WX-',
    '/GS-', '/Gs1048576', '/Zl', '/Gy', '/O1',
    '/GR-', '/D_UEFI', '/DGPUCHECK_STANDALONE',
    "/I$ProjectRoot"
)

Write-Step "Compiling $($sources.Count) source files..."
$objs = @()
$failed = $false

foreach ($src in $sources) {
    $obj = Join-Path $ObjDir ((Split-Path $src -Leaf) -replace '\.c$', '.obj')
    $objs += $obj
    $out = & $cl @clFlags "/Fo$obj" $src 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Err "cl failed on $(Split-Path $src -Leaf)"
        $out | ForEach-Object { Write-Host "        $_" }
        $failed = $true
    } else {
        $warnings = $out | Where-Object { $_ -match 'warning' }
        if ($warnings) {
            Write-Host "  [warn] $(Split-Path $src -Leaf)" -ForegroundColor Yellow
            $warnings | ForEach-Object { Write-Host "        $_" -ForegroundColor Yellow }
        }
        Write-Host "  [cc]   $(Split-Path $src -Leaf)"
    }
}

if ($failed) { Write-Err 'Compilation failed.'; exit 1 }

# --------------------------------------------------------------------------
# Link
# --------------------------------------------------------------------------
# /SUBSYSTEM:EFI_APPLICATION -> PE subsystem 10, what firmware requires
# /NODEFAULTLIB              -> no CRT whatsoever
# /FIXED:NO                  -> emit a .reloc section so the firmware loader
#                               can place the image anywhere in memory.
#                               /DYNAMICBASE is rejected outright with the EFI
#                               subsystem (LNK1295), so it must be :NO here -
#                               that only clears the ASLR DLL characteristic,
#                               the relocations themselves come from /FIXED:NO.
$linkFlags = @(
    '/NOLOGO',
    '/SUBSYSTEM:EFI_APPLICATION',
    '/ENTRY:EfiMain',
    '/NODEFAULTLIB',
    '/MACHINE:X64',
    '/INCREMENTAL:NO',
    '/MANIFEST:NO',
    '/DEBUG:NONE',
    '/OPT:REF', '/OPT:ICF',
    '/DYNAMICBASE:NO', '/FIXED:NO',
    '/NXCOMPAT:NO',
    "/MAP:$MapFile",
    "/OUT:$OutFile"
)

Write-Step 'Linking...'
$linkOut = & $linkExe @linkFlags @objs 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Err 'link failed.'
    $linkOut | ForEach-Object { Write-Host "        $_" }
    exit 1
}
$linkOut | Where-Object { $_ -match '\S' } | ForEach-Object { Write-Host "        $_" }

if (-not (Test-Path $OutFile)) { Write-Err 'Linker produced no output.'; exit 1 }

$fi = Get-Item $OutFile
Write-Ok "Built $($fi.FullName)  ($($fi.Length) bytes)"

# --------------------------------------------------------------------------
# Verify the image really is a PE32+ EFI application
# --------------------------------------------------------------------------
Write-Step 'Verifying PE image...'
$verify = Join-Path $PSScriptRoot 'verify-pe.py'
$python = (Get-Command python -ErrorAction SilentlyContinue).Source
if ($python -and (Test-Path $verify)) {
    & $python $verify $OutFile
    if ($LASTEXITCODE -ne 0) { Write-Err 'PE verification failed.'; exit 1 }
} elseif (Test-Path $dumpbin) {
    & $dumpbin /headers $OutFile | Select-String -Pattern 'machine|magic|subsystem|entry point'
} else {
    Write-Host '        (no verifier available)'
}

Write-Ok 'Build complete.'
