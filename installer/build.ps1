<#
.SYNOPSIS
    Builds the Mandarin Pinyin Converter Windows installer (NSIS).

.DESCRIPTION
    Verifies that NSIS (makensis) and all payload files referenced by setup.nsi
    are present, then compiles installer\MandarinPinyinConverter-Setup.exe.

    Run from anywhere:
        powershell -ExecutionPolicy Bypass -File installer\build.ps1

.PARAMETER NoPiper
    Skip the Piper TTS section's prerequisite check. Only use this if you have
    also removed/commented the SecPiper section in setup.nsi; otherwise the
    NSIS compile will fail on the missing piper\ files.
#>
[CmdletBinding()]
param(
    [switch]$NoPiper
)

$ErrorActionPreference = 'Stop'

# Resolve paths (script lives in installer\, repo root is its parent)
$InstallerDir = $PSScriptRoot
$RepoRoot     = Split-Path -Parent $InstallerDir
$ReleaseDir   = Join-Path $RepoRoot 'build\Release'
$PiperDir     = Join-Path $RepoRoot 'piper'
$Script       = Join-Path $InstallerDir 'setup.nsi'
$OutFile      = Join-Path $InstallerDir 'MandarinPinyinConverter-Setup.exe'

function Fail($msg) { Write-Host "  [MISSING] $msg" -ForegroundColor Red }
function Ok($msg)   { Write-Host "  [ok]      $msg" -ForegroundColor Green }

Write-Host "Mandarin Pinyin Converter - installer build" -ForegroundColor Cyan
Write-Host "Repo root: $RepoRoot`n"

# Locate makensis
$makensis = (Get-Command makensis.exe -ErrorAction SilentlyContinue).Source
if (-not $makensis) {
    foreach ($p in @(
        "$env:ProgramFiles\NSIS\makensis.exe",
        "${env:ProgramFiles(x86)}\NSIS\makensis.exe"
    )) {
        if (Test-Path $p) { $makensis = $p; break }
    }
}

$problems = 0

Write-Host "Checking NSIS:"
if ($makensis) {
    Ok "makensis -> $makensis"
} else {
    Fail "makensis.exe not found. Install NSIS:  winget install NSIS.NSIS"
    $problems++
}

# Required payload files
$required = @(
    (Join-Path $InstallerDir 'vc_redist.x64.exe'),
    (Join-Path $ReleaseDir   'mandarin-pdf-reader.exe'),
    (Join-Path $ReleaseDir   'wkhtmltopdf.exe'),
    (Join-Path $ReleaseDir   'brotlicommon.dll'),
    (Join-Path $ReleaseDir   'brotlidec.dll'),
    (Join-Path $ReleaseDir   'bz2.dll'),
    (Join-Path $ReleaseDir   'freetype.dll'),
    (Join-Path $ReleaseDir   'harfbuzz.dll'),
    (Join-Path $ReleaseDir   'jpeg62.dll'),
    (Join-Path $ReleaseDir   'libpng16.dll'),
    (Join-Path $ReleaseDir   'openjp2.dll'),
    (Join-Path $ReleaseDir   'z.dll'),
    (Join-Path $ReleaseDir   'data')
)

Write-Host "`nChecking required payload:"
foreach ($f in $required) {
    if (Test-Path $f) { Ok (Resolve-Path $f -Relative) }
    else              { Fail $f; $problems++ }
}

# Piper TTS payload (optional section in setup.nsi)
if (-not $NoPiper) {
    $piperFiles = @(
        'piper.exe', 'espeak-ng.dll', 'libtashkeel_model.ort', 'onnxruntime.dll',
        'onnxruntime_providers_shared.dll', 'piper_phonemize.dll',
        'zh_CN-huayan-medium.onnx', 'zh_CN-huayan-medium.onnx.json', 'espeak-ng-data'
    )
    Write-Host "`nChecking Piper TTS payload (pass -NoPiper to skip):"
    foreach ($name in $piperFiles) {
        $f = Join-Path $PiperDir $name
        if (Test-Path $f) { Ok "piper\$name" }
        else              { Fail $f; $problems++ }
    }
}

if ($problems -gt 0) {
    Write-Host "`n$problems prerequisite(s) missing. Fix the above, then re-run." -ForegroundColor Red
    exit 1
}

# Compile
Write-Host "`nCompiling installer..." -ForegroundColor Cyan
& $makensis $Script
if ($LASTEXITCODE -ne 0) {
    Write-Host "`nmakensis failed (exit $LASTEXITCODE)." -ForegroundColor Red
    exit $LASTEXITCODE
}

if (Test-Path $OutFile) {
    $sizeMB = [math]::Round((Get-Item $OutFile).Length / 1MB, 1)
    Write-Host "`nDone -> $OutFile ($sizeMB MB)" -ForegroundColor Green
} else {
    Write-Host "`nmakensis reported success but $OutFile was not found." -ForegroundColor Yellow
    exit 1
}
