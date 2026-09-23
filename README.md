# PsychNanoVG

`PsychNanoVG` draws antialiased 2D vector graphics in a Psychtoolbox (PTB)
window, from MATLAB and from GNU Octave. It is a MEX binding of
[NanoVG](https://github.com/memononen/nanovg).

`Screen` draws rectangles, ovals, lines, polygons, and text. `PsychNanoVG`
adds Bezier paths, stroke joins and caps, gradients, image patterns, and
TrueType text with exact glyph metrics. The GPU does the antialiasing. The
MEX draws into the same framebuffer as `Screen`, so you can mix the two in
one frame. Use it when `Screen` cannot draw the shape, the edge, or the text
layout that your stimulus needs.

![One frame of PsychNanoVGDemo in a 1280x720 Psychtoolbox window: a pair of eyes with drop shadows and highlights, an arc gauge with a gradient band, a ring with a gradient edge and 100 cached copies of it, a Bezier trajectory, and a wave with one color per vertex.](docs/images/psychnanovg-demo.png)

The picture is one frame of `PsychNanoVGDemo` read back from a Psychtoolbox
window.

## Install

You do not need a compiler. Each release zip holds a compiled MEX file for
one engine and one platform.

> The first release is pending. Until it is published, the Releases page is
> empty. Build from source with [DEV.md](https://github.com/aforren1/PsychNanoVG/blob/main/DEV.md),
> or download a package from the Artifacts list of a green CI run.

1. Open the [Releases page](https://github.com/aforren1/PsychNanoVG/releases).
2. Download the zip for your engine and platform:

   | Zip | Use it for |
   |---|---|
   | `psychnanovg-matlab-windows.zip` | MATLAB R2022b or later on Windows |
   | `psychnanovg-matlab-linux.zip` | MATLAB R2021b or later on Linux |
   | `psychnanovg-matlab-macos.zip` | MATLAB R2023b or later on an Apple silicon Mac |
   | `psychnanovg-octave-windows.zip` | Octave 10 on Windows |
   | `psychnanovg-octave-linux-6.4.zip` | Octave 6.x to 9.x on Linux |
   | `psychnanovg-octave-linux-10.zip` | Octave 10 or later on Linux |
   | `psychnanovg-octave-macos.zip` | Homebrew Octave on an Apple silicon Mac |

3. Make an empty folder, for example `C:\toolbox\PsychNanoVG`, and unzip into
   it. The zip has no top folder, so the folder then holds
   `PsychNanoVGSetup.m`, `dist/`, `m/`, `docs/`, `README.md`, and `SPEC.md`.
4. In MATLAB or Octave, add the folder and run the setup:

   ```matlab
   addpath('C:\toolbox\PsychNanoVG');
   PsychNanoVGSetup();
   ```

   `PsychNanoVGSetup` adds `dist/<arch>` and `m/` to the path, in that order.
5. Make sure that it works:

   ```matlab
   v = PsychNanoVG('Version')
   ```

   The output is a struct. `v.psychnanovg` is the version of the binding and
   `v.nanovg` is the NanoVG commit. If the MEX for your engine and platform is
   not in `dist/`, `PsychNanoVGSetup` raises `psychnanovg:NotBuilt` and names
   the file that it looked for. Then download the correct zip.

These two commands do the same as step 4, without `addpath`:

```matlab
run('C:\toolbox\PsychNanoVG\PsychNanoVGSetup.m');
```

```matlab
cd('C:\toolbox\PsychNanoVG');
PsychNanoVGSetup();
```

### Keep it on the path

Step 4 changes the path for this session only. To keep the change, do one of
these:

- **Saved path, MATLAB or Octave.** In step 4, run
  `PsychNanoVGSetup('add', 'save')` instead of `PsychNanoVGSetup()`. It adds
  the folders and then runs `savepath`. MATLAB can need write access to its
  installation folder for this; if `savepath` fails, you get the warning
  `psychnanovg:SavePath`, and you can use `startup.m` instead. Octave saves
  the path to `~/.octaverc`.
- **MATLAB, `startup.m`.** Add the two lines of step 4 to `startup.m`, in
  the folder that `userpath` shows. Psychtoolbox has its own `startup.m`,
  which calls `PsychStartup`. Your file hides it, so make this the first
  line of your file:

  ```matlab
  if exist('PsychStartup'), PsychStartup; end
  ```

- **Octave, `.octaverc`.** Add the two lines of step 4 to `~/.octaverc`. On
  Windows, `~` is your user folder, for example `C:\Users\you\.octaverc`.
  If the file already adds Psychtoolbox, put the lines after it.

### Remove it

```matlab
PsychNanoVGSetup('remove');           % this session only
PsychNanoVGSetup('remove', 'save');   % and save the path
```

This takes `dist/<arch>`, `m/`, and the package folder off the path. First
it closes every PsychNanoVG context and unloads the MEX file. Close your
Psychtoolbox windows before you remove it. If the path does not hold the
package, the command does nothing. If you added the setup lines to
`startup.m` or `~/.octaverc`, delete them too.

If the MEX file stays loaded, `PsychNanoVGSetup` raises
`psychnanovg:Locked`, prints what to do, and does not change the path.

## First example

This script opens a Psychtoolbox window, draws a rounded rectangle and a
line of text, shows it for two seconds, and closes the window:

```matlab
PsychDefaultSetup(2);
InitializeMatlabOpenGL(1);                 % before OpenWindow
[win, rect] = PsychImaging('OpenWindow', max(Screen('Screens')), 0.5);
[cx, cy] = RectCenter(rect);
vg = PsychNanoVGOpen(win);                 % one context for this window
PsychNanoVGFrame('Begin', vg);
PsychNanoVG('BeginPath');
PsychNanoVG('RoundedRect', cx - 200, cy - 100, 400, 200, 24);
PsychNanoVG('FillColor', [0.2 0.4 0.8 1]);
PsychNanoVG('Fill');
PsychNanoVG('FontFaceId', vg.fonts.sans);
PsychNanoVG('FontSize', 40);
PsychNanoVG('FillColor', [1 1 1 1]);
PsychNanoVG('Text', cx - 125, cy + 14, 'Hello, NanoVG');
PsychNanoVGFrame('End', vg);
Screen('Flip', win);
WaitSecs(2);
PsychNanoVGClose(vg);
sca;
```

`PsychNanoVGOpen` makes a NanoVG context for the window.
`PsychNanoVGFrame('Begin')` and `PsychNanoVGFrame('End')` enclose the
drawing. `PsychNanoVGClose` deletes the context before the window closes.
If Psychtoolbox stops with a sync test failure on a laptop, add
`Screen('Preference', 'SkipSyncTests', 1)` before `OpenWindow`, for the test
only. The sections below explain each step.

`PsychNanoVGDemo` shows much more. It holds a full screen window for six
seconds.

## Use

Four M-files stand between a script and the MEX, so a script never writes a
`Screen('BeginOpenGL')` and `Screen('EndOpenGL')` pair.

| Helper | What it does |
|---|---|
| `vg = PsychNanoVGOpen(win [, opts])` | Creates a context for an open window and loads a default sans font. Returns the handle struct, with the context handle in `vg.ctx`. |
| `PsychNanoVGFrame('Begin', vg [, w, h])` | Enters the OpenGL region, selects the context of `vg`, and starts the frame. |
| `PsychNanoVGFrame('End', vg)` | Finishes the frame of `vg` and leaves the region. |
| `PsychNanoVGGL(vg, subcommand, ...)` | Selects the context of `vg` and runs one OpenGL subcommand, such as `CreateFont`, inside its own region. |
| `PsychNanoVGClose(vg)` | Deletes the context of `vg`. Safe twice, and safe after the window is closed. |

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
a wave with one color per vertex, and the eyes of the NanoVG example, which
follow the mouse.

### Draw into two windows

Psychtoolbox gives every onscreen window its own OpenGL context, and the
two contexts share no objects. A font, an image, or a render target made
for one window does not exist in the other. Open one context per window;
the helpers select the right one for you:

```matlab
InitializeMatlabOpenGL(1);
winA = PsychImaging('OpenWindow', screenid, 0, [0 0 640 480]);
winB = PsychImaging('OpenWindow', screenid, 0, [700 0 1340 480]);
vgA = PsychNanoVGOpen(winA);
vgB = PsychNanoVGOpen(winB);

% every frame
PsychNanoVGFrame('Begin', vgA);
% draw into window A
PsychNanoVGFrame('End', vgA);
PsychNanoVGFrame('Begin', vgB);
% draw into window B
PsychNanoVGFrame('End', vgB);
Screen('Flip', winA, [], [], [], 1);           % flips both windows

PsychNanoVGClose(vgB);
PsychNanoVGClose(vgA);
```

Use the fonts of each struct in its own window: `vgA.fonts.sans` in the
frame of `vgA`. `PsychNanoVGTwoWindowDemo` is the full example. An offscreen
window uses the OpenGL context of its parent window, so it uses the context
of that window too.

In the low-level form, one context is current at a time.
`ctx = PsychNanoVG('Init')` makes a new one current, and
`PsychNanoVG('SetContext', ctx)` selects another. Select it after
`Screen('BeginOpenGL')` for its window:

| Subcommand | What it does |
|---|---|
| `ctx = PsychNanoVG('Init' [, opts])` | Creates a context in the OpenGL context that is current, and makes it current. |
| `prev = PsychNanoVG('SetContext', ctx)` | Makes `ctx` current. Returns the handle that was current, 0 for none. |
| `ctx = PsychNanoVG('SetContext')` | Returns the current handle and changes nothing. |
| `PsychNanoVG('Shutdown' [, ctx])` | Deletes `ctx`, or the current context. Run it in the region of the window of `ctx`. |
| `PsychNanoVG('Shutdown', 'all')` | Deletes every context, for example after a script failed before its cleanup. |
| `v = PsychNanoVG('Version')` | `v.context` is the current handle and `v.contexts` lists every open one. |

`Stats` reports the current context. A subcommand that issues OpenGL calls
while the OpenGL context of another window is current raises
`psychnanovg:Context` and makes no OpenGL call. A `Shutdown` in the wrong
region frees the memory, warns, and leaves the OpenGL objects to the driver.
The MEX file stays locked until the last context goes.

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
| R9 | One context per window. The helpers select it. In the low-level form, call `SetContext` after `Screen('BeginOpenGL')`. |

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

`perf/PsychNanoVGPerf`, in the source repository, measures the difference.
On the development machine (Intel Iris Xe, 1,000 points, null renderer, the
faster of two passes):

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

## Errors

| Identifier | Meaning |
|---|---|
| `psychnanovg:Usage` | Wrong number of arguments. |
| `psychnanovg:UnknownCommand` | No such subcommand or opcode. |
| `psychnanovg:NotInit` | `Init` is required first. |
| `psychnanovg:AlreadyInit` | Not raised since version 0.2.0. A second `Init` makes a second context. |
| `psychnanovg:NoGLContext` | An OpenGL subcommand ran with no current context. |
| `psychnanovg:GLInit` | The loader or `nvgCreateGL3` failed. |
| `psychnanovg:GLError` | OpenGL reported an error in `EndFrame`. |
| `psychnanovg:FrameState` | A frame error. See R8. |
| `psychnanovg:Handle` | Unknown font, image, paint, render target, or context handle. |
| `psychnanovg:Font` | The font file was not found or not read. |
| `psychnanovg:Type` | Wrong argument class. |
| `psychnanovg:Range` | A numeric argument is out of range, or all 16 contexts are in use. |
| `psychnanovg:Context` | The OpenGL context that is current belongs to another window than the current context. |

## Requirements

- Psychtoolbox 3.0.19 or later.
- One of these engines. The Install table names the zip for each one.
  - Windows: MATLAB R2022b or later, or Octave 10.
  - Linux: MATLAB R2021b or later, Octave 6.x to 9.x, or Octave 10 or later.
  - macOS on Apple silicon: MATLAB R2023b or later, or Homebrew Octave.
    Intel Macs are not supported.
- A GPU with OpenGL 3.3 on Windows and Linux, or OpenGL 2.1 on macOS.
- No compiler. A release zip holds the compiled MEX file. You need a
  compiler only to build from source; see
  [DEV.md](https://github.com/aforren1/PsychNanoVG/blob/main/DEV.md).

## Where to go next

- [SPEC.md](SPEC.md) is the design reference. It is in every release zip.
  Section 5 lists every subcommand and helper, and section 14 lists where the
  implementation differs from the design.
- [DEV.md](https://github.com/aforren1/PsychNanoVG/blob/main/DEV.md) tells
  contributors how to build from source, run the tests, measure frame time
  with `Stats`, and profile with Tracy.
- [RELEASING.md](https://github.com/aforren1/PsychNanoVG/blob/main/RELEASING.md)
  is the checklist for publishing a release.

## License

PsychNanoVG is MIT licensed; see `LICENSE`.

NanoVG, compiled into the MEX, is zlib licensed. The generated glad loader is
public domain (WTFPL or CC0), and the Khronos headers it embeds are Apache
2.0. License texts are in `third_party/` of the source repository. Tracy,
used only in profiling builds that are not released, is BSD 3-Clause.
