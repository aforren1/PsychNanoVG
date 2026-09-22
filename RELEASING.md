# Releasing PsychNanoVG

This is the checklist for publishing a release. A release is a `v*` git tag.
CI builds the packages, runs every test, and publishes a GitHub Release with
one zip per engine and platform. You never build release binaries by hand.

## Before you start

- `git status` is clean on `main`, and the last CI run on `main` is green:
  `gh run list --limit 1`.
- You know the new version. Before 1.0: a new subcommand or helper bumps the
  minor number, a fix bumps the patch number. A change that breaks a script
  that worked before bumps the minor number and gets a line in the release
  notes that says so.

## 1. Set the version

The version lives in two places, and the generator stamps the second one
into the sources, so `build gen` is part of a version change here:

| File | Line | Reaches |
|---|---|---|
| `src/psychnanovg.c` | `#define PNVG_VERSION "0.1.0"` | `PsychNanoVG('Version').psychnanovg` |
| `gen/generate.py` | `version = "0.1.0+nanovg.%s" % nanovg_commit()` | `pnvg_version_string` in `src/gen_dispatch.c` and the `Version:` line of `m/PsychNanoVG.m` |

Set both to the same number, then run `build gen` in either engine so
`src/gen_dispatch.c` and `m/PsychNanoVG.m` carry it. Commit the regenerated
files; users build without Python.

## 2. Verify locally

Run both engines. Each command builds, then runs the suite. Under MATLAB
with Psychtoolbox installed the suite also runs the three `tests/gl` files
against a real window; under Octave they report as skipped.

```
"C:\Program Files\MATLAB\R2023a\bin\matlab.exe" -batch "build test"
"C:\Program Files\GNU Octave\Octave-10.1.0\mingw64\bin\octave-cli.exe" --eval "build test"
```

Expect `0 failed` from both. The MATLAB count is higher because it includes
the GL tests. Then the demo, in MATLAB:

```matlab
PsychNanoVGDemo([], 5)
```

Expect exit without error and an `EndFrame` timing line. `build smoke` runs
the native GL smoke test as well; it needs no engine.

## 3. Update the documents

- `SPEC.md`: the status line at the top names the phase that is implemented.
  Anything that changed against the specification gets a row in section 14.
- `README.md`: new subcommands or helpers appear where their group is
  described.
- `third_party/PINS.md`: only if the NanoVG submodule moved. Then `build
  gen` again, because the generator stamps the NanoVG commit into the
  version string, and review the generated diff: a NanoVG API change shows
  up there first.

## 4. Push and wait for green

```
git add -A
git commit -m "Release v0.2.0"
git push
gh run watch
```

The release job only runs on a tag, so this push runs the matrix without
publishing. Wait for it to pass before tagging. A tag on a red commit
produces no release, because the release job needs every other job.

## 5. Tag

```
git tag -a v0.2.0 -m "PsychNanoVG v0.2.0"
git push origin v0.2.0
gh run watch
```

The tag push runs the matrix again. When every job passes, the `release`
job downloads the packages, zips each one, and runs
`gh release create v0.2.0 *.zip --title "PsychNanoVG v0.2.0" --generate-notes`.
The notes list the commits and pull requests since the previous tag.

## 6. Check the release

```
gh release view v0.2.0
gh release download v0.2.0 --pattern "psychnanovg-matlab-windows.zip" --dir %TEMP%\rel
```

Unzip into an empty folder and, in a fresh MATLAB:

```matlab
addpath('m'); PsychNanoVGSetup(); PsychNanoVG('Version')
```

The `psychnanovg` field must show the new version and the `nanovg` field the
pinned commit. Do the same for one Octave zip when you changed anything
Octave specific.

## What a release contains

| Zip | Built on | Runs on |
|---|---|---|
| `psychnanovg-matlab-linux.zip` | MATLAB R2021b, Ubuntu 22.04 | MATLAB R2021b and later on Linux |
| `psychnanovg-matlab-windows.zip` | MATLAB R2022b, Windows Server 2022 | MATLAB R2022b and later on Windows |
| `psychnanovg-octave-linux-6.4.zip` | Octave 6.4.0 | Octave 6.x through 9.x on Linux |
| `psychnanovg-octave-linux-10.zip` | Octave 10.1.0 | Octave 10.x and later on Linux |
| `psychnanovg-octave-windows.zip` | Octave 10.1.0 official zip | Octave 10.x on Windows |
| `psychnanovg-matlab-macos.zip` | MATLAB R2023b, `macos-latest` | MATLAB R2023b and later on Apple silicon Macs |
| `psychnanovg-octave-macos.zip` | Homebrew Octave, `macos-latest` | Homebrew Octave on Apple silicon Macs |

Each zip holds `dist/<arch>/PsychNanoVG.<mexext>`, `m/`, `README.md`, and
`SPEC.md`, and is a complete install for that engine and platform.

The two macOS packages are built on `macos-latest`, which is Apple silicon,
so they are `maca64` and they do not run on an Intel Mac. Nobody on the team
has a Mac, so the three macOS jobs carry `continue-on-error` until the first
green run; check them before you tag, because a release can otherwise go out
without them.

## If the release job fails

1. Read the failing job: `gh run view --log-failed`.
2. Fix on `main`, push, and wait for green.
3. Remove the tag and any partial release, then tag again:

```
gh release delete v0.2.0 --yes
git tag -d v0.2.0
git push --delete origin v0.2.0
git tag -a v0.2.0 -m "PsychNanoVG v0.2.0"
git push origin v0.2.0
```

Do not reuse a version number for different binaries once a release with
that tag has been downloaded by anyone. Bump the patch number instead.

## Known limits of the process

- The version string is set by hand in two files and is not derived from the
  tag. If they disagree, the tag wins for users, so check step 1.
- Release notes are generated from commit and pull request titles. Write
  titles that read well in a list.
- The release job has never run for real yet; its zip step was exercised
  locally. The first tag is the first end to end test.
