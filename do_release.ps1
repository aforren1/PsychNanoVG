<#
.SYNOPSIS
Release PsychNanoVG 0.2.0: set the version, verify, push, tag, and check the published zip.

.DESCRIPTION
Runs the checklist in RELEASING.md from a clean main branch that matches a
green origin/main. It sets the version, commits, tags, and pushes commit and
tag together, so CI runs once, on the tag, and that run publishes the GitHub
Release; this script never builds release binaries itself. It stops at the
first failed check. After a red tag run, fix the cause, delete the tag
(git tag -d vX; git push origin :refs/tags/vX) and rerun.

The steps it does not do, because they need judgment: updating SPEC.md,
README.md and DEV.md for the release (step 3), the GL tests and demos under
Psychtoolbox (part of step 2), and reading the generated release notes.

.PARAMETER Version
The new version, for example 0.2.0. It becomes the tag v0.2.0.

.PARAMETER LocalTests
Also run the MATLAB and Octave "build test" suites of step 2 before pushing.
Off by default: the script starts only from a tree identical to a green
origin/main, and CI runs the full matrix again on the tag, so a local run
repeats what CI has done and will do.

.PARAMETER NoWait
Return right after the push and print the CI run to watch. The release still
publishes from that run; step 6, the check of the published zip, is skipped.

.PARAMETER DryRun
Print what would happen. Nothing is edited, committed, pushed or tagged.

.EXAMPLE
.\do_release.ps1 -Version 0.2.0

.EXAMPLE
.\do_release.ps1 -Version 0.2.1 -LocalTests
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version,
    [switch]$LocalTests,
    [switch]$NoWait,
    [switch]$DryRun
)

Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'

# ---- per-repository facts (the only lines that differ between the repos) ----
$Name        = 'PsychNanoVG'                         # tag and release title prefix
$MexName     = 'PsychNanoVG'                         # the MEX function
$VersionField = 'psychnanovg'                        # field of ('Version') that carries the version
$SetupName   = 'PsychNanoVGSetup'                    # setup function at the zip root
$WindowsZip  = 'psychnanovg-matlab-windows.zip'      # asset checked after the release
$VersionEdits = @(
    @{ File = 'src/psychnanovg.c'; Pattern = '#define PNVG_VERSION "\d+\.\d+\.\d+"'; Replacement = '#define PNVG_VERSION "{V}"' },
    @{ File = 'gen/generate.py'; Pattern = 'version = "\d+\.\d+\.\d+\+nanovg\.'; Replacement = 'version = "{V}+nanovg.' }
)
$NeedsGen    = $true                                 # the generator stamps the version into sources
$LocalTestRuns = @(
    @{ Label = 'MATLAB build test'; Exe = 'matlab'; Args = @('-batch', 'build test') },
    @{ Label = 'Octave build test'; Exe = 'octave'; Args = @('--eval', 'build test') }
)
$Matlab = 'C:\Program Files\MATLAB\R2023a\bin\matlab.exe'
$Octave = 'C:\Program Files\GNU Octave\Octave-10.1.0\mingw64\bin\octave-cli.exe'

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root
$Tag = "v$Version"

function Step([string]$text) { Write-Host ""; Write-Host "==> $text" -ForegroundColor Cyan }
function Fail([string]$text) { Write-Host "FAILED: $text" -ForegroundColor Red; exit 1 }
function Run([string]$exe, [string[]]$argv) {
    # Native output goes straight to the console; only the exit code matters here.
    & $exe @argv
    if ($LASTEXITCODE -ne 0) { Fail "$exe $($argv -join ' ') exited with $LASTEXITCODE" }
}
function Invoke-Git([string[]]$argv) { Run 'git' $argv }

# ---- before you start -------------------------------------------------------
Step "Preconditions"
$branch = (git rev-parse --abbrev-ref HEAD).Trim()
if ($branch -ne 'main') { Fail "on branch '$branch', release from main" }
# Submodule content changes are the LVGL patches applied at configure time
# and never part of a release, so they do not count as dirt.
$dirty = git status --porcelain --ignore-submodules=dirty
if ($dirty) { Write-Host $dirty; Fail "the working tree is not clean" }
Invoke-Git @('fetch', '--quiet', 'origin')
$local  = (git rev-parse HEAD).Trim()
$remote = (git rev-parse 'origin/main').Trim()
if ($local -ne $remote) { Fail "main is not in sync with origin/main (local $local, remote $remote)" }
if (git tag --list $Tag) { Fail "tag $Tag already exists" }
try { & gh auth status 2>$null | Out-Null } catch { }
if ($LASTEXITCODE -ne 0) { Fail "gh is not authenticated; run gh auth login" }
# --json plus ConvertFrom-Json: PowerShell 5.1 rewrites the quotes and backslashes
# of a jq string before gh sees them.
$lastRun = (gh run list --branch main --limit 1 --json conclusion,headSha | ConvertFrom-Json)[0]
Write-Host "last CI run on main: $($lastRun.conclusion) $($lastRun.headSha)"
if ($lastRun.conclusion -ne 'success') { Fail "the last CI run on main is not green; fix that first" }
if (Select-String -Path 'README.md' -Pattern 'first release is pending' -Quiet) {
    Write-Host "note: README.md still says the first release is pending; RELEASING.md step 3 says to remove it after this release" -ForegroundColor Yellow
}

