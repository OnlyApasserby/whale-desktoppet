<#
.SYNOPSIS
    Initialize a workspace skeleton defined by the workspace-manage skill.

.DESCRIPTION
    Creates the directory skeleton, writes a minimal .gitignore, resolves the
    open-source license (MIT by default, GPL-3.0 when a GPLv3 signal is found
    under references/), seeds docs/ and references/ README files, and runs
    "git init" only.

    SECURITY / WORKFLOW CONTRACT
      - This script NEVER configures a git remote and NEVER pushes.
      - Asking the user whether to push to a remote repository is mandatory and
        is reported by this script as a pending action.
      - Remote configuration requires scripts/init-git-remote.ps1 together with
        the explicit -ConfirmPush switch (the recorded user confirmation).
      - Build directories are intentionally NOT created here: existing build
        directories must be reused (workspace-manage section 2.2).

.NOTES
    ASCII-only on purpose. PowerShell 5.1 reads script files using the system
    code page when no BOM is present, so non-ASCII text inside a script may be
    garbled. Generated files are written with -Encoding UTF8 and can be
    localized afterwards (e.g. rename index headings to the project language).

.EXAMPLE
    .\init-workspace.ps1 -WorkspaceRoot D:\proj\mytool -AppName mytool -License auto
#>
[CmdletBinding()]
param(
    [string]$WorkspaceRoot = '.',
    [string]$AppName = 'App',
    [string]$CopyrightHolder = '',
    [ValidateSet('auto', 'MIT', 'GPL-3.0', 'none')]
    [string]$License = 'auto',
    [switch]$SkipGitInit,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

function Write-Step { param([string]$Text) Write-Host ('[init] ' + $Text) -ForegroundColor Cyan }
function Write-Warn2 { param([string]$Text) Write-Warning $Text }

$scriptDir  = $PSScriptRoot
$skillRoot  = Split-Path -Parent $scriptDir
$assetsDir  = Join-Path $skillRoot 'assets'

if (-not (Test-Path -LiteralPath $WorkspaceRoot)) {
    New-Item -ItemType Directory -Force -Path $WorkspaceRoot | Out-Null
}
$root = (Resolve-Path -LiteralPath $WorkspaceRoot).Path
Write-Step ("workspace root: " + $root)

# ---------------------------------------------------------------- 1. skeleton
$dirs = @(
    'src\host',
    'src\api',
    'src\plugins',
    'tests',
    'resources\qt-ui',
    'docs',
    'docs\pitfalls',
    'references',
    'scripts',
    'dist'
)
$created = New-Object System.Collections.ArrayList
foreach ($d in $dirs) {
    $full = Join-Path $root $d
    if (-not (Test-Path -LiteralPath $full)) {
        New-Item -ItemType Directory -Force -Path $full | Out-Null
        [void]$created.Add($d)
    }
}
Write-Step ("directories ensured: " + $dirs.Count + " (new: " + $created.Count + ")")
Write-Step 'build directories are NOT created on purpose: reuse existing ones'

# ------------------------------------------------------------- 2. .gitignore
$gitignorePath = Join-Path $root '.gitignore'
if ((Test-Path -LiteralPath $gitignorePath) -and -not $Force) {
    Write-Step '.gitignore exists, kept (use -Force to overwrite)'
} else {
    Copy-Item -LiteralPath (Join-Path $assetsDir 'gitignore.template') -Destination $gitignorePath -Force
    Write-Step 'wrote .gitignore from assets/gitignore.template'
}

# ------------------------------------------------- 3. GPLv3 detection in refs
function Get-Gplv3Evidence {
    param([string]$RefDir)
    $result = New-Object System.Collections.ArrayList
    if (-not (Test-Path -LiteralPath $RefDir)) { return $result }

    $files = Get-ChildItem -LiteralPath $RefDir -Recurse -File -ErrorAction SilentlyContinue |
             Where-Object { $_.Length -lt 2MB }
    foreach ($f in $files) {
        $n = $f.Name.ToUpperInvariant()
        $candidate = ($n -like 'LICENSE*') -or ($n -like 'LICENCE*') -or ($n -like 'COPYING*') -or
                     ($n -like 'COPYRIGHT*') -or ($n -like '*.JSON') -or ($n -like '*.PRO') -or
                     ($n -like '*.TXT') -or ($n -like '*.MD') -or ($n -like '*.H') -or
                     ($n -like '*.CPP') -or ($n -like '*.CMAKE') -or ($n -like 'CMAKELISTS.TXT') -or
                     ($n -like '*.QMLPROJECT')
        if (-not $candidate) { continue }

        $text = Get-Content -LiteralPath $f.FullName -Raw -ErrorAction SilentlyContinue
        if ([string]::IsNullOrEmpty($text)) { continue }

        $signal = $null
        if ($text -match 'SPDX-License-Identifier:\s*GPL-3\.0') {
            $signal = 'SPDX-License-Identifier: GPL-3.0'
        } elseif (($text -match 'GNU GENERAL PUBLIC LICENSE') -and ($text -match 'Version 3')) {
            $signal = 'GNU GENERAL PUBLIC LICENSE ... Version 3'
        }
        if ($signal) {
            $rel = $f.FullName.Substring($root.Length).TrimStart('\')
            [void]$result.Add([pscustomobject]@{ Path = $rel; Signal = $signal })
        }
    }
    return $result
}

$refDir   = Join-Path $root 'references'
$evidence = @(Get-Gplv3Evidence -RefDir $refDir)

$effective = $License
switch ($License) {
    'none'    { $effective = 'none' }
    'GPL-3.0' { $effective = 'GPL-3.0' }
    default {
        if ($evidence.Count -gt 0) { $effective = 'GPL-3.0' } else { $effective = 'MIT' }
    }
}

if (($evidence.Count -gt 0) -and ($License -eq 'MIT')) {
    Write-Warn2 'GPLv3 evidence found under references/: the project license is promoted to GPL-3.0 (GPLv3 is copyleft).'
}
if (($evidence.Count -gt 0) -and ($effective -ne 'GPL-3.0')) {
    Write-Warn2 'GPLv3 evidence exists but LICENSE was not switched. Report this conflict to the user.'
}

# ---------------------------------------------------------------- 4. LICENSE
$licensePath = Join-Path $root 'LICENSE'
$licenseStatus = 'pending'

if ($effective -eq 'MIT') {
    $tmpl = Get-Content -LiteralPath (Join-Path $assetsDir 'LICENSE-MIT.txt') -Raw
    $holder = $CopyrightHolder
    if ([string]::IsNullOrWhiteSpace($holder)) { $holder = $AppName }
    $text = $tmpl.Replace('{{YEAR}}', (Get-Date -Format 'yyyy')).Replace('{{COPYRIGHT_HOLDER}}', $holder)
    Set-Content -LiteralPath $licensePath -Value $text -Encoding UTF8
    $licenseStatus = 'MIT'
    Write-Step 'wrote LICENSE (MIT)'
}
elseif ($effective -eq 'GPL-3.0') {
    $src = Get-ChildItem -LiteralPath $refDir -Recurse -File -ErrorAction SilentlyContinue |
           Where-Object { $_.Name -match '^(COPYING|LICENSE|LICENCE)' } |
           Where-Object {
               $t = Get-Content -LiteralPath $_.FullName -Raw -ErrorAction SilentlyContinue
               $t -match 'GNU GENERAL PUBLIC LICENSE'
           } |
           Select-Object -First 1

    if ($src) {
        Copy-Item -LiteralPath $src.FullName -Destination $licensePath -Force
        $licenseStatus = 'GPL-3.0'
        Write-Step ('wrote LICENSE (GPL-3.0) copied verbatim from ' + $src.FullName)
    } else {
        $licenseStatus = 'unresolved'
        Write-Warn2 'GPLv3 detected but no in-tree COPYING/LICENSE copy found.'
        Write-Warn2 'Download https://www.gnu.org/licenses/gpl-3.0.txt and save it as LICENSE (verbatim).'
        Write-Warn2 'Do NOT rewrite the GPLv3 text. LICENSE was left untouched.'
    }
}
else {
    $licenseStatus = 'none'
    Write-Step 'license file skipped (-License none)'
}

# ------------------------------------------------------------- 5. docs seeds
$today = Get-Date -Format 'yyyy-MM-dd'
function Write-Seed {
    param([string]$RelativePath, [string]$Content, [switch]$ForceWrite)
    $full = Join-Path $root $RelativePath
    if ((Test-Path -LiteralPath $full) -and -not $ForceWrite) {
        Write-Step ($RelativePath + ' exists, kept')
        return
    }
    $parent = Split-Path -Parent $full
    if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
    Set-Content -LiteralPath $full -Value $Content -Encoding UTF8
    Write-Step ('wrote ' + $RelativePath)
}

$docsReadme = @"
# $AppName Documentation

Single entry point. Every technical document and every pitfall record must be
reachable from here in one hop (workspace-manage section 3).
Rename the two section headings to your project language if needed.

## Technical docs

| Doc | Description | Updated |
|---|---|---|
| [architecture.md](./architecture.md) | Architecture plan and user confirmation | $today |
| [build.md](./build.md) | Build directory ownership and reuse log | $today |
| [release.md](./release.md) | Release records and license | $today |

## Pitfalls

| ID | Symptom | Stage | Link |
|---|---|---|---|
| - | (none yet) | - | - |
"@
Write-Seed -RelativePath 'docs\README.md' -Content $docsReadme -ForceWrite:$Force

$archSeed = @"
# Architecture

## Modules

(pending)

## Dependency direction

    plugins/<name>  ->  api
    host            ->  api
    host            -/-> plugins/<name>   (runtime discovery only, no link-time dependency)

## Plugin contract

(pending)

## User confirmation

- Confirmed by user: NO
- Date: -
- Notes: complete gate G1 of workspace-manage before writing business code.
"@
Write-Seed -RelativePath 'docs\architecture.md' -Content $archSeed -ForceWrite:$Force

$buildSeed = @"
# Build directories

Reuse existing directories. Create a new one only when none matches, then register it here
(workspace-manage section 2.2, qt-msvc-cmake section 4.3).

| Build dir | Generator | Configuration | Purpose | Created | Reused |
|---|---|---|---|---|---|
| - | - | - | - | - | - |
"@
Write-Seed -RelativePath 'docs\build.md' -Content $buildSeed -ForceWrite:$Force

$releaseSeed = @"
# Release records

Artifacts are produced only under dist/ (portable zip + NSIS installer).

| Version | Date | Portable | Installer | License | Verification |
|---|---|---|---|---|---|
| - | - | - | - | - | - |
"@
Write-Seed -RelativePath 'docs\release.md' -Content $releaseSeed -ForceWrite:$Force

$refSeed = @"
# Reference projects

Third-party projects live in this folder. They are READ-ONLY and are NOT committed
(only this README is tracked).

Record every project below, including its license: this record feeds the GPLv3
detection of workspace-manage section 8.

| Project | Source | Fetched | Purpose | License |
|---|---|---|---|---|
| - | - | - | - | - |
"@
Write-Seed -RelativePath 'references\README.md' -Content $refSeed -ForceWrite:$Force

# ----------------------------------------------------------- 6. git init only
$gitInitialized = $false
$gitNote = ''
$gitDir = Join-Path $root '.git'

if (Test-Path -LiteralPath $gitDir) {
    $gitInitialized = $true
    $gitNote = 'repository already present'
    Write-Step 'git repository already present'
}
elseif ($SkipGitInit) {
    $gitNote = 'skipped by -SkipGitInit'
    Write-Step 'git init skipped (-SkipGitInit)'
}
elseif (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    $gitNote = 'git not found in PATH'
    Write-Warn2 'git not found in PATH; git init skipped'
}
else {
    Push-Location $root
    try {
        & git init | Out-Null
        if ($LASTEXITCODE -ne 0) { throw ('git init failed with exit code ' + $LASTEXITCODE) }
        & git symbolic-ref HEAD refs/heads/main | Out-Null
        $gitInitialized = $true
        $gitNote = 'initialized with branch main'
        Write-Step 'git init done (default branch main); NO remote configured'
    }
    finally {
        Pop-Location
    }
}

# ------------------------------------------------------------------ 7. report
Write-Host ''
Write-Host '=== MANDATORY NEXT STEPS ===' -ForegroundColor Yellow
Write-Host '1) Review .gitignore and LICENSE before the first commit.'
Write-Host '2) ASK THE USER: "Do you want to push this project to a remote repository?"'
Write-Host '   - If YES: collect the remote URL, then run'
Write-Host '       .\scripts\init-git-remote.ps1 -RemoteUrl <URL> -CommitMessage "<msg>" -ConfirmPush'
Write-Host '   - If NO : stop here. Do NOT configure a remote. Do NOT push.'
Write-Host '3) Do not create build directories. Reuse the existing ones.'

$summary = [ordered]@{
    skill                  = 'workspace-manage'
    script                 = 'init-workspace.ps1'
    workspaceRoot          = $root
    createdDirs            = @($created)
    gitignore              = '.gitignore'
    gitInitialized         = $gitInitialized
    gitNote                = $gitNote
    remoteConfigured       = $false
    pushed                 = $false
    askedUserAboutRemote   = $true
    referencesDir          = 'references'
    referencesReadme       = 'references\README.md'
    distDir                = 'dist'
    buildDirsCreated       = $false
    license                = [ordered]@{
        chosen  = $licenseStatus
        default = 'MIT'
        evidence = @($evidence)
        files   = @('LICENSE', 'docs\README.md', 'docs\release.md')
    }
}
$summary | ConvertTo-Json -Depth 6
