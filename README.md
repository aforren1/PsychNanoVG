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
| Generated API, 96 subcommands, 120 in total | Works. Tested with the null renderer under MATLAB R2023a and Octave 10.1. |
| Batched paths, paints, fonts, images, `Stats` | Works. |
| Arcs and shapes in the `Path` matrix, `StrokeSegments`, `PsychNanoVGPolylineGradient` | Works. Phase 2. Covered by the null renderer suite, by `tests/gl/test_gl_paths`, and by the native smoke test. |
| Psychtoolbox integration, `tests/gl/` | Works under MATLAB. With the suite that needs no GPU, 336 assertions pass, including shapes, text metrics, the render target round trip, an arc gauge, and a gradient polyline. |
| Render targets, `CreateImageFromTexture` | Works. Covered by `tests/gl/test_gl_target` and by the native smoke test. |
| `tests/gl/` under Octave | Skipped. The Psychtoolbox `Screen` MEX for Octave does not load on the development machine. |
| `m/PsychNanoVGDemo` | Runs under MATLAB. It holds a full screen window for six seconds by default. |
| Linux | Works. Built and tested with Octave 6.4 on Ubuntu 22.04, and `smoke_gl` runs under Xvfb with Mesa llvmpipe. |
| macOS on Apple silicon | Built and tested on the `macos-latest` runner, with the GL2 backend. The GL 2.1 context has no GPU timer unless it offers `GL_ARB_timer_query`, so `gpuNs` can be NaN there. |
| macOS on Intel | Not covered. |
| Tracy | Works when you turn it on. CPU zones for every subcommand and the hot paths, and a GPU zone per frame. Built and captured on Windows with MSVC and with Octave's MinGW. Not tried on Linux or macOS. |

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
context whose backend callbacks do nothing. `test_helpers` also puts a
recording `Screen` stub from `tests/stub/` on the path for its own duration,
so the convenience layer is checked without Psychtoolbox: the region opens
once, closes once, and closes again on an error. Path building, the state stack,
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
| `octave-macos` | Builds and tests with Homebrew Octave on Apple silicon. Uploads the package. |
| `smoke-gl-macos` | Builds `smoke_gl` and runs it against a CGL context with no drawable, rendering into a render target. |
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

Four M-files stand between a script and the MEX, so a script never writes a
`Screen('BeginOpenGL')` and `Screen('EndOpenGL')` pair.

| Helper | What it does |
|---|---|
| `vg = PsychNanoVGOpen(win [, opts])` | Creates the context for an open window and loads a default sans font. Returns the handle struct. |
| `PsychNanoVGFrame('Begin', vg [, w, h])` | Enters the OpenGL region and starts the frame. |
| `PsychNanoVGFrame('End', vg)` | Finishes the frame and leaves the region. |
| `PsychNanoVGGL(vg, subcommand, ...)` | Runs one OpenGL subcommand, such as `CreateFont`, inside its own region. |
| `PsychNanoVGClose(vg)` | Deletes the context. Safe twice, and safe after the window is closed. |

Between `Begin` and `End`, draw with plain `PsychNanoVG` calls.

```matlab
InitializeMatlabOpenGL(1);                     % before OpenWindow
[win, rect] = PsychImaging('OpenWindow', screenid, 0);

vg = PsychNanoVGOpen(win);
cleanup = onCleanup(@() PsychNanoVGClose(vg));

% every frame
Screen('FillRect', win, 128);
PsychNanoVGFrame('Begin', vg);
PsychNanoVG('BeginPath');
PsychNanoVG('Circle', cx, cy, 100);
PsychNanoVG('FillColor', [1 1 1 1]);
PsychNanoVG('Fill');
PsychNanoVG('FontFaceId', vg.fonts.sans);
PsychNanoVG('FontSize', 24);
PsychNanoVG('Text', cx, cy + 140, 'fixate');
PsychNanoVGFrame('End', vg);
Screen('DrawText', win, 'Screen still works', 10, 10);
Screen('Flip', win);
```

`vg.fonts.sans` is the default font. It is absent when no system font was
found, so test with `isfield(vg.fonts, 'sans')` on an unknown machine.

Each helper calls `Screen('EndOpenGL')` on the error path as well, so a MEX
error inside a wrapped region still leaves Psychtoolbox in 2D drawing mode.

`PsychNanoVGOpen` refuses a window that was opened without
`InitializeMatlabOpenGL`, with `psychnanovg:No3DGraphics`, rather than let
`Screen('BeginOpenGL')` fail later for a reason that is harder to read.

`PsychNanoVGDemo` shows more: a gradient ring, a Bezier trajectory, text
placed with `TextBounds`, a cached render target, a gauge drawn from arcs,
and a wave with one color per vertex.

### The low-level form

The helpers are thin. Every subcommand must run between
`Screen('BeginOpenGL')` and `Screen('EndOpenGL')`, and you can write that
yourself:

