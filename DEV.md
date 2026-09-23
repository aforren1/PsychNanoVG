# Developing PsychNanoVG

This file is for contributors. It tells you how to get the sources, build
the MEX, run the tests, and profile it. [README.md](README.md) is for users
of a release zip. [SPEC.md](SPEC.md) is the design reference.
[RELEASING.md](RELEASING.md) is the release checklist.

## Status

| Part | State |
|---|---|
| Generated API, 96 subcommands, 121 in total | Works. Tested with the null renderer under MATLAB R2023a and Octave 10.1. |
| Batched paths, paints, fonts, images, `Stats` | Works. |
| Arcs and shapes in the `Path` matrix, `StrokeSegments`, `PsychNanoVGPolylineGradient` | Works. Phase 2. Covered by the null renderer suite, by `tests/gl/test_gl_paths`, and by the native smoke test. |
| Psychtoolbox integration, `tests/gl/` | Works under MATLAB. With the suite that needs no GPU, 430 assertions pass, including shapes, text metrics, the render target round trip, an arc gauge, a gradient polyline, and two windows. Octave passes the 358 that need no GPU. |
| Several windows, one context each | Works. Phase 3. Tested with two Psychtoolbox windows under MATLAB, with the null renderer everywhere, and with two GL contexts in the native smoke test. |
| Render targets, `CreateImageFromTexture` | Works. Covered by `tests/gl/test_gl_target` and by the native smoke test. |
| `tests/gl/` under Octave | Skipped. The Psychtoolbox `Screen` MEX for Octave does not load on the development machine. |
| `m/PsychNanoVGDemo` | Runs under MATLAB. It holds a full screen window for six seconds by default. |
| Linux | Works. Built and tested with Octave 6.4 on Ubuntu 22.04, and `smoke_gl` runs under Xvfb with Mesa llvmpipe. |
| GLES2 and GLES3 on Linux | A build option. Phase 3. The smoke test passes in an EGL pbuffer with Mesa. Not tried in a Psychtoolbox window, because that needs a Psychtoolbox Waffle build. |
| macOS on Apple silicon | Built and tested on the `macos-latest` runner, with the GL2 backend. The GL 2.1 context has no GPU timer unless it offers `GL_ARB_timer_query`, so `gpuNs` can be NaN there. |
| macOS on Intel | Not covered. |
| Tracy | Works when you turn it on. CPU zones for every subcommand and the hot paths, and a GPU zone per frame. Built and captured on Windows with MSVC and with Octave's MinGW. Not tried on Linux or macOS. |

See the last section of `SPEC.md` for the full list of deviations.

## Requirements for building

- MATLAB at or above the release that CI builds on for the platform: R2021b
  on Linux, R2022b on Windows, or R2023b on macOS. CI then tests each of those
  binaries on the latest MATLAB. The development machine uses R2023a.
- Or GNU Octave: 6.4 or later on Linux (CI builds on 6.4.0 and on 10.1.0),
  10.1 on Windows, or Homebrew Octave on macOS.
- A C compiler. MATLAB uses MSVC 2022 on Windows. Octave uses the MinGW gcc
  that it ships with.
- CMake 3.16 or later.
- Psychtoolbox 3.0.19 or later, to draw in a window. The tests that do not
  touch the GPU run without it.
- A GPU with OpenGL 3.3 on Windows and Linux, or OpenGL 2.1 on macOS.

## Get the sources

Clone the repository with its submodules:

    git clone --recurse-submodules https://github.com/aforren1/PsychNanoVG.git
    cd PsychNanoVG

NanoVG is a submodule. If the clone above was made without
`--recurse-submodules`, run `git submodule update --init --recursive`, or clone
the commit recorded in `third_party/PINS.md` by hand:

    git clone https://github.com/memononen/nanovg.git third_party/nanovg

## Build

Build from the repository root:

    matlab -batch build
    octave-cli --eval build

`build.m` compiles the static library with CMake and then calls `mex`. The
two engines use different compilers, so each gets its own build directory
(`build-matlab`, `build-octave`) and its own install directory
(`inst-matlab`, `inst-octave`). On Linux and macOS the names carry a platform
suffix, such as `build-octave-linux`, because one working tree is often shared
between Windows and WSL and an object file from one toolchain makes CMake
refuse to configure for the other. The MEX goes to
`dist/<arch>/PsychNanoVG.<mexext>`, where `<arch>` is `win64`, `glnxa64`,
`maci64`, or `maca64`.

`dist/` is split by platform because Octave names its MEX `PsychNanoVG.mex` on
every operating system. Without the split, a Linux build in a tree shared with
Windows would replace the Windows one. A MATLAB and an Octave build for the
same platform sit side by side, because their file extensions differ.

Other targets:

