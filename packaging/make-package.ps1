<#
.SYNOPSIS
    Build WhalePet release artifacts: portable folder + NSIS installer.

.DESCRIPTION
    1) Configure a DEDICATED build dir (build-package) with -DWHALEPET_PACKAGE=ON:
       the Release binaries (WhalePet.exe + the whalepet-mcp.exe MCP bridge) are
       written to dist/WhalePet and carry NO debug symbols;
    2) Build Release;
    3) Run windeployqt to bundle the Qt runtime and plugins;
    4) Defensively purge any leftover debug files (*.pdb / *.ilk / *.exp / *.lib);
    5) Prepare an EMPTY engine/ folder (drop-in location for the user's own UCI
       chess engine; any engine left on the packaging machine is purged first);
    6) Invoke makensis to produce dist/WhalePet-Setup-<version>.exe.

    The portable edition is simply the dist/WhalePet/ folder (zip it to distribute).

    NOTE: this script is intentionally ASCII-only. Windows PowerShell 5.1 reads
    BOM-less script files as ANSI, which corrupts non-ASCII literals.

.PARAMETER QtDir
    Qt prefix containing bin/windeployqt.exe. Default: D:/Qt-debug.

.PARAMETER NsisDir
    NSIS install dir containing makensis.exe. Default: D:\program files (x86)\NSIS.

.PARAMETER Version
    Version string (written to the installer metadata and file name). Default: 0.2.0.

.PARAMETER SkipBuild
    Skip the CMake configure/build step and package the existing dist/WhalePet.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File packaging/make-package.ps1
#>
param(
    [string]$QtDir   = 'D:/Qt-debug',
    [string]$NsisDir = 'D:\program files (x86)\NSIS',
    [string]$Version = '0.2.0',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

$root      = Split-Path -Parent $PSScriptRoot
$cmake     = 'C:\Program Files\CMake\bin\cmake.exe'
$buildDir  = Join-Path $root 'build-package'
$distDir   = Join-Path $root 'dist\WhalePet'
$windeploy = Join-Path $QtDir 'bin\windeployqt.exe'
$makensis  = Join-Path $NsisDir 'makensis.exe'
$nsiFile   = Join-Path $root 'packaging\whalepet.nsi'
$setupExe  = Join-Path $root ("dist\WhalePet-Setup-$Version.exe")

function Assert-Path {
    param([string]$Path, [string]$What)
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$What not found: $Path"
    }
}

Assert-Path -Path $cmake     -What 'CMake'
Assert-Path -Path $windeploy -What 'windeployqt'
Assert-Path -Path $makensis  -What 'makensis (NSIS)'

if ($SkipBuild) {
    Write-Host '==> Skipping build (-SkipBuild)'
} else {
    Write-Host '==> [1/5] Configure release build (WHALEPET_PACKAGE=ON, output dist/WhalePet)'
    & $cmake -S $root -B $buildDir -G 'Visual Studio 18 2026' -A x64 -DCMAKE_PREFIX_PATH="$QtDir" -DWHALEPET_PACKAGE=ON
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed (exit $LASTEXITCODE)" }

    Write-Host '==> [2/5] Build Release'
    & $cmake --build $buildDir --config Release --parallel
    if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)" }
}

$exePath = Join-Path $distDir 'WhalePet.exe'
Assert-Path -Path $exePath -What 'Release exe (build first)'

# P7.2: the MCP stdio bridge process. Shipped in the same folder and installed by
# whalepet.nsi (uninstall removes it explicitly - see docs/packages.md 2/5).
$mcpExePath = Join-Path $distDir 'whalepet-mcp.exe'
Assert-Path -Path $mcpExePath -What 'Release MCP bridge exe (build first)'

Write-Host '==> [3/5] windeployqt (bundle Qt runtime and plugins)'
& $windeploy --release --no-translations --compiler-runtime --dir $distDir $exePath $mcpExePath
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed (exit $LASTEXITCODE)" }

# Defensive purge: the portable folder must not ship any debug symbols.
$junk = Get-ChildItem -Path $distDir -Recurse -File -Include *.pdb, *.ilk, *.exp, *.lib -ErrorAction SilentlyContinue
if ($junk) {
    Write-Host ('    purged debug files: ' + (($junk | ForEach-Object { $_.Name }) -join ', '))
    $junk | Remove-Item -Force
}

# Chess engine drop-in folder: ship it EMPTY. The user supplies their own UCI
# engine (e.g. stockfish.exe) here at runtime - see README "Chess engine".
# Anything left over on the packaging machine must NOT be redistributed, so the
# folder content is purged before being recreated empty.
Write-Host '==> [4/5] Prepare empty engine/ folder (drop in your own UCI engine)'
$engineDir = Join-Path $distDir 'engine'
if (Test-Path -LiteralPath $engineDir) {
    Write-Host '    purged stale engine/ content (user engines are not redistributed)'
    Remove-Item -LiteralPath $engineDir -Recurse -Force
}
New-Item -ItemType Directory -Path $engineDir -Force | Out-Null
Write-Host '    created empty engine/ folder (drop your UCI engine here)'

# Dynamic plugin (DLL) drop-in folder (P7.3): NOT shipped. The host scans
# <applicationDirPath>/plugins at startup; a missing folder is normal. Any stale
# folder left on the packaging machine is purged so third-party DLLs are never
# redistributed (whalepet.nsi also excludes plugins/ - see docs/packages.md 8).
$pluginsDir = Join-Path $distDir 'plugins'
if (Test-Path -LiteralPath $pluginsDir) {
    Write-Host '    purged stale plugins/ content (user plugins are not redistributed)'
    Remove-Item -LiteralPath $pluginsDir -Recurse -Force
}

# Ship docs + license with the portable folder (MIT requires the notice to travel along).
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination $distDir -Force
Copy-Item -LiteralPath (Join-Path $root 'LICENSE')   -Destination $distDir -Force

Write-Host '==> [5/5] Build NSIS installer'
& $makensis /INPUTCHARSET UTF8 "/DAPP_VERSION=$Version" $nsiFile
if ($LASTEXITCODE -ne 0) { throw "makensis failed (exit $LASTEXITCODE)" }

Assert-Path -Path $setupExe -What 'Installer'

$portableSize = (Get-ChildItem -Path $distDir -Recurse -File | Measure-Object -Property Length -Sum).Sum
Write-Host ''
Write-Host '==== Package complete ===='
Write-Host ('Portable folder : ' + $distDir)
Write-Host ('  size          : ' + [math]::Round($portableSize / 1MB, 1) + ' MB (zip and ship)')
Write-Host ('Installer       : ' + $setupExe)
Write-Host ('  size          : ' + [math]::Round((Get-Item -LiteralPath $setupExe).Length / 1MB, 1) + ' MB')