# ---- 1. set the version -----------------------------------------------------
Step "Set the version to $Version"
foreach ($edit in $VersionEdits) {
    $path = $edit.File
    $text = Get-Content -Raw -Encoding UTF8 $path
    if (-not ($text -match $edit.Pattern)) { Fail "no version line matching '$($edit.Pattern)' in $path" }
    $new = [regex]::Replace($text, $edit.Pattern, $edit.Replacement.Replace('{V}', $Version), 1)
    if ($new -eq $text) {
        Write-Host "$path already at $Version"
    } elseif ($DryRun) {
        Write-Host "would set the version in $path"
    } else {
        # Keep the file's own line endings and no BOM.
        [System.IO.File]::WriteAllText((Join-Path $Root $path), $new, (New-Object System.Text.UTF8Encoding($false)))
        Write-Host "set the version in $path"
    }
}
if ($NeedsGen) {
    Step "Regenerate the binding so the sources carry the version"
    if ($DryRun) { Write-Host "would run: octave --eval build gen" }
    else { Run $Octave @('--eval', 'build gen') }
}

# ---- 2. verify locally ------------------------------------------------------
if (-not $LocalTests) {
    Step "Local tests not requested; CI on the tag is the check (-LocalTests runs them)"
} elseif ($DryRun) {
    Step "Would run the local tests"; $LocalTestRuns | ForEach-Object { Write-Host "  $($_.Exe) $($_.Args -join ' ')" }
} else {
    foreach ($t in $LocalTestRuns) {
        Step "Local tests: $($t.Label)"
        $exe = if ($t.Exe -eq 'matlab') { $Matlab } else { $Octave }
        # stdout streams to the console and is kept; stderr goes to the console
        # untouched. A 2>&1 here would turn every stderr line, such as an .octaverc
        # warning, into an error record that stops the script.
        & $exe @($t.Args) | Tee-Object -Variable lines | Out-Host
        $out = $lines | Out-String
        if ($LASTEXITCODE -ne 0) { Fail "$($t.Label) exited with $LASTEXITCODE" }
        if (-not ($out -match '==== \d+ passed, 0 failed ====')) { Fail "$($t.Label) did not report 0 failed" }
    }
    Write-Host "GL tests and demos under Psychtoolbox are not automated; RELEASING.md step 2 lists them." -ForegroundColor Yellow
}

if ($DryRun) {
    Step "Dry run: would commit Release $Tag, tag $Tag, push both, wait for the tag run, then check $WindowsZip"
    exit 0
}

# ---- 4 and 5. commit, tag, push once ------------------------------------------
Step "Commit the version and tag $Tag"
$staged = git status --porcelain --ignore-submodules=dirty
if ($staged) {
    Invoke-Git @('add', '-A')
    Invoke-Git @('commit', '-q', '-m', "Release $Tag")
} else {
    Write-Host "nothing to commit; the version was already in place"
}
$sha = (git rev-parse HEAD).Trim()
Invoke-Git @('tag', '-a', $Tag, '-m', "$Name $Tag")
# One push for commit and tag: the tag run builds, tests and publishes, and a
# separate run on the commit would only repeat it.
Invoke-Git @('push', '--quiet', 'origin', 'main', $Tag)

function Find-Run([string]$commit, [string]$ref) {
    # One push of main and the tag starts two runs on the same commit, one per
    # ref, and only the tag run has the release job. Pick it by its ref name.
    for ($i = 0; $i -lt 30; $i++) {
        Start-Sleep -Seconds 10
        $runs = gh run list --commit $commit --limit 10 --json databaseId,headBranch | ConvertFrom-Json
        $hit = $runs | Where-Object { $_.headBranch -eq $ref } | Select-Object -First 1
        if ($hit) { return $hit.databaseId }
    }
    Fail "no CI run appeared for $ref at $commit"
}

$runId = Find-Run $sha $Tag
$runUrl = gh run view $runId --json url --jq .url
if ($NoWait) {
    Write-Host "Pushed $Tag. CI run: $runUrl"
    Write-Host "When it is green the release is published; check it with: gh release view $Tag"
    exit 0
}
Step "Wait for the tag run ($runUrl)"
& gh run watch $runId --exit-status --interval 30
if ($LASTEXITCODE -ne 0) {
    $jobs = (gh run view $runId --json jobs | ConvertFrom-Json).jobs
    $jobs | Where-Object { $_.conclusion -ne 'success' } | ForEach-Object { Write-Host ('  {0}  {1}' -f $_.conclusion, $_.name) }
    Fail "the tag run is red, so nothing was published. Fix the cause, delete the tag (git tag -d $Tag; git push origin :refs/tags/$Tag) and rerun."
}

# ---- 6. check the release ---------------------------------------------------
Step "Check the release"
& gh release view $Tag
if ($LASTEXITCODE -ne 0) { Fail "gh release view $Tag failed; the release job did not publish" }
$dir = Join-Path $env:TEMP ("rel-" + $Name + "-" + $Version)
if (Test-Path $dir) { Remove-Item -Recurse -Force -Confirm:$false $dir }
New-Item -ItemType Directory -Force $dir | Out-Null
Run 'gh' @('release', 'download', $Tag, '--pattern', $WindowsZip, '--dir', $dir)
$unz = Join-Path $dir 'unzipped'
Expand-Archive -Path (Join-Path $dir $WindowsZip) -DestinationPath $unz
# A fresh MATLAB follows the README install steps literally, with nothing else on the path.
$check = "addpath('$unz'); $SetupName; v = $MexName('Version'); fprintf('RELEASE_VERSION=%s\n', v.$VersionField);"
& $Matlab -batch $check | Tee-Object -Variable lines | Out-Host
$out = $lines | Out-String
if (-not ($out -match "RELEASE_VERSION=$([regex]::Escape($Version))")) { Fail "the downloaded zip does not report version $Version" }
Write-Host ""
Write-Host "Released $Name $Tag" -ForegroundColor Green
gh release view $Tag --json url --jq .url
Write-Host "Left to do by hand: read the generated notes (gh release view $Tag --web), and for the first release remove the pending note from README.md."