| Command | Effect |
|---|---|
| `build` | Build the library and the MEX. |
| `build gen` | Run the binding generator first, then build. |
| `build test` | Build, then run `tests/run_tests`. |
| `build smoke` | Build the native GL smoke test and run it. |
| `build clean` | Remove the build and install directories. |

Set `MEX_CMAKE_GENERATOR` to choose a different CMake generator.

### Build for GLES on Linux

Psychtoolbox makes an OpenGL ES context only with its Waffle display
backends, on Linux: the Wayland `Screen` that ships in
`PsychBasic/Octave5LinuxFiles64/Wayland`, or an embedded build. Set
`PSYCH_USE_GFX_BACKEND=gles2` or `gles3` before Psychtoolbox starts. The
MEX then needs the matching NanoVG backend:

    PSYCHNANOVG_GLES=3 octave-cli --eval build

The value is `2` or `3`. The GLES backend replaces the GL3 backend, so this
MEX works only in a GLES window, and `PsychNanoVG('Version').backend` says
`GLES3`. Build again without the variable to get the normal MEX back. The
GLES backends have no GPU timer, so `gpuNs` in `Stats` is NaN. NanoVG has no
GLES1 backend. SPEC section 14.8 has the details.

To run the smoke test on the GLES backend in an EGL pbuffer, without a
display:

    cmake -S . -B build-gles3 -DPSYCHNANOVG_GLES=3 -DPSYCHNANOVG_SMOKE_GL=ON
    cmake --build build-gles3 --target smoke_gl
    ./build-gles3/smoke_gl

It needs the EGL and GLES development files, such as `libegl-dev` and
`libgles-dev` on Ubuntu.

## Put a source checkout on the path

`PsychNanoVGSetup` picks the `dist/<arch>` directory for the engine and the
platform you are on, and puts it ahead of `m/`. The order matters, because a
MEX file only takes precedence over an M-file of the same name inside one
directory, and `m/PsychNanoVG.m` holds the help text.

    addpath(root);
    PsychNanoVGSetup();

`root` is the repository root. The older form, with `m/` first, also works:

    addpath(fullfile(root, 'm'));
    PsychNanoVGSetup();

It raises `psychnanovg:NotBuilt` and names the file it looked for when the MEX
for this platform is missing.

`PsychNanoVGSetup('remove')` takes `dist/<arch>`, `m/`, and the root off the
path again. It shuts down every context and clears the MEX first, because a
path change while a locked MEX file is loaded crashes Octave 10.1. When the
MEX stays locked, it raises `psychnanovg:Locked` and changes nothing. A
second argument `'save'` runs `savepath` after `add` or `remove`.

Two identical copies of `PsychNanoVGSetup.m` exist: one in the root and one
in `m/`. A release zip needs the root copy, because a new user can call it
before `m/` is on the path. The helpers in `m/` call the `m/` copy. A
function cannot hand over to another file of the same name, because the
calling file and the current folder win the name lookup, so each copy does
the whole job. Each copy finds the root from its own location. Change one,
then copy it over the other; `run_tests` fails when they differ.

## Test

    matlab -batch "addpath('tests'); run_tests"
    octave-cli --eval "addpath('tests'); run_tests"

`run_tests` calls `PsychNanoVGSetup` itself, so no other path setup is needed.

The tests in `tests/` need no GPU. They use the null renderer: a NanoVG
context whose backend callbacks do nothing. `test_helpers` also puts a
recording `Screen` stub from `tests/stub/` on the path for its own duration,
so the convenience layer is checked without Psychtoolbox: the region opens
once, closes once, and closes again on an error. Path building, the state stack,
text layout, the handle tables, and all of the marshaling run for real. Only
the OpenGL calls are absent.

`test_setup` runs `PsychNanoVGSetup('remove')`, a second remove that must
not change the path, and an `add` from the root copy. `run_tests` calls it
before the first call loads the MEX file, for the same Octave 10.1 reason.
It does not test `'save'`, because that would rewrite the saved path of the
machine.

`test_contexts` checks several contexts with the null renderer: switching,
handles, per-context state and Stats, `Shutdown` of a context that is not
current, and the MEX lock.

The tests in `tests/gl/` need Psychtoolbox and a GPU. `run_tests` reports
them as skipped when `Screen` does not answer. `test_gl_contexts` opens two
small windows side by side. When a second window does not open, it puts both
contexts into one window and skips the checks that need two.

Every script that opens a window goes through `tests/gl/ptb_test_window.m`.
That helper sets `SkipSyncTests` and `VisualDebugLevel`, so an unattended run
does not stop for the display sync report or the welcome splash. The shipped
demos use `m/private/psychnanovg_demo_window.m` instead, a copy with the same
body, because a release zip has no `tests/` and a demo must not change the
path. `run_tests` fails when the two bodies differ.

