<#
.SYNOPSIS
Release PsychNanoVG 0.2.0: set the version, verify, push, tag, and check the published zip.

.DESCRIPTION
Runs the checklist in RELEASING.md from a clean main branch. CI builds every
package and publishes the GitHub Release when the tag lands; this script never
builds release binaries itself. It stops at the first failed check and leaves
the tree in the state it reached, so a rerun after a fix continues from the
version commit.

The steps it does not do, because they need judgment: updating SPEC.md,
README.md and DEV.md for the release (step 3), the GL tests and demos under
Psychtoolbox (part of step 2), and reading the generated release notes.

.PARAMETER Version
The new version, for example 0.2.0. It becomes the tag v0.2.0.

.PARAMETER SkipLocalTests
Skip the MATLAB and Octave "build test" runs of step 2. Use it only when they
ran already on this exact tree.

.PARAMETER DryRun
Print what would happen. Nothing is edited, committed, pushed or tagged.

.EXAMPLE
.\do_release.ps1 -Version 0.2.0

.EXAMPLE
.\do_release.ps1 -Version 0.2.1 -SkipLocalTests
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version,
    [switch]$SkipLocalTests,
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
$LocalTests  = @(
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
& gh auth status 2>$null | Out-Null
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
if ($SkipLocalTests) {
    Step "Local tests skipped on request"
} elseif ($DryRun) {
    Step "Would run the local tests"; $LocalTests | ForEach-Object { Write-Host "  $($_.Exe) $($_.Args -join ' ')" }
} else {
    foreach ($t in $LocalTests) {
        Step "Local tests: $($t.Label)"
        $exe = if ($t.Exe -eq 'matlab') { $Matlab } else { $Octave }
        $out = & $exe @($t.Args) 2>&1 | Tee-Object -Variable captured | Out-String
        Write-Host $out
        if ($LASTEXITCODE -ne 0) { Fail "$($t.Label) exited with $LASTEXITCODE" }
        if (-not ($out -match '==== \d+ passed, 0 failed ====')) { Fail "$($t.Label) did not report 0 failed" }
    }
    Write-Host "GL tests and demos under Psychtoolbox are not automated; RELEASING.md step 2 lists them." -ForegroundColor Yellow
}

if ($DryRun) {
    Step "Dry run: would commit 'Release $Tag', push, wait for CI, tag $Tag, push the tag, wait again, then check $WindowsZip"
    exit 0
}

# ---- 4. push and wait for green ---------------------------------------------
Step "Commit and push the version"
$staged = git status --porcelain --ignore-submodules=dirty
if ($staged) {
    Invoke-Git @('add', '-A')
    Invoke-Git @('commit', '-q', '-m', "Release $Tag")
} else {
    Write-Host "nothing to commit; the version was already in place"
}
Invoke-Git @('push', '--quiet', 'origin', 'main')
$sha = (git rev-parse HEAD).Trim()

function Wait-Run([string]$commit, [string]$what) {
    Step "Wait for CI on $what ($commit)"
    $id = $null
    for ($i = 0; $i -lt 30 -and -not $id; $i++) {
        Start-Sleep -Seconds 10
        $id = gh run list --commit $commit --limit 1 --json databaseId --jq '.[0].databaseId'
    }
    if (-not $id) { Fail "no CI run appeared for $commit" }
    Write-Host "run $id"
    & gh run watch $id --exit-status --interval 30
    if ($LASTEXITCODE -ne 0) {
        $jobs = (gh run view $id --json jobs | ConvertFrom-Json).jobs
        $jobs | Where-Object { $_.conclusion -ne 'success' } | ForEach-Object { Write-Host ('  {0}  {1}' -f $_.conclusion, $_.name) }
        Fail "CI is red for $what; fix it, then rerun this script with the same -Version"
    }
}
Wait-Run $sha 'the release commit'

# ---- 5. tag -----------------------------------------------------------------
Step "Tag $Tag"
Invoke-Git @('tag', '-a', $Tag, '-m', "$Name $Tag")
Invoke-Git @('push', '--quiet', 'origin', $Tag)
Wait-Run $sha "the tag $Tag"

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
$out = & $Matlab -batch $check 2>&1 | Out-String
Write-Host $out
if (-not ($out -match "RELEASE_VERSION=$([regex]::Escape($Version))")) { Fail "the downloaded zip does not report version $Version" }
Write-Host ""
Write-Host "Released $Name $Tag" -ForegroundColor Green
gh release view $Tag --json url --jq .url
Write-Host "Left to do by hand: read the generated notes (gh release view $Tag --web), and for the first release remove the pending note from README.md."
