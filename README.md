# PsychNanoVG

`PsychNanoVG` draws antialiased 2D vector graphics in a Psychtoolbox (PTB)
window, from MATLAB and from GNU Octave. It is a MEX binding of
[NanoVG](https://github.com/memononen/nanovg).

`Screen` draws rectangles, ovals, lines, polygons, and text. `PsychNanoVG`
adds Bezier paths, stroke joins and caps, gradients, image patterns, and
TrueType text with exact glyph metrics. The GPU does the antialiasing. The
MEX draws into the same framebuffer as `Screen`, so you can mix the two in
one frame.

`SPEC.md` is the design reference. This file tells you how to build the
binding, how to test it, and how to use it.

## Status

| Part | State |
|---|---|
| Generated API, 96 subcommands, 119 in total | Works. Tested with the null renderer under MATLAB R2023a and Octave 10.1. |
| Batched paths, paints, fonts, images, `Stats` | Works. |
| Psychtoolbox integration, `tests/gl/` | Works under MATLAB. 230 assertions pass, including shapes, text metrics, and the render target round trip. |
| Render targets, `CreateImageFromTexture` | Works. Covered by `tests/gl/test_gl_target` and by the native smoke test. |
| `tests/gl/` under Octave | Skipped. The Psychtoolbox `Screen` MEX for Octave does not load on the development machine. |
| `m/PsychNanoVGDemo` | Written, not run. It holds a full screen window for six seconds. |
| Linux | Works. Built and tested with Octave 6.4 on Ubuntu 22.04, and `smoke_gl` runs under Xvfb with Mesa llvmpipe. |
| macOS | Written, not run. |
| Tracy | A CMake option that is off. Only the dispatch zone is wired up. |

See the last section of `SPEC.md` for the full list of deviations.

## Requirements

- MATLAB R2023a or later, or GNU Octave 10.1 or later.
- A C compiler. MATLAB uses MSVC 2022 on Windows. Octave uses the MinGW gcc
  that it ships with.
- CMake 3.16 or later.
- Psychtoolbox 3.0.19 or later, to draw in a window. The tests that do not
  touch the GPU run without it.
- A GPU with OpenGL 3.3 on Windows and Linux, or OpenGL 2.1 on macOS.

## Build

Clone the repository with its submodules:

    git clone --recurse-submodules https://github.com/aforren1/PsychNanoVG.git
    cd PsychNanoVG

NanoVG is a submodule. If the clone above was made without
`--recurse-submodules`, run `git submodule update --init --recursive`, or clone
the commit recorded in `third_party/PINS.md` by hand:

    git clone https://github.com/memononen/nanovg.git third_party/nanovg

Then build:

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

### Put it on the path

`PsychNanoVGSetup` picks the `dist/<arch>` directory for the engine and the
platform you are on, and puts it ahead of `m/`. The order matters, because a
MEX file only takes precedence over an M-file of the same name inside one
directory, and `m/PsychNanoVG.m` holds the help text.

    addpath(fullfile(root, 'm'));
    PsychNanoVGSetup();

It raises `psychnanovg:NotBuilt` and names the file it looked for when the MEX
for this platform is missing.

## Test

    matlab -batch "addpath('tests'); run_tests"
    octave-cli --eval "addpath('tests'); run_tests"

`run_tests` calls `PsychNanoVGSetup` itself, so no other path setup is needed.

The tests in `tests/` need no GPU. They use the null renderer: a NanoVG
context whose backend callbacks do nothing. Path building, the state stack,
text layout, the handle tables, and all of the marshaling run for real. Only
the OpenGL calls are absent.

The tests in `tests/gl/` need Psychtoolbox and a GPU. `run_tests` reports
them as skipped when `Screen` does not answer.

Every script that opens a window goes through `tests/gl/ptb_test_window.m`.
That helper sets `SkipSyncTests` and `VisualDebugLevel`, so an unattended run
does not stop for the display sync report or the welcome splash.

`tests/smoke_gl.c` is a native program that opens its own off-screen window
and OpenGL context, with WGL on Windows and GLX elsewhere. It exercises the
real GL path without Psychtoolbox, and it is the only GL coverage under Octave
and on CI:

    matlab -batch "build smoke"
    octave-cli --eval "build smoke"

On Linux `build smoke` runs it through `xvfb-run`, so it needs no display.

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
| `release` | On a `v*` tag, zips every package and publishes a GitHub Release with `gh release create`. |

No runner has a GPU, so every job runs the `renderer='null'` suite. The tests
in `tests/gl/` need Psychtoolbox and a display, so `run_tests` reports them as
skipped on CI.

Each artifact holds only its own `dist/<arch>/`, plus `m/`, `README.md`, and
`SPEC.md`, so one download is a complete install for that engine and platform.
Find them under Artifacts on the run summary page, or on the Releases page for
a tag.

`third_party/nanovg` becomes a git submodule of this repository. Until then
each build job clones the commit that `third_party/PINS.md` records, but only
when `third_party/nanovg/src/nanovg.h` is missing, so the workflow works
before and after the change.

## Use

Every subcommand must run between `Screen('BeginOpenGL')` and
`Screen('EndOpenGL')`.

```matlab
InitializeMatlabOpenGL(1);
[win, rect] = PsychImaging('OpenWindow', screenid, 0);

Screen('BeginOpenGL', win);
PsychNanoVG('Init');
font = PsychNanoVG('CreateFont', 'sans', ...
                   PsychNanoVG('FindSystemFont', 'Arial'));
Screen('EndOpenGL', win);

% every frame
Screen('FillRect', win, 128);
Screen('BeginOpenGL', win);
PsychNanoVG('BeginFrame', RectWidth(rect), RectHeight(rect));
PsychNanoVG('BeginPath');
PsychNanoVG('Circle', cx, cy, 100);
PsychNanoVG('FillColor', [1 1 1 1]);
PsychNanoVG('Fill');
PsychNanoVG('FontFaceId', font);
PsychNanoVG('FontSize', 24);
PsychNanoVG('Text', cx, cy + 140, 'fixate');
PsychNanoVG('EndFrame');
Screen('EndOpenGL', win);
Screen('Flip', win);

% at the end
Screen('BeginOpenGL', win);
PsychNanoVG('Shutdown');
Screen('EndOpenGL', win);
sca;
```

`PsychNanoVGDemo` shows more: a gradient ring, a Bezier trajectory, text
placed with `TextBounds`, and a cached render target.

### Find a subcommand

    PsychNanoVG                 % prints every subcommand, by group
    PsychNanoVG('Circle?')      % prints the help for one subcommand

The subcommand names are the NanoVG names without the `nvg` prefix.
`nvgRoundedRectVarying` becomes `RoundedRectVarying`.

### Rules

| Rule | Statement |
|---|---|
| R1 | Call every subcommand between `Screen('BeginOpenGL')` and `Screen('EndOpenGL')`. |
| R2 | Call `PsychNanoVG` only from the main thread. |
| R3 | `EndFrame` drains OpenGL errors and raises `psychnanovg:GLError`. |
| R4 | `BeginFrame` sets the viewport. `EndFrame` puts the viewport, the blend state, and the active texture unit back. |
| R5 | `BeginFrame` takes the size of the current drawing target. |
| R6 | The MEX never calls `Screen`. |
| R7 | Run `Shutdown` inside `BeginOpenGL`. |
| R8 | One `BeginFrame` per `EndFrame`. Nested frames are an error. |

## Draw many vertices

One MEX call costs about one microsecond under MATLAB and about ten under
Octave. A path with 1,000 points therefore
costs about one millisecond if you call `LineTo` 1,000 times. The batched
subcommands do the loop in C and cost one call:

| Subcommand | Argument |
|---|---|
| `Polyline` | Nx2, one `MoveTo` and N-1 `LineTo` |
| `Polygon` | Nx2, closed |
| `Path` | a cell array of `{'M', x, y}` and so on, or an Nx7 matrix |
| `Circles` | Nx3, for dot fields |
| `Rects` | Nx4 |

`perf/PsychNanoVGPerf` measures the difference. On the development machine
(Intel Iris Xe, 1,000 points, null renderer, the faster of two passes):

| Measurement | MATLAB R2023a | Octave 10.1 |
|---|---|---|
| Empty loop, the same indexing, no MEX call | 0.01 us | 4.81 us |
| `LineTo` by name | 0.96 us per call | 14.29 us per call, 9.49 us net |
| `LineTo` by opcode | 0.75 us per call | 14.72 us per call, 9.92 us net |
| `Polyline` | 14.6 ns per point | 24.5 ns per point |
| `Polyline` against per-vertex `LineTo` | 66 times faster | 584 times faster |

The per-call numbers move by a factor of two or three between runs on a laptop,
so read them as an order of magnitude. The ratio is stable. Octave's own loop
overhead is most of its per-call number, which is why batching gains so much
more there. Cache the `PsychNanoVGOp` struct outside the trial loop.

With a real Psychtoolbox window and the GL renderer under MATLAB, `EndFrame`
costs 49 us for the demo ring and 130 us for a 10,000 point stroked polyline.
The GPU takes 231 us for the same polyline, read from a timer query pair. The
native smoke test reports an `EndFrame` median of 45 us for a frame that holds
a filled circle, a stroked rectangle, a 10,000 point polyline, and a line of
text.

## Measure

`Stats` is always compiled in. It counts every subcommand and every frame.

    s = PsychNanoVG('Stats');
    s.endFrameNs        % the last frame, in nanoseconds
    s.endFrameMaxNs
    s.gpuNs             % from a GL timer query pair, read two frames later
    s.drawCalls         % NanoVG counters for the last frame
    s.commands          % per subcommand: name, calls, totalNs, maxNs
    PsychNanoVG('Stats', 'reset');

## Colors, coordinates, and text

- Colors are 1x4 double in 0 to 1, or 1x3 with alpha 1.
- The origin is top left and y points down, the same as Psychtoolbox.
- NanoVG blends in the color space of the framebuffer. On a standard 8-bit
  window the antialiased edges blend in sRGB space, the same as `Screen`.
- The antialiasing fringe is one pixel wide. For a controlled edge profile,
  draw a gradient ring instead.
- `FindSystemFont` searches the operating system font directories.
  `PsychNanoVGFonts.m` holds the search, so you can add a directory without
  rebuilding the MEX.
- A new render target holds whatever was in that memory. Clear it yourself
  after `RenderTargetBind`, with `glClear` through mogl.

## Files

| Path | Contents |
|---|---|
| `src/PsychNanoVG.c` | The MEX entry point, dispatch, marshaling, lifecycle. |
| `src/core/` | The NanoVG-facing layer, with no MATLAB types. The smoke test links it. |
| `src/pnvg_gl.c` | glad, the NanoVG GL backend, the proc loader, GL state save and restore. |
| `src/pnvg_batch.c` | `Polyline`, `Polygon`, `Path`, `Circles`, `Rects`. |
| `src/pnvg_targets.c` | Render targets and `CreateImageFromTexture`. |
| `src/gen_dispatch.c`, `src/gen_enums.c` | Generated. Committed. |
| `gen/generate.py` | The generator. Run it with `build gen`. |
| `m/` | Help text, opcodes, the path setup, the font search, the demo. |
| `tests/` | The suite that needs no GPU, plus `tests/gl/` and `smoke_gl.c`. |
| `perf/` | The timings of SPEC 9.4. |

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

## Errors

| Identifier | Meaning |
|---|---|
| `psychnanovg:Usage` | Wrong number of arguments. |
| `psychnanovg:UnknownCommand` | No such subcommand or opcode. |
| `psychnanovg:NotInit` | `Init` is required first. |
| `psychnanovg:AlreadyInit` | `Init` was called twice. |
| `psychnanovg:NoGLContext` | An OpenGL subcommand ran with no current context. |
| `psychnanovg:GLInit` | The loader or `nvgCreateGL3` failed. |
| `psychnanovg:GLError` | OpenGL reported an error in `EndFrame`. |
| `psychnanovg:FrameState` | A frame error. See R8. |
| `psychnanovg:Handle` | Unknown font, image, paint, or render target handle. |
| `psychnanovg:Font` | The font file was not found or not read. |
| `psychnanovg:Type` | Wrong argument class. |
| `psychnanovg:Range` | A numeric argument is out of range. |

## License

NanoVG is zlib licensed. glad output is in the public domain, with an MIT
option. This binding follows the license of the workspace.