`tests/smoke_gl.c` is a native program that opens its own off-screen window
and OpenGL context, with WGL on Windows and GLX elsewhere. It exercises the
real GL path without Psychtoolbox, and it is the only GL coverage under Octave
and on CI. It also makes a second NanoVG context and a second GL context, to
check that contexts keep their state apart and refuse the wrong GL context:

    matlab -batch "build smoke"
    octave-cli --eval "build smoke"

On Linux `build smoke` runs it through `xvfb-run`, so it needs no display.

### Check the Linux build under WSL

One working tree can serve Windows and WSL at once, because the build
directories and `dist/` are split by platform. From a WSL shell with Octave,
CMake, and the OpenGL development files installed:

    cd /mnt/c/path/to/PsychNanoVG
    octave-cli --eval build
    octave-cli --eval "addpath('tests'); run_tests"
    octave-cli --eval "build smoke"

The MEX goes to `dist/glnxa64/`, beside the Windows one in `dist/win64/`.
`build smoke` runs `smoke_gl` through `xvfb-run`, so WSL needs no display.

## Continuous integration

`.github/workflows/ci.yml` runs on every push and every pull request. This
project is its own repository, so the workflow needs no path filter and every
step runs at the repository root.

| Job | What it does |
|---|---|
| `matlab-build` | Builds and tests on the floor release: MATLAB R2021b on Ubuntu 22.04 and R2022b on Windows 2022. Uploads the package. |
| `matlab-test-forward` | Downloads that exact binary and tests it on the latest MATLAB, on Linux and on Windows. No rebuild. |
| `octave-build` | Builds and tests in the `gnuoctave/octave` Docker images for 6.4.0 and 10.1.0, one per binary compatible era. Uploads both packages. |
| `octave-test-forward` | Tests the 6.4 binary on Octave 7.3 and 9.4, and the 10.1 binary on 10.3 and 11.3. No rebuild. |
| `octave-windows` | Builds and tests with the official GNU Octave Windows zip (10.1.0, cached), using the toolchain and `make` it ships, as on a developer machine. Uploads the package. |
| `smoke-gl-linux` | Builds `smoke_gl` and runs it under `xvfb-run` with Mesa llvmpipe. This is the only automated OpenGL coverage. |
| `smoke-gl-windows-compile` | Compiles `smoke_gl` with MSVC. The hosted Windows runner has no GPU, so it is not run. |
| `octave-macos` | Builds and tests with Homebrew Octave on Apple silicon. Uploads the package. |
| `smoke-gl-macos` | Builds `smoke_gl` and runs it against a CGL context with no drawable, rendering into a render target. |
| `release` | On a `v*` tag, zips every package and publishes a GitHub Release with `gh release create`. |

No runner has a GPU, so every job runs the `renderer='null'` suite. The tests
in `tests/gl/` need Psychtoolbox and a display, so `run_tests` reports them as
skipped on CI.

Each artifact holds only its own `dist/<arch>/`, plus `m/`,
`PsychNanoVGSetup.m`, `README.md`, `SPEC.md`, and
`docs/images/psychnanovg-demo.png`, so one download is a complete install for
that engine and platform, and the README picture shows in it. No artifact
needs a file from `tests/`.
Find them under Artifacts on the run summary page, or on the Releases page for
a tag.

`third_party/nanovg` becomes a git submodule of this repository. Until then
each build job clones the commit that `third_party/PINS.md` records, but only
when `third_party/nanovg/src/nanovg.h` is missing, so the workflow works
before and after the change.

## Regenerate the binding

    matlab -batch "build gen"

The generator reads `third_party/nanovg/src/nanovg.h`, applies the exclusions
and the renames in `gen/allowlist.txt`, and writes `src/gen_dispatch.c`,
`src/gen_enums.c`, `m/PsychNanoVG.m`, `m/PsychNanoVGOp.m`, and
`tests/test_gen_marshal.m`. It needs Python 3.10 or later, and it runs
through `uv`. The generated files are committed, so you only need the
generator when NanoVG changes.

A function whose signature no rule in SPEC 7.2 covers is dropped and
reported. The generator does not guess.

## Files