```matlab
Screen('BeginOpenGL', win);
PsychNanoVG('Init');
font = PsychNanoVG('CreateFont', 'sans', ...
                   PsychNanoVG('FindSystemFont', 'Arial'));
Screen('EndOpenGL', win);

Screen('BeginOpenGL', win);
PsychNanoVG('BeginFrame', RectWidth(rect), RectHeight(rect));
PsychNanoVG('BeginPath');
PsychNanoVG('Circle', cx, cy, 100);
PsychNanoVG('FillColor', [1 1 1 1]);
PsychNanoVG('Fill');
PsychNanoVG('EndFrame');
Screen('EndOpenGL', win);

Screen('BeginOpenGL', win);
PsychNanoVG('Shutdown');
Screen('EndOpenGL', win);
```

Use this form when several subcommands have to share one region. A render
target is the case that needs it, because `Screen('EndOpenGL')` resets the
framebuffer binding:

```matlab
[rt, glTex] = PsychNanoVGGL(vg, 'RenderTargetCreate', 256, 256);
Screen('BeginOpenGL', vg.win);
PsychNanoVG('RenderTargetBind', rt);
glClearColor(0, 0, 0, 0);
glClear(bitor(GL.COLOR_BUFFER_BIT, GL.STENCIL_BUFFER_BIT));
PsychNanoVG('BeginFrame', 256, 256);
% draw
PsychNanoVG('EndFrame');
PsychNanoVG('RenderTargetUnbind');
Screen('EndOpenGL', vg.win);
```

`PsychNanoVGGL` passes through while a region is open, so the calls inside
can still be written through it.

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
| `Path` | an Nx7 matrix of commands, or a cell array such as `{{'M', x, y}, {'Arc', cx, cy, r, a0, a1, 'CW'}}` |
| `Circles` | Nx3, for dot fields |
| `Rects` | Nx4 |
| `StrokeSegments` | Nx4 segments and Nx8 color pairs, one gradient stroke per segment |

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

### Draw arcs in one call

Each row of the `Path` matrix is one command: a code in column 1, then the
arguments of the NanoVG function, zero padded to 7 columns. Angles are
radians. y points down, so an angle that increases turns clockwise.

| Code | Command | Columns 2 to 7 |
|---|---|---|
| 1 | MoveTo | x, y |
| 2 | LineTo | x, y |
| 3 | QuadTo | cx, cy, x, y |
| 4 | BezierTo | c1x, c1y, c2x, c2y, x, y |
| 5 | ClosePath | |
| 6 | Arc | cx, cy, r, a0, a1, dir |
| 7 | ArcTo | x1, y1, x2, y2, r |
| 8 | Ellipse | cx, cy, rx, ry |
| 9 | Circle | cx, cy, r |
| 10 | Rect | x, y, w, h |
| 11 | RoundedRect | x, y, w, h, r |
| 12 | PathWinding | dir |

`dir` is 1 for counterclockwise or solid, and 2 for clockwise or hole. This
gauge band is one call:

```matlab
band = [6 cx cy 100 0.75*pi 2.25*pi 2;   % outer arc, clockwise
        6 cx cy  80 2.25*pi 0.75*pi 1;   % inner arc back
        5 0 0 0 0 0 0];                  % close
PsychNanoVG('BeginPath');
PsychNanoVG('Path', band);
PsychNanoVG('Fill');
```

`Path` checks every row before it draws any. A bad code or a bad `dir` raises
`psychnanovg:Range`, names the row, and leaves the path as it was.

### Draw a line with one color per vertex

`PsychNanoVGPolylineGradient(xy, rgba)` takes an Nx2 polyline and Nx4 colors.
It draws each segment with a linear gradient from the color of its first
vertex to the color of its second, in one MEX call to `StrokeSegments`:

```matlab
PsychNanoVG('StrokeWidth', 6);
PsychNanoVG('LineCap', 'ROUND');      % closes the gaps at the corners
PsychNanoVGPolylineGradient(xy, rgba);
```

Each gradient segment is its own stroke, so there are no joins between
segments; round caps hide that. Segments in a row that all have one color
become one stroke with real joins. The function replaces the current path
and keeps the stroke paint.

For 1,000 segments the call costs about 0.3 us per segment with the null
renderer and 0.5 us with the GL renderer, against 14 to 26 us for the same
work written as seven subcommands per segment. The GPU then takes about 6.5
us per segment on Intel Iris Xe, because each stroke is its own draw. SPEC
section 14.7 has the full table.

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
| `src/pnvg_batch.c` | `Polyline`, `Polygon`, `Path`, `Circles`, `Rects`, `StrokeSegments`. |
| `src/pnvg_profiler.h`, `src/core/pnvg_tracy.cpp` | The Stats switch and the Tracy zones. The C++ file is compiled only with Tracy. |
| `src/pnvg_targets.c` | Render targets and `CreateImageFromTexture`. |
| `src/gen_dispatch.c`, `src/gen_enums.c` | Generated. Committed. |
| `gen/generate.py` | The generator. Run it with `build gen`. |
| `m/` | The convenience layer, help text, opcodes, the path setup, the font search, the gradient polyline, the demo. |
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

## Releasing

A release is a `v*` tag; CI builds and publishes the packages. The
step-by-step checklist, including where the version string lives and how to
recover from a failed release job, is in [RELEASING.md](RELEASING.md).
