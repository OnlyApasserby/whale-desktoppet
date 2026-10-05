<#
.SYNOPSIS
    WhalePet release driver: portable folder + zip + NSIS installer.

.DESCRIPTION
    Produces the two official release artifacts, only under <WorkspaceRoot>\dist:
        dist\<App>-<Version>-portable\        staged exe(s) + windeployqt output
        dist\<App>-<Version>-portable.zip
        dist\<App>-<Version>-setup.exe        compiled with makensis

    Contract (workspace-manage section 6, build commands from qt-msvc-cmake):
      - the build directory MUST already exist and MUST be reused; this script
        never creates, cleans or switches a build directory
      - only Release-like configurations are accepted; a Debug build must not
        be shipped as a release
      - every external command runs with a timeout
      - NSIS baseline: D:\program files (x86)\NSIS\makensis.exe
      - absolute paths are passed to makensis (relative paths would resolve
        against the .nsi location, not the working directory)

    WhalePet specifics carried over from the former scripts\make-package.ps1
    (it created its own build-package directory, which violates the
    "reuse the existing build directory" rule - so it was retired):
      - the MCP stdio bridge (whalepet-mcp.exe) ships next to WhalePet.exe
        => -ExtraExe
      - the portable folder must not ship debug symbols
        => -PurgeDebugFiles
      - engine\ is shipped EMPTY (the user drops in their own UCI chess engine)
        => -EmptyFolders
      - plugins\ must never be redistributed (third-party DLL drop-in)
        => -PurgeFolders
      - LICENSE must travel with the binaries (MIT requires the notice)
        => -ShipFiles
      - install.exe icon/version metadata needs a 4-segment version
        => -DAPP_VERSION4 derived automatically

.NOTES
    ASCII-only on purpose (PowerShell 5.1 without BOM reads the system code
    page, which corrupts non-ASCII literals). Same convention as every other
    script under scripts\ and as cmake\Executables.cmake line 29.