| Path | Contents |
|---|---|
| `src/psychnanovg.c` | The MEX entry point, dispatch, marshaling, lifecycle, contexts. |
| `src/core/` | The NanoVG-facing layer, with no MATLAB types. The smoke test links it. |
| `src/pnvg_gl.c` | glad, the NanoVG GL or GLES backend, the proc loader, GL state save and restore, the GPU timer. |
| `src/pnvg_batch.c` | `Polyline`, `Polygon`, `Path`, `Circles`, `Rects`, `StrokeSegments`. |
| `src/pnvg_profiler.h`, `src/core/pnvg_tracy.cpp` | The Stats switch and the Tracy zones. The C++ file is compiled only with Tracy. |
| `src/pnvg_targets.c` | Render targets and `CreateImageFromTexture`. |
| `src/gen_dispatch.c`, `src/gen_enums.c` | Generated. Committed. |
| `gen/generate.py` | The generator. Run it with `build gen`. |
| `PsychNanoVGSetup.m` | The path setup, a copy of `m/PsychNanoVGSetup.m` for a new user who has only the package root on the path. |
| `m/` | The convenience layer, help text, opcodes, the path setup, the font search, the gradient polyline, the demos. |
| `m/private/psychnanovg_demo_window.m` | The window helper of the demos, a copy of `tests/gl/ptb_test_window.m`. Private, so it is on no path. |
| `docs/images/psychnanovg-demo.png` | The README picture. Every package ships it. |
| `tests/` | The suite that needs no GPU, plus `tests/gl/` and `smoke_gl.c`. |
| `README.md`, `DEV.md`, `SPEC.md`, `RELEASING.md` | User guide, this guide, the design reference, the release checklist. |
| `perf/` | The timings of SPEC 9.4, and the cost of `SetContext`. |
| `tools/CaptureReadmeScreenshot.m` | Makes `docs/images/psychnanovg-demo.png`, the picture at the top of `README.md`. A source-tree tool: it puts `m/` on the path once, before the MEX loads. |

## Measure

`Stats` is always compiled in. It counts every subcommand and every frame.

    s = PsychNanoVG('Stats');
    s.endFrameNs        % the last frame, in nanoseconds
    s.endFrameMaxNs
    s.gpuNs             % from a GL timer query pair, read two frames later
    s.drawCalls         % NanoVG counters for the last frame
    s.commands          % per subcommand: name, calls, totalNs, maxNs
    PsychNanoVG('Stats', 'reset');

`gpuNs` is NaN when the context cannot measure GPU time: with the null
renderer, and on a GL context below 3.3 that has no `GL_ARB_timer_query`.

`endFrameNs` times NanoVG's own work. The graphics driver can submit the
commands a little later, still inside `EndFrame`, so for heavy frames the
`EndFrame` row of `s.commands` is the better measure of what the frame costs
the CPU.

### Profile with Tracy

[Tracy](https://github.com/wolfpld/tracy) shows every subcommand as a CPU
zone and every frame as a GPU zone. It is not part of a normal build.

1. Clone the tested version into `third_party/tracy`:

       git clone --branch v0.11.1 https://github.com/wolfpld/tracy.git third_party/tracy

2. Build with the environment variable set:

       set PSYCHNANOVG_TRACY=1
       matlab -batch build

   That is the Windows command prompt. In PowerShell, set it with
   `$env:PSYCHNANOVG_TRACY = '1'`. On Linux and macOS, write
   `PSYCHNANOVG_TRACY=1 matlab -batch build`. Octave builds the same way.
   `Version().build` then ends in `tracy=1`. Without the variable, the next
   build is a normal one again.

3. Start the Tracy profiler, or `tracy-capture -o run.tracy`, and run the
   script. The profiler starts with the first `PsychNanoVG` call and stops
   when MATLAB unloads the MEX file.

The GPU zone "NanoVG frame" spans `BeginFrame` to `EndFrame` and uses the same
timer queries as `gpuNs`. On a context without timer queries there is no GPU
zone. `tracy-csvexport` exports the CPU zones.

If `PSYCHNANOVG_TRACY` is set and `third_party/tracy` is missing, `build`
stops with `build:tracy` and prints the clone command.

## macOS notes

- CI builds and tests on the `macos-latest` runner, which is Apple silicon.
  The packages are `maca64` and do not run on an Intel Mac. Intel Macs are
  not covered.
- macOS gets the NanoVG GL2 backend, because Psychtoolbox makes legacy
  OpenGL 2.1 contexts there (SPEC section 4.1). `Init` accepts `auto`,
  `null`, and the one GL backend that the build has.
- The GL 2.1 context has no GPU timer unless it offers
  `GL_ARB_timer_query`, so `gpuNs` in `Stats` can be NaN.
- `smoke-gl-macos` renders into a render target in a CGL context with no
  drawable, because the runner has no display.
- Nobody on the team has a Mac. Before you tag a release, check that the
  macOS jobs passed; RELEASING.md explains why.

## Releasing

A release is a `v*` tag; CI builds and publishes the packages. The
step-by-step checklist, including where the version string lives and how to
recover from a failed release job, is in [RELEASING.md](RELEASING.md).

Each release zip holds `PsychNanoVGSetup.m`, `dist/<arch>/`, `m/` (with
`m/private/`), `README.md`, `SPEC.md`, and `docs/images/psychnanovg-demo.png`,
with no top folder. The `path:` list of each `Upload package` step in
`.github/workflows/ci.yml` decides that content. A new file that users need at
run time must go into those lists. A shipped M-file must not need `tests/` or
change the path at run time.
