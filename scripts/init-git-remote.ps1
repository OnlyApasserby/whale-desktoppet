<#
.SYNOPSIS
    Configure the git remote and push, only after the user has confirmed.

.DESCRIPTION
    This is the second half of the version-control flow of the workspace-manage
    skill (section 4). init-workspace.ps1 runs "git init" and stops. Pushing to a
    remote repository requires an explicit user decision, therefore this script
    refuses to run unless ALL of the following are provided:

      -RemoteUrl    the remote repository location given by the user
      -ConfirmPush  the recorded user confirmation

    Safety rules enforced here:
      - never invents or guesses a remote URL
      - never runs "git push --force" and never rewrites history
      - never commits secrets; review .gitignore first
      - never passes --no-verify

.EXAMPLE
    .\init-git-remote.ps1 -RemoteUrl https://example.com/me/mytool.git -CommitMessage "chore: initial commit" -ConfirmPush
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$RemoteUrl,

    [string]$WorkspaceRoot = '.',
    [string]$Branch = 'main',
    [string]$RemoteName = 'origin',
    [string]$CommitMessage = '',
    [switch]$ConfirmPush
)

$ErrorActionPreference = 'Stop'

function Write-Step { param([string]$Text) Write-Host ('[remote] ' + $Text) -ForegroundColor Cyan }

if (-not $ConfirmPush) {
    throw @'
Refused: -ConfirmPush was not supplied.

Pushing to a remote repository requires an explicit user decision.
Ask the user first ("Do you want to push this project to a remote repository?"),
collect the remote URL, then re-run with -ConfirmPush.
'@
}

if ([string]::IsNullOrWhiteSpace($RemoteUrl)) {
    throw 'Refused: -RemoteUrl is empty. Never invent or guess a remote location.'
}

if ($RemoteUrl -match '\s') {
    throw 'Refused: -RemoteUrl contains whitespace. Provide a clean URL (HTTPS or SSH).'
}

$root = (Resolve-Path -LiteralPath $WorkspaceRoot).Path
if (-not (Test-Path -LiteralPath (Join-Path $root '.git'))) {
    throw ('No git repository at ' + $root + '. Run scripts\init-workspace.ps1 first.')
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw 'git not found in PATH.'
}

Push-Location $root
try {
    # ---- guard: never commit obvious local secrets
    $dirty = @(& git status --porcelain --untracked-files=all)
    $risky = @($dirty | Where-Object { $_ -match '(\.env|\.pem|\.key|id_rsa|\.local$)' })
    if ($risky.Count -gt 0) {
        throw ('Refused: potential secret files are not ignored:' + [Environment]::NewLine + ($risky -join [Environment]::NewLine))
    }

    # ---- remote
    $existing = @(& git remote)
    if ($existing -contains $RemoteName) {
        & git remote set-url $RemoteName $RemoteUrl
        Write-Step ('updated remote ' + $RemoteName)
    } else {
        & git remote add $RemoteName $RemoteUrl
        Write-Step ('added remote ' + $RemoteName)
    }
    if ($LASTEXITCODE -ne 0) { throw ('git remote configuration failed with exit code ' + $LASTEXITCODE) }

    # ---- commit (only when a message was supplied)
    $hasHead = $true
    & git rev-parse --verify HEAD *> $null
    if ($LASTEXITCODE -ne 0) { $hasHead = $false }

    if (-not [string]::IsNullOrWhiteSpace($CommitMessage)) {
        & git add -A
        if ($LASTEXITCODE -ne 0) { throw ('git add failed with exit code ' + $LASTEXITCODE) }
        & git commit -m $CommitMessage
        if ($LASTEXITCODE -ne 0) { throw ('git commit failed with exit code ' + $LASTEXITCODE) }
        Write-Step 'committed working tree'
    } elseif (-not $hasHead) {
        throw 'Refused: the repository has no commit yet. Re-run with -CommitMessage "<msg>".'
    }

    # ---- branch and push
    & git branch -M $Branch
    if ($LASTEXITCODE -ne 0) { throw ('git branch -M failed with exit code ' + $LASTEXITCODE) }

    & git push -u $RemoteName $Branch
    if ($LASTEXITCODE -ne 0) { throw ('git push failed with exit code ' + $LASTEXITCODE) }
    Write-Step ('pushed ' + $Branch + ' to ' + $RemoteName)
}
finally {
    Pop-Location
}

$summary = [ordered]@{
    skill            = 'workspace-manage'
    script           = 'init-git-remote.ps1'
    workspaceRoot    = $root
    remoteName       = $RemoteName
    remoteUrl        = $RemoteUrl
    branch           = $Branch
    remoteConfigured = $true
    pushed           = $true
    userConfirmedPush = $true
    forceUsed        = $false
    commitMessage    = $CommitMessage
}
$summary | ConvertTo-Json -Depth 6