.EXAMPLE
    # build-package is configured with -DWHALEPET_PACKAGE=ON, so the Release
    # binaries are already written to dist\WhalePet (no PDB).
    powershell -ExecutionPolicy Bypass -File scripts\package-release.ps1 `
        -AppName WhalePet -Version 0.2.0 -BuildDir build-package
#>
[CmdletBinding()]
param(
    [string]$WorkspaceRoot = '.',

    [Parameter(Mandatory = $true)][string]$AppName,
    [Parameter(Mandatory = $true)][string]$Version,

    [string]$BuildDir = 'build',

    [ValidateSet('Release', 'RelWithDebInfo')]
    [string]$Config = 'Release',

    # Primary executable. Empty => auto-detect (see Resolve-PrimaryExe).
    [string]$ExePath = '',

    # Extra binaries staged next to the primary exe (bare name => looked up
    # next to the primary exe first, then relative to the workspace root).
    [string[]]$ExtraExe = @('whalepet-mcp.exe'),

    [string]$QtRoot = 'D:/Qt-debug',
    [string]$NsisExe = 'D:\program files (x86)\NSIS\makensis.exe',
    [string]$NsiScript = 'scripts\installer.nsi',
    [string]$NsisInputCharset = 'UTF8',

    [int]$TimeoutMs = 600000,

    [bool]$PurgeDebugFiles = $true,
    [string[]]$ShipFiles = @('README.md', 'LICENSE'),
    [string[]]$EmptyFolders = @('engine'),
    [string[]]$PurgeFolders = @('plugins'),

    [switch]$SkipZip,
    [switch]$SkipInstaller
)

$ErrorActionPreference = 'Stop'

function Write-Step { param([string]$Text) Write-Host ('[release] ' + $Text) -ForegroundColor Cyan }
function Write-Warn2 { param([string]$Text) Write-Warning ('[release] ' + $Text) }

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$Exe,
        [string[]]$Arguments = @(),
        [int]$Timeout = 600000,
        [string]$Label = ''
    )
    $p = Start-Process -FilePath $Exe -ArgumentList $Arguments -NoNewWindow -PassThru
    if (-not $p.WaitForExit($Timeout)) {
        try { $p.Kill() } catch { }
        throw ('TIMEOUT (' + $Timeout + ' ms): ' + $Label)
    }
    if ($p.ExitCode -ne 0) {
        throw ('FAILED (exit ' + $p.ExitCode + '): ' + $Label)
    }
    return $p.ExitCode
}

if ($Version -notmatch '^\d+\.\d+\.\d+') {
    throw ('Refused: -Version must be semantic (e.g. 1.0.0), got: ' + $Version)
}

# 4-segment version for the NSIS version resource (VIProductVersion requires X.X.X.X).
$verParts = @($Version.Split('.')) + @('0', '0', '0')
$Version4 = (($verParts[0..3]) -join '.')

$root = (Resolve-Path -LiteralPath $WorkspaceRoot).Path
$buildPath = Join-Path $root $BuildDir

# -------------------------------------------------- 1. reuse existing build dir
if (-not (Test-Path -LiteralPath $buildPath)) {
    throw @"
Refused: build directory does not exist: $buildPath

Build directories must be reused, never created by this script.
Prepare the build first (qt-msvc-cmake section 4), then re-run.
"@
}
$cacheFile = Get-ChildItem -LiteralPath $buildPath -Filter 'CMakeCache.txt' -ErrorAction SilentlyContinue
if ($cacheFile) {
    $cacheText = Get-Content -LiteralPath $cacheFile.FullName -Raw
    if ($cacheText -match 'CMAKE_BUILD_TYPE:STRING=Debug') {
        throw ('Refused: ' + $BuildDir + ' is a Debug build directory. Releases require a Release build.')
    }
}
Write-Step ('reusing existing build directory: ' + $BuildDir)

# ------------------------------------------------------------ 2. resolve paths
function Resolve-PrimaryExe {
    param([string]$BuildPath, [string]$Config, [string]$AppName, [string]$Root)

    $candidates = New-Object System.Collections.Generic.List[string]
    if (-not [string]::IsNullOrWhiteSpace($ExePath)) { $candidates.Add($ExePath) | Out-Null }
    $candidates.Add((Join-Path $BuildPath (Join-Path $Config ($AppName + '.exe')))) | Out-Null
    $candidates.Add((Join-Path $BuildPath ($AppName + '.exe'))) | Out-Null
    # WHALEPET_PACKAGE=ON (cmake\OutputLayout.cmake) writes Release output to dist\<App>\
    $candidates.Add((Join-Path $Root (Join-Path 'dist' (Join-Path $AppName ($AppName + '.exe'))))) | Out-Null

    foreach ($c in $candidates) {
        $p = $c
        if (-not [System.IO.Path]::IsPathRooted($p)) { $p = Join-Path $Root $p }
        if (Test-Path -LiteralPath $p) {
            return (Resolve-Path -LiteralPath $p).Path
        }
    }
    if (-not [string]::IsNullOrWhiteSpace($ExePath)) {
        throw ('Target executable not found: ' + $ExePath)
    }
    throw ('Target executable not found. Looked for:' + [Environment]::NewLine + (($candidates | ForEach-Object { '  ' + $_ }) -join [Environment]::NewLine))
}
$primaryExe = Resolve-PrimaryExe -BuildPath $buildPath -Config $Config -AppName $AppName -Root $root
Write-Step ('primary executable: ' + $primaryExe)

$distDir = Join-Path $root 'dist'
if (-not (Test-Path -LiteralPath $distDir)) { New-Item -ItemType Directory -Force -Path $distDir | Out-Null }

$baseName = $AppName + '-' + $Version
$portable = Join-Path $distDir ($baseName + '-portable')
$zipPath = Join-Path $distDir ($baseName + '-portable.zip')
$setupPath = Join-Path $distDir ($baseName + '-setup.exe')

if (Test-Path -LiteralPath $portable) { Remove-Item -LiteralPath $portable -Recurse -Force }
New-Item -ItemType Directory -Force -Path $portable | Out-Null

$stagedExes = New-Object System.Collections.Generic.List[string]
Copy-Item -LiteralPath $primaryExe -Destination $portable -Force
$stagedPrimary = Join-Path $portable (Split-Path -Leaf $primaryExe)
$stagedExes.Add($stagedPrimary) | Out-Null
Write-Step ('staged primary: ' + (Split-Path -Leaf $primaryExe))

foreach ($extra in $ExtraExe) {
    if ([string]::IsNullOrWhiteSpace($extra)) { continue }
    $candidates = New-Object System.Collections.Generic.List[string]
    $candidates.Add((Join-Path (Split-Path -Parent $primaryExe) $extra)) | Out-Null
    $extraFromRoot = $extra
    if (-not [System.IO.Path]::IsPathRooted($extraFromRoot)) { $extraFromRoot = Join-Path $root $extra }
    $candidates.Add($extraFromRoot) | Out-Null

    $found = $null
    foreach ($c in $candidates) {
        if (Test-Path -LiteralPath $c) { $found = (Resolve-Path -LiteralPath $c).Path; break }
    }
    if (-not $found) {
        throw ('Extra executable not found: ' + $extra + ' (looked next to ' + (Split-Path -Leaf $primaryExe) + ' and relative to the workspace root)')
    }
    Copy-Item -LiteralPath $found -Destination $portable -Force
    $stagedExes.Add((Join-Path $portable (Split-Path -Leaf $found))) | Out-Null
    Write-Step ('staged extra: ' + (Split-Path -Leaf $found))
}

# ------------------------------------------------------------- 3. windeployqt
$deployQt = Join-Path (Join-Path $QtRoot 'bin') 'windeployqt.exe'
if (-not (Test-Path -LiteralPath $deployQt)) {
    throw ('windeployqt not found: ' + $deployQt + ' (check -QtRoot against the qt-msvc-cmake baseline)')
}
$deployArgs = @('--release', '--no-translations', '--compiler-runtime', '--dir', $portable) + $stagedExes.ToArray()
Invoke-Checked -Exe $deployQt -Arguments $deployArgs -Timeout $TimeoutMs -Label 'windeployqt' | Out-Null
Write-Step 'windeployqt finished'

if (-not (Test-Path -LiteralPath (Join-Path $portable 'platforms'))) {
    Write-Warn2 'platforms\ not found in the portable folder; Qt GUI may fail to start.'
}

# ------------------------------------------- 4. WhalePet post-processing rules
if ($PurgeDebugFiles) {
    $junk = Get-ChildItem -Path $portable -Recurse -File -Include *.pdb, *.ilk, *.exp, *.lib -ErrorAction SilentlyContinue
    if ($junk) {
        Write-Step ('purged debug files: ' + (($junk | ForEach-Object { $_.Name }) -join ', '))
        $junk | Remove-Item -Force
    }
}

foreach ($folder in $EmptyFolders) {
    if ([string]::IsNullOrWhiteSpace($folder)) { continue }
    $target = Join-Path $portable $folder
    if (Test-Path -LiteralPath $target) {
        Write-Step ('purged stale ' + $folder + '\ (user content is not redistributed)')
        Remove-Item -LiteralPath $target -Recurse -Force
    }
    New-Item -ItemType Directory -Path $target -Force | Out-Null
    Write-Step ('created empty folder: ' + $folder + '\')
}

foreach ($folder in $PurgeFolders) {
    if ([string]::IsNullOrWhiteSpace($folder)) { continue }
    $target = Join-Path $portable $folder
    if (Test-Path -LiteralPath $target) {
        Write-Step ('purged stale ' + $folder + '\ (user content is not redistributed)')
        Remove-Item -LiteralPath $target -Recurse -Force
    }
}

foreach ($ship in $ShipFiles) {
    if ([string]::IsNullOrWhiteSpace($ship)) { continue }
    $src = $ship
    if (-not [System.IO.Path]::IsPathRooted($src)) { $src = Join-Path $root $src }
    if (-not (Test-Path -LiteralPath $src)) {
        Write-Warn2 ('ship file missing, skipped: ' + $ship)
        continue
    }
    Copy-Item -LiteralPath $src -Destination $portable -Force
    Write-Step ('shipped: ' + (Split-Path -Leaf $src))
}

# ------------------------------------------------------------------ 5. zip it
$zipResult = 'skipped'
if (-not $SkipZip) {
    if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
    Compress-Archive -Path (Join-Path $portable '*') -DestinationPath $zipPath -Force
    $zipResult = 'built'
    Write-Step ('portable package: ' + (Split-Path -Leaf $zipPath))
}

# -------------------------------------------------------------- 6. NSIS setup
$installerResult = 'skipped'
if ($SkipInstaller) {
    Write-Warn2 'installer build skipped (-SkipInstaller); portable output only.'
    $installerResult = 'skipped-by-flag'
}
elseif (-not (Test-Path -LiteralPath $NsisExe)) {
    Write-Warn2 ('NSIS compiler not found: ' + $NsisExe)
    Write-Warn2 'Install NSIS at D:\program files (x86)\NSIS or pass -NsisExe / -SkipInstaller.'
    $installerResult = 'skipped-nsis-missing'
}
else {
    $nsiPath = Join-Path $root $NsiScript
    if (-not (Test-Path -LiteralPath $nsiPath)) {
        throw ('NSIS script not found: ' + $nsiPath + ' (copy assets\installer.nsi.template to scripts\installer.nsi)')
    }
    $nsiArgs = @(
        '/V2',
        ('/INPUTCHARSET ' + $NsisInputCharset),
        ('/DAPP_NAME=' + $AppName),
        ('/DAPP_VERSION=' + $Version),
        ('/DAPP_VERSION4=' + $Version4),
        ('/DSRC_DIR=' + $portable),
        ('/DOUT_FILE=' + $setupPath)
    )
    Invoke-Checked -Exe $NsisExe -Arguments ($nsiArgs + @($nsiPath)) -Timeout $TimeoutMs -Label 'makensis' | Out-Null
    if (-not (Test-Path -LiteralPath $setupPath)) {
        throw ('makensis reported success but the installer is missing: ' + $setupPath)
    }
    $installerResult = 'built'
    Write-Step ('installer: ' + (Split-Path -Leaf $setupPath))
}

# ------------------------------------------------------------ 7. record/report
$portableSize = (Get-ChildItem -Path $portable -Recurse -File | Measure-Object -Property Length -Sum).Sum
Write-Host ''
Write-Host '==== Package complete ===='
Write-Host ('Portable folder : dist\' + $baseName + '-portable' + '  (' + [math]::Round($portableSize / 1MB, 1) + ' MB)')
if ($zipResult -eq 'built') {
    Write-Host ('Portable zip    : dist\' + $baseName + '-portable.zip' + '  (' + [math]::Round((Get-Item -LiteralPath $zipPath).Length / 1MB, 1) + ' MB)')
}
if ($installerResult -eq 'built') {
    Write-Host ('Installer       : dist\' + $baseName + '-setup.exe' + '  (' + [math]::Round((Get-Item -LiteralPath $setupPath).Length / 1MB, 1) + ' MB)')
}
else {
    Write-Host ('Installer       : ' + $installerResult)
}

$zipRel = 'none'
if ($zipResult -eq 'built') { $zipRel = 'dist\' + $baseName + '-portable.zip' }
$installerRel = 'none'
if ($installerResult -eq 'built') { $installerRel = 'dist\' + $baseName + '-setup.exe' }

$summary = [ordered]@{
    skill           = 'workspace-manage'
    script          = 'package-release.ps1'
    workspaceRoot   = $root
    buildDir        = [ordered]@{ path = $BuildDir; reused = $true; created = $false; config = $Config }
    primaryExe      = $primaryExe
    distDir         = 'dist'
    portable        = ('dist\' + $baseName + '-portable')
    portableZip     = $zipRel
    installer       = $installerRel
    installerStatus = $installerResult
    version4        = $Version4
    nsisExe         = $NsisExe
    licenseNotice   = 'Verify LICENSE / README.md / docs\release.md stay consistent (workspace-manage section 8).'
    nextSteps       = @(
        'Run the portable build under a clean PATH and check the exit code.',
        'Smoke-test the installer: silent install to a temp dir, run, silent uninstall.',
        'Record version, artifacts and license in docs\release.md and refresh docs\README.md.'
    )
}
$summary | ConvertTo-Json -Depth 6
