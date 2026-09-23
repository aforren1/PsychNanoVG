# PsychNanoVG specification

Status: implemented through phase 2. Specification version 0.1, 2026-09-22; section 14 records every deviation. The phase 2 additions to sections 5 and 7 are marked "phase 2".

`PsychNanoVG` is a MEX binding of NanoVG for MATLAB and GNU Octave. NanoVG is a
small antialiased 2D vector graphics library on OpenGL. The binding draws
paths, gradients, images, and TrueType text into a Psychtoolbox (PTB) onscreen
window, inside PTB's userspace OpenGL context.

This document is the design reference for the implementation. Section 3 and
section 4 explain the design. The other sections are reference material.

## 1. Purpose and scope

### 1.1 Purpose

PTB's `Screen` drawing commands cover rectangles, ovals, lines, polygons, and
text. They do not cover Bezier paths, stroke joins and caps, gradients, image
patterns, or TrueType text with exact glyph metrics. NanoVG provides these with
GPU antialiasing. `PsychNanoVG` exposes the NanoVG API to experiment scripts
with the same call pattern as `Screen`.

Typical uses: smooth stimulus outlines, rings and arcs with controlled edge
profiles, gradient fills, vector icons, and text with sub-pixel positioning.

### 1.2 In scope

- One NanoVG context per MATLAB or Octave process, bound to one PTB onscreen
  window.
- The complete NanoVG public API except the pieces listed in section 7.3, about
  95 functions, generated from `nanovg.h`.
- Batched path subcommands that take whole matrices, so a 10,000-point path is
  one MEX call.
- Drawing directly into the PTB window, or into an offscreen render target that
  becomes a PTB texture.
- MATLAB R2023a and Octave 10.1 on Windows, verified. Linux expected to work.
  macOS on Apple silicon (`maca64`) built and tested on a CI runner, with
  the GL2 backend. Intel Macs are not covered.

### 1.3 Out of scope

- Input handling. NanoVG draws only.
- Widgets. Use the GUI bindings in this workspace for that.
- Any code shared with other bindings. This project is self-contained.

## 2. Dependencies and pinned versions

| Dependency | Version | Location | Reason |
|---|---|---|---|
| NanoVG | `memononen/nanovg` master, commit of 2026-02-19 or later | `third_party/nanovg` (git submodule) | Upstream library: `nanovg.c`, `nanovg.h`, `nanovg_gl.h`, `nanovg_gl_utils.h`, `fontstash.h`, `stb_truetype.h`, `stb_image.h`. The fork inside LVGL is not used because it includes LVGL headers. |
| glad 2 | generated header, GL 3.3 compatibility, functions used by `nanovg_gl.h` | `third_party/glad` (committed, generated once) | `nanovg_gl.h` expects GL function declarations from the including code. glad provides them and a loader that takes a proc-address function. |
| Tracy | 0.11.x | `third_party/tracy` (git submodule, optional) | Profiler client, compiled only with `PSYCHNANOVG_TRACY=ON`. |
| Psychtoolbox | 3.0.19 or later | user install | `Screen('BeginOpenGL')`, `Screen('SetOpenGLTexture')`, `Screen('GetOpenGLTexture')`. |
| MATLAB | R2023a verified | user install | C MEX with MSVC 2022. |
| Octave | 10.1 verified | user install | `mkoctfile --mex` with the bundled MinGW gcc. |
| CMake | 3.16 or later | build machine | Builds the static NanoVG library. |
| Python and uv | Python 3.10 or later, pycparser | developer machine only | Runs `gen/generate.py`. Generated files are committed. |

## 3. Architecture overview

```
MATLAB / Octave script
  |  PsychNanoVG('BeginFrame', w, h)  PsychNanoVG('Polyline', xy)  PsychNanoVG('Stroke')  PsychNanoVG('EndFrame')
  v
+--------------------------------------------------------------+
| PsychNanoVG MEX (C99)                                        |
|  dispatch:  sorted name table + opcode fast path             |
|  marshal:   mxArray <-> float, NVGcolor, NVGpaint handles    |
|  batch:     Polyline, Polygon, Path (matrix forms)           |
|  state:     one NVGcontext, paint table, font and image ids  |
|  targets:   window (default) or FBO render target           |
+--------------------------------------------------------------+
  |  OpenGL calls, only between Screen('BeginOpenGL') and Screen('EndOpenGL')
  v
PTB userspace GL context  -->  PTB imaging pipeline FBO (or own FBO -> PTB texture)  -->  Screen('Flip')
```

Design choices and why:

- Direct drawing into the PTB framebuffer is the default. NanoVG draws where
  the current FBO points, and `Screen('BeginOpenGL')` binds PTB's drawing
  target. A script mixes `Screen` commands and NanoVG commands in one frame.
- Render targets are optional. A script that draws a complex static shape once
  renders it into a NanoVG framebuffer, wraps the texture with
  `Screen('SetOpenGLTexture')`, and then draws it with `Screen('DrawTexture')`
  every frame at no NanoVG cost.
- The API is generated from `nanovg.h` with pycparser. NanoVG is small and
  stable, but generation still gives consistent help text, argument checks,
  and tests from one source.
- Paint objects (`NVGpaint`) are returned by value in C. The MEX keeps them in
  a small table and returns an index. Scripts pass the index to `FillPaint`
  and `StrokePaint`.
- Batched path subcommands exist because immediate per-vertex calls from MATLAB
  cost 1 to 3 us each. `Polyline` with an Nx2 matrix issues N `nvgLineTo` calls
  inside one MEX call.

## 4. PTB integration contract

### 4.1 Verified PTB behavior

These facts come from the PTB source tree, `PsychSourceGL/Source/`.

- `Screen('BeginOpenGL', win)` (`Common/Screen/SCREENglMatrixFunctionWrappers.c`)
  switches to a separate userspace GL context that shares textures, buffers,
  FBOs, and shaders with PTB's context but not render state. PTB binds its
  current FBO and, on the first call, sets viewport, scissor, and projection to
  the client rectangle. `BeginOpenGL` requires `InitializeMatlabOpenGL`.
- `Screen('EndOpenGL', win)` calls `glGetError` and aborts the script if an
  error is pending. It also resets the FBO binding.
- PTB requests an 8-bit stencil buffer for the window
  (`Windows/Screen/PsychWindowGlue.c`, `pfd.cStencilBits = 8`) and attaches a
  stencil renderbuffer to imaging pipeline FBOs unless the
  `kPsychDontAttachStencilToFBO` ConserveVRAM flag is set
  (`Common/Screen/PsychImagingPipelineSupport.c`). NanoVG needs a stencil
  buffer for concave fills and for `NVG_STENCIL_STROKES`.
- PTB contexts are legacy compatibility contexts. NanoVG's GL3 backend
  (`#version 150 core` shaders) works on Windows and Linux. macOS PTB contexts
  are GL 2.1, so the GL2 backend is used there. The backend is fixed when the
  static library is compiled, because `nanovg_gl.h` is one implementation
  unit: CMake defines `NANOVG_GL2_IMPLEMENTATION` on Apple and
  `NANOVG_GL3_IMPLEMENTATION` everywhere else, and `build.m` passes the
  matching `PNVG_GL2` to `mex`.
- `Screen('SetOpenGLTexture', win, tex, glTexId, target, w, h)` wraps an
  external GL texture. `Screen('GetOpenGLTexture', win, tex)` returns the GL
  texture id and target of a PTB texture (`Common/Screen/SCREENGetOpenGLTexture.c`).
- PTB and NanoVG both use a top-left origin with y pointing down, in pixels.

### 4.2 Verified NanoVG behavior

From upstream `src/nanovg.h` and `src/nanovg_gl.h`:

- `nvgBeginFrame(ctx, windowWidth, windowHeight, devicePixelRatio)` and
  `nvgEndFrame(ctx)` bracket all drawing. NanoVG buffers calls and issues GL
  work in `nvgEndFrame`.
- `nvgCreateGL3(flags)` and `nvgDeleteGL3(ctx)` with flags `NVG_ANTIALIAS`,
  `NVG_STENCIL_STROKES`, `NVG_DEBUG`. `nvgCreateGL2` for the GL2 backend.
- `nanovg_gl.h` does not call `glViewport`. It reads the view size from
  `nvgBeginFrame` for its shader. The embedding code sets the viewport.
- After `nvgEndFrame` the backend leaves `glUseProgram(0)`, VAO 0, and stencil
  test disabled, but it does not restore blend state or texture bindings.
- Text comes from fontstash and stb_truetype. `nvgCreateFont(ctx, name,
  filename)` loads a TTF file. `nvgText` returns the advance, `nvgTextBounds`
  returns the advance and fills a 4-element bounds array, `nvgTextMetrics`
  returns ascender, descender, and line height.

### 4.3 Required call order

The convenience layer of section 5.4 opens and closes the OpenGL region, so
a script writes no `Screen('BeginOpenGL')` and `Screen('EndOpenGL')` pair of
its own.

```matlab
% Setup, once
InitializeMatlabOpenGL(1);                                   % before OpenWindow
[win, rect] = PsychImaging('OpenWindow', screenid, 0);
vg = PsychNanoVGOpen(win);                                   % Init, plus a default sans font
cleanup = onCleanup(@() PsychNanoVGClose(vg));

% Every frame
Screen('FillRect', win, 128);                                % PTB drawing first
PsychNanoVGFrame('Begin', vg);                               % BeginOpenGL, then BeginFrame
PsychNanoVG('BeginPath');
PsychNanoVG('Circle', cx, cy, 100);
PsychNanoVG('FillColor', [1 1 1 1]);
PsychNanoVG('Fill');
PsychNanoVG('StrokeWidth', 3);
PsychNanoVG('StrokeColor', [0 0 0 1]);
PsychNanoVG('Stroke');
PsychNanoVG('FontFaceId', vg.fonts.sans); PsychNanoVG('FontSize', 24);
PsychNanoVG('Text', cx, cy + 140, 'fixate');
PsychNanoVGFrame('End', vg);                                 % EndFrame, then EndOpenGL
Screen('DrawText', win, 'PTB text still works', 10, 10);     % more PTB drawing
Screen('Flip', win);

% A setup call that touches OpenGL, outside a frame
font = PsychNanoVGGL(vg, 'CreateFont', 'mono', fontPath);

% Teardown, once
PsychNanoVGClose(vg);
sca;
```

The helpers are thin. This is the same frame written out, and it is what the
helpers do:

```matlab
% Setup, once
InitializeMatlabOpenGL(1);
[win, rect] = PsychImaging('OpenWindow', screenid, 0);
Screen('BeginOpenGL', win);
PsychNanoVG('Init');                                         % nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES)
font = PsychNanoVG('CreateFont', 'sans', PsychNanoVG('FindSystemFont', 'Arial'));
Screen('EndOpenGL', win);

% Every frame
Screen('FillRect', win, 128);
Screen('BeginOpenGL', win);
PsychNanoVG('BeginFrame', RectWidth(rect), RectHeight(rect));
PsychNanoVG('BeginPath');
PsychNanoVG('Circle', cx, cy, 100);
PsychNanoVG('FillColor', [1 1 1 1]);
PsychNanoVG('Fill');
PsychNanoVG('EndFrame');                                     % GL work happens here, then error drain
Screen('EndOpenGL', win);
Screen('Flip', win);

% Teardown, once
Screen('BeginOpenGL', win);
PsychNanoVG('Shutdown');
Screen('EndOpenGL', win);
sca;
```

Use the raw form when several subcommands have to share one region. A render
target is the case that needs it: `RenderTargetBind`, the `glClear`, the
frame, and `RenderTargetUnbind` all belong together, because
`Screen('EndOpenGL')` resets the framebuffer binding.

### 4.4 Rules

| Rule | Statement | Reason |
|---|---|---|
| R1 | Call every `PsychNanoVG` subcommand between `Screen('BeginOpenGL')` and `Screen('EndOpenGL')`. | `Init`, `EndFrame`, image and font creation, and render target functions issue GL calls. Path and state calls do not, but one rule is simpler. The MEX raises `psychnanovg:NoGLContext` when a GL subcommand finds no current context. |
| R2 | Call `PsychNanoVG` only from the MATLAB main thread. | GL contexts are thread bound. |
| R3 | `EndFrame` leaves `glGetError` at `GL_NO_ERROR`. | `Screen('EndOpenGL')` aborts on a pending error. `EndFrame` drains errors and raises `psychnanovg:GLError`. |
| R4 | `BeginFrame` sets `glViewport(0, 0, w, h)` and `EndFrame` restores the previous viewport, blend function, blend enable, and active texture unit. | NanoVG does not set the viewport and does not restore blend state. Restoring keeps other userspace GL code in the same context predictable. |
| R5 | `BeginFrame` receives the current drawing target size. | The size can change with stereo modes and offscreen windows. The script passes `Screen('Rect')` of the target. |
| R6 | The MEX never calls `Screen`. | No PTB dependency. |
| R7 | `Shutdown` runs inside `BeginOpenGL` when possible. | GL objects need a current context for deletion. Without one the MEX skips deletion with a warning. |
| R8 | A frame is one `BeginFrame` and one `EndFrame`. Nested frames are an error, `psychnanovg:FrameState`. | NanoVG requires it. |

## 5. MATLAB API reference

All functions accept a subcommand name as the first argument, or a numeric
opcode (section 9.1). `PsychNanoVG` with no arguments prints the list.
`PsychNanoVG('Name?')` prints the help for one subcommand.

Subcommand names are the NanoVG names without the `nvg` prefix: `nvgBeginPath`
becomes `BeginPath`, `nvgRoundedRectVarying` becomes `RoundedRectVarying`.

### 5.1 Lifecycle subcommands

| Subcommand | GL | Signature | Notes |
|---|---|---|---|
| Init | yes | `PsychNanoVG('Init' [, opts])` | Loads GL entry points, creates the context with `nvgCreateGL3` (or GL2 on macOS). `opts` fields: `antialias` (default true), `stencilStrokes` (default true), `debug` (default false), `logLevel`. `mexLock`, `mexAtExit`. Errors `psychnanovg:AlreadyInit`, `psychnanovg:GLInit`. |
| Shutdown | yes | `PsychNanoVG('Shutdown')` | Deletes render targets, images, fonts, and the context. `mexUnlock`. |
| BeginFrame | yes | `PsychNanoVG('BeginFrame', w, h [, pixelRatio=1])` | Saves viewport and blend state, sets viewport, calls `nvgBeginFrame`. |
| EndFrame | yes | `PsychNanoVG('EndFrame')` | `nvgEndFrame`, restore state, GL error drain. Records frame statistics. |
| CancelFrame | no | `PsychNanoVG('CancelFrame')` | `nvgCancelFrame`. |
| Version | no | `v = PsychNanoVG('Version')` | Struct: `nanovg` (commit), `psychnanovg`, `backend` (`GL3` or `GL2`), `glVersion`, `glRenderer`, `build`. |
| Opcode | no | `op = PsychNanoVG('Opcode', 'LineTo')` | Numeric opcode. |
| Stats | no | `s = PsychNanoVG('Stats' [, 'reset'])` | Section 9.2. |
| Enum | no | `v = PsychNanoVG('Enum', 'NVG_ALIGN_CENTER')` | Value from the generated enum table. Also accepts `'ALIGN_CENTER|ALIGN_MIDDLE'`. |
| FindSystemFont | no | `path = PsychNanoVG('FindSystemFont', family)` | Searches the OS font directories for a TTF or OTF file whose name matches. Returns `''` when not found. Implemented in `PsychNanoVGFonts.m`, not in the MEX. |

### 5.2 Generated subcommands

Grouped as in `nanovg.h`:

| Group | Subcommands |
|---|---|
| State | Save, Restore, Reset |
| Render styles | ShapeAntiAlias, StrokeColor, StrokePaint, FillColor, FillPaint, MiterLimit, StrokeWidth, LineCap, LineJoin, GlobalAlpha |
| Transforms | ResetTransform, Transform, Translate, Rotate, SkewX, SkewY, Scale, CurrentTransform (returns 1x6) |
| Transform helpers | TransformIdentity, TransformTranslate, TransformScale, TransformRotate, TransformSkewX, TransformSkewY, TransformMultiply, TransformPremultiply, TransformInverse, TransformPoint, DegToRad, RadToDeg (pure functions on 1x6 vectors) |
| Images | CreateImage (file), CreateImageRGBA (HxWx4 uint8), UpdateImage, ImageSize (returns 1x2), DeleteImage |
| Paints | LinearGradient, BoxGradient, RadialGradient, ImagePattern (return a paint index) |
| Scissoring | Scissor, IntersectScissor, ResetScissor |
| Paths | BeginPath, MoveTo, LineTo, BezierTo, QuadTo, ArcTo, ClosePath, PathWinding, Arc, Rect, RoundedRect, RoundedRectVarying, Ellipse, Circle, Fill, Stroke |
| Text | CreateFont, CreateFontAtIndex, CreateFontMem (uint8 vector), FindFont, AddFallbackFontId, AddFallbackFont, ResetFallbackFontsId, ResetFallbackFonts, FontSize, FontBlur, TextLetterSpacing, TextLineHeight, TextAlign, FontFaceId, FontFace, Text, TextBox, TextBounds (returns advance and 1x4 bounds), TextBoxBounds (returns 1x4), TextGlyphPositions (returns struct array), TextMetrics (returns ascender, descender, lineh), TextBreakLines (returns struct array) |
| Colors | RGB, RGBA, RGBf, RGBAf, LerpRGBA, TransRGBA, TransRGBAf, HSL, HSLA (return 1x4 double; provided for scripts that port NanoVG examples) |

### 5.3 Hand-written subcommands

| Subcommand | Signature | Notes |
|---|---|---|
| Polyline | `PsychNanoVG('Polyline', xy [, close=false])` | `xy` is Nx2 double. `MoveTo` on the first row, `LineTo` on the rest, `ClosePath` when requested. Appends to the current path. |
| Polygon | `PsychNanoVG('Polygon', xy)` | `Polyline` with close. |
| Path | `PsychNanoVG('Path', cmds)` | `cmds` is a cell array of `{'M', x, y}`, `{'L', x, y}`, `{'Q', cx, cy, x, y}`, `{'C', c1x, c1y, c2x, c2y, x, y}`, `{'Z'}`, or an Nx7 double matrix with a command code in column 1 and zero-padded arguments. The matrix form is the fast path. Phase 2: arcs, shapes, and the winding go in both forms, so a path with arcs is one call. Section 7.4 gives the encoding. |
| StrokeSegments | `PsychNanoVG('StrokeSegments', seg, rgba)` | Phase 2. `seg` is Nx4 `[x0 y0 x1 y1]`, `rgba` is Nx8 `[r0 g0 b0 a0 r1 g1 b1 a1]`. One stroke per segment with an `nvgLinearGradient` paint from the first color to the second, all in one call. Section 7.4 gives the rules. |
| Circles | `PsychNanoVG('Circles', cxyr)` | Nx3 matrix, one `Circle` per row, one path. For dot fields. |
| Rects | `PsychNanoVG('Rects', xywh)` | Nx4 matrix. |
| CreateImageFromTexture | `img = PsychNanoVG('CreateImageFromTexture', glTexId, w, h [, flags])` | `nvglCreateImageFromHandleGL3`. Wraps a PTB texture's GL id from `Screen('GetOpenGLTexture')` as a NanoVG image, for `ImagePattern`. |
| RenderTargetCreate | `[rt, glTexId] = PsychNanoVG('RenderTargetCreate', w, h [, imageFlags])` | `nvgluCreateFramebuffer`. Returns a target handle and the GL texture id for `Screen('SetOpenGLTexture')`. |
| RenderTargetBind | `PsychNanoVG('RenderTargetBind', rt)` | `nvgluBindFramebuffer` plus viewport. `BeginFrame` must follow with the target size. |
| RenderTargetUnbind | `PsychNanoVG('RenderTargetUnbind')` | Rebinds the FBO that was current before `RenderTargetBind`. |
| RenderTargetImage | `img = PsychNanoVG('RenderTargetImage', rt)` | The target's NanoVG image id, for `ImagePattern`. |
| RenderTargetDelete | `PsychNanoVG('RenderTargetDelete', rt)` | |
| PaintDelete | `PsychNanoVG('PaintDelete', paint)` | Frees a paint table entry. Paints are cheap; the table holds 256. |

### 5.4 Helper M-files

| File | Purpose |
|---|---|
| `m/PsychNanoVG.m` | Help text only. Generated. |
| `m/PsychNanoVGOp.m` | Generated opcode constants. |
| `m/PsychNanoVGSetup.m` | Puts `dist/<arch>` and `m/` on the path, in that order. |
| `m/PsychNanoVGOpen.m` | `vg = PsychNanoVGOpen(win [, opts])`. Checks that 3D graphics are on, then `Init` inside one OpenGL region, and loads a default sans font. Returns a struct with `win`, `rect`, `opened`, and `fonts`. |
| `m/PsychNanoVGFrame.m` | `PsychNanoVGFrame('Begin', vg [, w, h])` and `('End', vg)`. The frame and the OpenGL region together. The default size is the window rect. |
| `m/PsychNanoVGGL.m` | `[...] = PsychNanoVGGL(vg, subcommand, ...)`. One OpenGL subcommand inside one region. Passes through when a region is already open. |
| `m/PsychNanoVGClose.m` | `PsychNanoVGClose(vg)`. `Shutdown` inside one region. Safe twice, and safe after the window is closed. |
| `m/PsychNanoVGFonts.m` | `FindSystemFont` implementation per OS. |
| `m/PsychNanoVGPolylineGradient.m` | Phase 2. `[seg, col] = PsychNanoVGPolylineGradient(xy, rgba)`. A polyline with one color per vertex: builds the segment and color matrices of `StrokeSegments` from an Nx2 polyline and Nx4 colors, and draws them in one call. |
| `m/PsychNanoVGDemo.m` | Demo: antialiased ring stimulus with gradient edge, a Bezier trajectory, text with metrics, a cached render target, and (phase 2) a gauge built from arcs and a wave with one color per vertex. |

Every one of the four uses `Screen('EndOpenGL')` on the error path as well as
on the normal path. A MEX error inside a wrapped region therefore still
leaves Psychtoolbox in 2D drawing mode, which is what keeps the next `Screen`
command correct.

### 5.5 Error identifiers

| Identifier | Meaning |
|---|---|
| `psychnanovg:Usage` | Wrong number or class of arguments. |
| `psychnanovg:UnknownCommand` | Subcommand or opcode not found. |
| `psychnanovg:NotInit` | `Init` required. |
| `psychnanovg:AlreadyInit` | `Init` called twice. |
| `psychnanovg:NoGLContext` | GL subcommand without a current context. |
| `psychnanovg:GLInit` | GL loader or `nvgCreateGL3` failed. |
| `psychnanovg:GLError` | `glGetError` reported an error in `EndFrame`. |
| `psychnanovg:FrameState` | `BeginFrame` inside a frame, or a drawing call outside a frame. |
| `psychnanovg:Handle` | Unknown font, image, paint, or render target handle. |
| `psychnanovg:Font` | Font file not found or not parseable. |
| `psychnanovg:Type` | Argument class not accepted. |
| `psychnanovg:Range` | Numeric argument out of range. |

## 6. Coordinates, color, and text

### 6.1 Coordinates

NanoVG and PTB share a top-left origin with y down. `BeginFrame` takes the
target size in pixels. No flip is needed for the window. For a render target,
the texture that results is upright when drawn with `Screen('DrawTexture')`,
because PTB's `SetOpenGLTexture` assumes the same orientation as its own
offscreen windows. The demo verifies this.

### 6.2 Color and gamma

Colors are 1x4 double in 0 to 1, or 1x3 with alpha 1. NanoVG blends in the
framebuffer's color space with premultiplied alpha in its shader. On a
standard 8-bit PTB window, antialiased edges blend in sRGB space, which is
what `Screen` does too. On a PTB window opened with
`PsychImaging('AddTask', 'General', 'EnableSRGBRendering')` (or another
linear pipeline), `GL_FRAMEBUFFER_SRGB` is enabled by PTB and blending is
linear. The specification records this because edge luminance matters for
some stimuli. NanoVG's antialiasing fringe is one pixel wide; scripts that
need a controlled edge profile draw a gradient ring instead of relying on the
fringe.

### 6.3 Text

`CreateFont` takes a TTF or OTF path. `FindSystemFont` searches
`C:\Windows\Fonts`, `/usr/share/fonts`, `~/.fonts`, `/Library/Fonts`, and
`/System/Library/Fonts`. Text strings are MATLAB char. On MATLAB, UTF-16 is
converted to UTF-8 in a 4 KB stack buffer, heap above that. On Octave, char is
UTF-8 bytes already. `TextBounds`, `TextMetrics`, and `TextGlyphPositions` give
exact layout information for centering and for word-by-word presentation.

## 7. Marshaling rules and the generator

### 7.1 Generator

`gen/generate.py` runs with `uv run gen/generate.py`. It preprocesses
`nanovg.h` with the C compiler, parses it with pycparser, and emits
`src/gen_dispatch.c`, `src/gen_enums.c`, `m/PsychNanoVG.m`, `m/PsychNanoVGOp.m`,
and `tests/test_gen_marshal.m`. `gen/allowlist.txt` lists exclusions and
name overrides rather than inclusions, because the API is small and almost all
of it is wanted.

### 7.2 Type rules

| C type | MATLAB input | MATLAB output |
|---|---|---|
| `NVGcontext*` | implicit, the MEX context | |
| `float`, `int` | double scalar, range checked | double scalar |
| `float* xform` (1x6 in-out) | 1x6 double | 1x6 double |
| `float* bounds` (out, 4) | not passed | 1x4 double |
| `NVGcolor` | 1x4 or 1x3 double in 0 to 1 | 1x4 double |
| `NVGpaint` (return) | | paint index (double) |
| `NVGpaint` (input, `FillPaint`, `StrokePaint`) | paint index | |
| `const char* string, const char* end` | char; `end` is removed from the MATLAB signature and computed | |
| `const char* filename`, `const char* name` | char | |
| `const unsigned char* data, int ndata` | uint8 vector; `ndata` from `numel` | |
| `const unsigned char* data` for `CreateImageRGBA` and `UpdateImage` | HxWx4 uint8. MATLAB stores it column-major and NanoVG wants row-major RGBA. The handler transposes into a scratch buffer sized to the image. | |
| enums (`NVGwinding`, `NVGlineCap`, `NVGalign`, `NVGimageFlags`, `NVGcreateFlags`) | double, or a name with or without the `NVG_` prefix, or names joined with `\|` | double |
| `int image`, `int font` | handle from the creating call | handle |
| `NVGglyphPosition* positions, int maxPositions` | not passed; `maxPositions` from string length | struct array with `x`, `minx`, `maxx`, `index` |
| `NVGtextRow* rows, int maxRows` | `maxRows` optional, default 64 | struct array with `start`, `end`, `next`, `width`, `minx`, `maxx`, and the row text |

### 7.3 Excluded

- `nvgCreateInternal`, `nvgDeleteInternal`, `nvgInternalParams`, `nvgDebugDumpPathCache`.
- `nvgCreateFontMem` with `freeData = 1`; the MEX always copies and passes 0.
- The GLES2 and GLES3 backends.

### 7.4 Batched matrix encodings (phase 2)

The batched subcommands take column-major double or single matrices, the
layout that MATLAB stores. The handler checks the class and the column count
once per call. Inside the loop there is no allocation and no call back into
MATLAB.

`Path` matrix form. Each row is one command: the code in column 1 and the
arguments in columns 2 to 7, in the order of the NanoVG function, zero
padded. Angles are radians. y points down, so an angle that increases turns
clockwise on the screen.

| Code | Cell name | NanoVG call | Columns 2 to 7 |
|---|---|---|---|
| 1 | `M` | `nvgMoveTo` | x, y |
| 2 | `L` | `nvgLineTo` | x, y |
| 3 | `Q` | `nvgQuadTo` | cx, cy, x, y |
| 4 | `C` | `nvgBezierTo` | c1x, c1y, c2x, c2y, x, y |
| 5 | `Z` | `nvgClosePath` | none |
| 6 | `Arc` | `nvgArc` | cx, cy, r, a0, a1, dir |
| 7 | `ArcTo` | `nvgArcTo` | x1, y1, x2, y2, r |
| 8 | `Ellipse` | `nvgEllipse` | cx, cy, rx, ry |
| 9 | `Circle` | `nvgCircle` | cx, cy, r |
| 10 | `Rect` | `nvgRect` | x, y, w, h |
| 11 | `RoundedRect` | `nvgRoundedRect` | x, y, w, h, r |
| 12 | `Winding` | `nvgPathWinding` | dir |

Rules:

- The codes never change meaning. A new command gets the next free code.
- `dir` is 1 (`NVG_CCW`, `NVG_SOLID`) or 2 (`NVG_CW`, `NVG_HOLE`). In the
  cell form it can also be a constant name, as for the `Arc` and
  `PathWinding` subcommands.
- The handler validates column 1, and the `dir` of rows 6 and 12, for every
  row before it sends any row to NanoVG. A code that is not an integer from 1
  to 12, or a `dir` that is not 1 or 2, raises `psychnanovg:Range` and names
  the row. The path is then unchanged. The other arguments are not checked,
  the same as for the per-call subcommands.
- The cell form accepts the names in the table, in any case. A wrong
  argument count raises `psychnanovg:Usage`.

`StrokeSegments` form. Row i of `seg` is the segment `[x0 y0 x1 y1]`, and row
i of `rgba` holds its two colors, `[r0 g0 b0 a0 r1 g1 b1 a1]`.

- The two matrices have the same number of rows, or `psychnanovg:Usage`. They
  can differ in class.
- Each segment is `nvgBeginPath`, `nvgMoveTo`, `nvgLineTo`,
  `nvgStrokePaint(nvgLinearGradient(x0, y0, x1, y1, c0, c1))`, and
  `nvgStroke`. The stroke uses the current width, cap, join, transform,
  and global alpha.
- A run of contiguous segments, where each starts at the end of the one
  before and all have one color at both ends, becomes one path with one
  `nvgStrokeColor` and one `nvgStroke`. The run then has real joins and one
  draw call.
- The current path is replaced. The stroke paint that was current before the
  call is current again after it.

## 8. State, lifecycle, and error handling

### 8.1 State

One static struct: `NVGcontext* vg`, backend kind, `inFrame` flag, saved
viewport and blend state, paint table `NVGpaint paints[256]` with a free
list, render target table `NVGLUframebuffer* targets[16]` with the FBO that
was bound before each bind, `stats`, and a deferred error buffer.

Fonts and images are NanoVG integer ids and pass through unchanged. The MEX
keeps a bitset of live image ids so `DeleteImage` on an unknown id raises
`psychnanovg:Handle` instead of asserting inside NanoVG.

### 8.2 Init sequence

1. Check for a current GL context.
2. `gladLoadGL(psychnanovg_get_proc)` with a loader that tries
   `wglGetProcAddress` then `GetProcAddress(opengl32.dll)` on Windows,
   `glXGetProcAddressARB` on Linux, `dlsym` on the OpenGL framework on macOS.
3. Read `GL_VERSION`, `GL_RENDERER`, and `GL_STENCIL_BITS` of the current
   drawing target. Warn when stencil bits are 0: concave fills and
   `NVG_STENCIL_STROKES` will render incorrectly.
4. `vg = nvgCreateGL3(flags)` or `nvgCreateGL2(flags)`. Raise
   `psychnanovg:GLInit` on NULL.
5. `mexLock`, `mexAtExit`.

### 8.3 Frame sequence

`BeginFrame`: check `inFrame == 0`, save `GL_VIEWPORT`, `GL_BLEND`,
`GL_BLEND_SRC_ALPHA`, `GL_BLEND_DST_ALPHA`, `GL_BLEND_SRC_RGB`,
`GL_BLEND_DST_RGB`, `GL_ACTIVE_TEXTURE`, set `glViewport(0, 0, w, h)`,
`nvgBeginFrame(vg, w, h, pixelRatio)`, set `inFrame`.

`EndFrame`: `nvgEndFrame(vg)`, restore the saved state, `glGetError` drain,
clear `inFrame`, record statistics.

### 8.4 Errors

NanoVG has no assert hook. Its internal asserts are C `assert`, compiled out
in release builds. The MEX validates handles and frame state before calling
NanoVG so that release builds never reach an invalid state. GL errors are
reported through `psychnanovg:GLError` from `EndFrame` and from the image and
render target subcommands.

## 9. Performance and profiling

### 9.1 Dispatch

Same design as the other subcommand MEX files in this workspace: names read
with `mxGetString` into a 64-byte stack buffer, binary search in a generated
sorted table, and a numeric opcode fast path with constants from
`PsychNanoVGOp.m`. Budget: under 0.5 us for dispatch plus marshaling of a
scalar call, so per-vertex `LineTo` calls cost about 2 us each from MATLAB.
Scripts with more than a few hundred vertices per frame use `Polyline`,
`Path`, `Circles`, or `Rects`.

NanoVG's own cost is in `nvgEndFrame`: path flattening and triangulation on
the CPU, then one or a few GL draw calls. Text goes through the fontstash atlas
texture.

### 9.2 Built-in Stats

Always compiled unless `PSYCHNANOVG_STATS=0`. Per subcommand `calls`,
`totalNs`, `maxNs`. Per frame: `endFrameNs` last, max, sum; `gpuNs` from a
`GL_TIMESTAMP` query pair read two frames later; `drawCalls`, `fillCount`,
`strokeCount`, `textCount`, and `vertexCount` from NanoVG's internal counters
when built with `NANOVG_STATS`. Phase 2: `gpuNs` is NaN when the context has
no timer queries (section 14.7).

### 9.3 Tracy

CMake option `PSYCHNANOVG_TRACY` (default OFF) compiles `TracyClient.cpp` into
the static library so the MEX stays C. `PNVG_ZONE(name)` over `TracyCZoneN`
around dispatch, `EndFrame`, and image uploads; `TracyCGpuZone` around
`nvgEndFrame`. Export with `tracy-csvexport`.

### 9.4 What to measure first

1. `EndFrame` CPU time for the demo ring and for a 10,000-point `Polyline`.
2. GPU time for the same.
3. Per-call cost of `LineTo` against `Polyline` for 1,000 points.

## 10. Build

### 10.1 Layout

```
PsychNanoVG/
  CMakeLists.txt          builds nanovg static lib (nanovg.c + glad + optional Tracy)
  build.m
  README.md
  SPEC.md
  src/
    psychnanovg.c         mexFunction, dispatch, state, lifecycle, frame
    pnvg_gl.c             nanovg_gl.h implementation unit (NANOVG_GL3_IMPLEMENTATION or GL2), gl utils, loader
    pnvg_batch.c          Polyline, Polygon, Path, Circles, Rects
    pnvg_targets.c        render targets, CreateImageFromTexture
    pnvg_marshal.h pnvg_internal.h pnvg_profiler.h
    gen_dispatch.c gen_enums.c      generated, committed
  gen/
    pyproject.toml generate.py allowlist.txt templates/
  m/
    PsychNanoVG.m PsychNanoVGOp.m PsychNanoVGFonts.m PsychNanoVGDemo.m
  tests/
    run_tests.m test_dispatch.m test_gen_marshal.m test_paths.m test_transforms.m
    gl/test_gl_shapes.m gl/test_gl_text.m gl/test_gl_target.m
  perf/PsychNanoVGPerf.m
  third_party/
    nanovg/               submodule
    glad/                 generated header and source, committed
    tracy/                submodule, optional
```

### 10.2 Flow

`build.m` follows the user's `mex-msgpack` pattern: detect the engine, build
the static library with CMake using the engine's compiler (MSVC 2022 for
MATLAB, MinGW gcc from `mkoctfile -p CC` for Octave, separate
`build-matlab/` and `build-octave/`), then `mex` with `-R2017b`, the library,
and `opengl32.lib`, `-lGL`, or `-framework OpenGL`. CMake selects
`NANOVG_GL3_IMPLEMENTATION` on Windows and Linux and `NANOVG_GL2_IMPLEMENTATION`
on macOS. Options: `PSYCHNANOVG_TRACY`, `PSYCHNANOVG_STATS`. `build.m gen` runs
the generator. `build.m test` runs `run_tests.m`.

### 10.3 CI

Same shape as `mex-msgpack`. CI runs the no-GL tests and compiles the GL code
without running it. GL tests run on developer machines.

## 11. Testing

### 11.1 Without a GPU

Path and transform subcommands do not touch GL until `EndFrame`. Test builds
add `Init` option `renderer='null'`, which creates the context with NanoVG's
internal parameters and a null render backend (all callbacks empty). This
allows `run_tests.m` to cover dispatch, marshaling of every generated
function, `Path` and `Polyline` argument handling, transform helpers against
known matrices, paint table exhaustion, frame state errors, and `Stats`.

### 11.2 With PTB and a GPU

`tests/gl/` opens a 640x480 PTB window:

- `test_gl_shapes.m`: a filled circle and a stroked rectangle at known
  positions, `Screen('GetImage')`, mean color inside and outside within a
  tolerance, antialiased edge width about one pixel.
- `test_gl_text.m`: `TextBounds` agrees with the rendered extent within two
  pixels; `TextMetrics` returns positive ascender and line height.
- `test_gl_target.m`: draw into a render target, wrap with
  `Screen('SetOpenGLTexture')`, `DrawTexture`, read back, compare with direct
  drawing of the same shape, same orientation.
- GL error attribution: a test-only subcommand injects a GL error and
  `EndFrame` raises `psychnanovg:GLError`.

### 11.3 Interactive

`PsychNanoVGDemo.m`: a ring with a radial gradient edge, a Bezier path traced
over time, centered text with metrics, and the same ring cached in a render
target drawn 100 times. `perf/PsychNanoVGPerf.m` prints the timings of
section 9.4.

## 12. Risks, alternatives considered, open questions

### 12.1 Risks

| Risk | Mitigation |
|---|---|
| No stencil buffer on the drawing target | `Init` reads `GL_STENCIL_BITS` and warns. PTB windows have stencil by default. |
| NanoVG blends in sRGB space on standard windows | Documented in section 6.2. Same as `Screen`. |
| Upstream NanoVG maintenance is slow | The library is small and stable. The submodule pins a commit. |
| GL state left by NanoVG affects other userspace GL code | `EndFrame` restores viewport, blend, and active texture. PTB's own context is isolated regardless. |
| Column-major image data | Transposed into a scratch buffer in `CreateImageRGBA` and `UpdateImage`. Cost is one pass over the image. |
| macOS GL 2.1 | GL2 backend, no VAO, `#version 120` shaders. Built and tested on `macos-latest`. The GL2 backend needs the framebuffer object entry points, which are not core in 2.1, so glad is generated with `GL_ARB_framebuffer_object`. |

### 12.2 Alternatives considered

- Reuse the NanoVG copy inside LVGL. Rejected: it includes `lvgl_public.h` and
  is compiled as part of LVGL. This project stays independent.
- Draw only into render targets and never into the PTB framebuffer. Rejected:
  direct drawing lets `Screen` and NanoVG commands interleave freely and needs
  no texture management for dynamic stimuli. Render targets remain available
  for static content.
- Bind PTB textures as NanoVG images by copying pixels. Rejected:
  `Screen('GetOpenGLTexture')` gives the GL id, and NanoVG can wrap it with no
  copy.
- Hand-write all bindings. Rejected: the generator costs little and produces
  the help text and tests.

### 12.3 Open questions

| Question | Default assumed by this specification |
|---|---|
| Should `Init` default `NVG_STENCIL_STROKES` on? | Yes. It gives correct overlapping strokes at a small cost. |
| Provide `FindSystemFont` at all, or require explicit paths? | Provide it. Experiments run on lab machines with unknown font sets. |
| Should `Polyline` accept single as well as double? | Yes, both, converted to float in the handler. |
| macOS support? | Apple silicon, with the GL2 backend, built and tested by CI. First CI run 2026-09-22: the Homebrew Octave build passed and is blocking; the smoke test reached a 2.1 context on the Apple Software Renderer and passed every check but the release of the offscreen target, fixed by making render target binds a stack (section 14); the MATLAB build failed on `mex` rejecting a bare `-framework` argument, fixed with the `LDFLAGS=` form. Round 2 (same day): the MATLAB build and the smoke test passed and are blocking; the Octave build passed but one Stats assertion failed, because `clock_gettime(CLOCK_MONOTONIC)` is rounded to microseconds on macOS and a null renderer frame measured 0 ns; the timer now uses `clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW)` on Apple. Intel Macs are not covered. |

## 13. Phasing

| Phase | Content |
|---|---|
| 1 | Lifecycle, generator, all generated subcommands, batched paths, paints, fonts, images from files and arrays, `Stats`, null-renderer tests, GL tests, demo. |
| 2 | Render targets and `CreateImageFromTexture` (both delivered in phase 1), Tracy GPU zones, `Path` matrix form with arcs, per-vertex color polylines through `LinearGradient` helpers. Delivered; section 14.7 records the differences. |
| 3 | Multiple contexts for multiple PTB windows, GLES backends if PTB on embedded Linux needs them. |

## 14. Deviations from version 0.1

Phases 1 and 2 are implemented. This section records every place where the
implementation differs from sections 1 to 13, and the reason. Sections 1 to 13
hold the version 0.1 text, except the phase 2 additions to sections 5, 7, 9.2,
and 13, which are marked "phase 2". Section 14.7 holds the phase 2 rows.

### 14.1 Dependencies and build

| Deviation | Reason |
|---|---|
| NanoVG is a git submodule under `third_party/nanovg`, pinned at `ce3bf745eb2d2dbc14a50bf2446783f691ac4353` (2026-02-19) as recorded in `third_party/PINS.md`. Tracy is not vendored. | The repository did not exist while phase 1 was written, so NanoVG was a plain clone until it was registered as a submodule at the same commit on 2026-09-22. Tracy is optional and large, and a build without it must not need it, so a developer clones it into `third_party/tracy` by hand (section 14.7). |
| Phase 1 only: `PSYCHNANOVG_TRACY` stayed OFF, and the only zone was `PNVG_ZONE("dispatch")`. Phase 2 wires the option; section 14.7 describes the zones. | The zone macros compile to nothing while the option is off, so they cost nothing and the CMake path could be tested later without touching the sources. |
| glad is generated for `gl:compatibility=3.3` with no extensions. | Every GL symbol that `nanovg_gl.h` and `nanovg_gl_utils.h` use is core in 3.3, and so is `glQueryCounter` for the GPU timer. The full extension set would make the header five times larger for no gain. |
| `mex` gets `-R2017b` under MATLAB only. | `-R2017b` names the API that the code already uses, so it states the intent under MATLAB. Octave's `mex` does not accept the flag. |
| `build.m` passes `-DPNVG_OCTAVE=1` under Octave. | Section 6.3 needs different string marshaling for each engine, and no standard macro tells a MEX file which engine compiled it. |
| Octave on Windows configures CMake with `-G "Unix Makefiles"` and Octave's own `usr/bin/make.exe`, not with `-G "MinGW Makefiles"`. `MEX_CMAKE_GENERATOR` still overrides the choice. | Octave 10.1 on Windows is an MSYS2 tree and puts `sh.exe` on PATH itself. The "MinGW Makefiles" generator refuses to run while `sh.exe` is on PATH, and the directory that holds `sh.exe` also holds `make.exe`. Ninja is not installed on this machine. |
| CMake compiles `src/core/pnvg_nanovg_unit.c`, which includes `nanovg.c`, instead of compiling `nanovg.c` directly. | Section 9.2 asks for NanoVG's draw counters. They live in the private `NVGcontext` struct in `nanovg.c`, there is no accessor, and upstream has no `NANOVG_STATS` switch. Including the file gives one small accessor and leaves the vendored clone unpatched. |
| `src/core/` holds the NanoVG-facing layer, and the `mx` marshaling sits on top of it. Section 10.1 does not name this directory. | `tests/smoke_gl.c` links the same code without MATLAB. Without the split there would be no way to exercise the GL path on a machine that has no Psychtoolbox. |
| `dist` must come before `m` on the MATLAB path. | `m/PsychNanoVG.m` carries the help text and also answers when the MEX is missing. A MEX file only takes precedence over an M-file in the same directory. |

### 14.2 Generated API

| Deviation | Reason |
|---|---|
| 96 generated subcommands and 24 hand-written ones, 120 in total (119 before phase 2 added `StrokeSegments`). Section 1.2 estimates about 95 generated. | The count includes `CreateImageMem`, `CreateFontMemAtIndex`, and the three composite operation setters, which section 7.3 does not exclude. |
| `nvgBeginFrame`, `nvgEndFrame`, and `nvgCancelFrame` are excluded from generation. | Section 5.1 gives all three hand-written handlers that also save and restore GL state, set the viewport, drain GL errors, and record statistics. Two commands cannot share one name. The generator now refuses a duplicate name instead of emitting one. |
| `nvgResetFallbackFonts` is excluded from generation and hand-written. | It passes the result of `nvgFindFont` straight to `fonsResetFallbackFont`, which indexes `stash->fonts` with it and never checks it. An unknown family name is -1 there, so the generated wrapper turned a typo in a script into a process crash. The hand-written handler resolves the name and raises `psychnanovg:Handle`. This was found by the generated marshaling test. |
| Font handles are validated the same way section 8.1 validates image handles, with a high water mark instead of a bitset. `int font`, `int baseFont`, and `int fallbackFont` all go through the check, and a bad handle raises `psychnanovg:Handle`. | `fonsAddFallbackFont` and `fonsResetFallbackFont` dereference `stash->fonts[id]` with no check of their own, and an unused slot holds NULL. Section 8.4 requires the MEX to validate handles before it calls NanoVG; section 8.1 lists only images. Fontstash ids count up from 0 and are never freed, so one count is enough. |
| `CreateImageRGBA` drops `w` and `h` from the MATLAB signature and takes them from `size(data)`. | Section 7.2 says the pixels arrive as HxWx4, which already carries the size, in the same way that `ndata` comes from `numel`. Two sources for one number can disagree. |
| `float* dst` is an output, not an input-output, except in `TransformMultiply` and `TransformPremultiply`. `TransformIdentity` therefore takes no argument, `TransformTranslate(tx, ty)` returns a 1x6, and `TransformInverse(src)` returns `[dst, ok]`. | Section 7.2 describes `float* xform` as 1x6 in-out. That is true only for the two functions that read `dst` before they write it. `dst = TransformIdentity(dst)` would be a signature with no meaning. |
| Two out scalars named `(w, h)` or `(dstx, dsty)` become one 1x2 output. `TextMetrics` keeps three separate outputs. | Section 5.2 asks `ImageSize` for a 1x2 and `TextMetrics` for three values. The rule follows both. |
| `TextGlyphPositions` holds at most 2048 glyphs and `TextBreakLines` at most 256 rows. A longer string is clipped, not rejected. | Both buffers sit on the handler stack, which keeps the per-call path free of heap traffic. |
| The `index` field of `TextGlyphPositions`, and `start`, `end`, and `next` of `TextBreakLines`, are 1-based byte offsets into the UTF-8 string that was passed in. | NanoVG reports pointers into that string. MATLAB indexes from 1. A byte offset is the only one of the two that survives the call. |
| The `text` field of a `TextBreakLines` row is built with `mxCreateString`, so non-ASCII row text is read in the local code page under MATLAB. | The layout fields are exact, which is the purpose of the field. Correct UTF-8 char output needs `mxCreateCharArray` and a decode step, which phase 2 can add. |
| Text style setters and text measurement require a frame. Section 5.2 groups them with the other text calls and says nothing about frame scope. | `nvgBeginFrame` calls `nvgReset`, so a font size or an alignment set outside a frame is discarded before the next frame draws, and a measurement made outside a frame measures the wrong state. The paint constructors do not require a frame, because they read no context state. |
| The `Enum` table holds the 49 constants of `nanovg.h` and the three of `NVGcreateFlags` in `nanovg_gl.h`. | Section 5.1 lets `Init` name its flags, so the names have to be in the table. `nanovg_gl.h` cannot be preprocessed without a GL header in scope, so that one enum block is read textually. |
| `FindSystemFont` is a MEX subcommand that forwards to `PsychNanoVGFonts.m` through `mexCallMATLAB`. | Section 5.1 lists it as a subcommand and also puts the implementation in `PsychNanoVGFonts.m`. A MEX file shadows an M-file of the same name, so the subcommand cannot reach the M-file by name alone. |
| `opts.logLevel` of section 5.1 is accepted and ignored. | There is nothing to log yet. Errors use the identifiers of section 5.5 and warnings use `mexWarnMsgIdAndTxt`. |
| The generator prefers a real C preprocessor and falls back to a small built-in one when no compiler is on PATH. | Section 7.1 asks for the C compiler. `nanovg.h` includes nothing and uses no function-like macros, so the fallback is exact for this header and keeps the generator runnable on a machine with no compiler. |

### 14.3 Scope

| Deviation | Reason |
|---|---|
| Render targets and `CreateImageFromTexture` are implemented now, although section 13 puts them in phase 2. | `nvgluCreateFramebuffer` and `nvglCreateImageFromHandleGL3` are already in the vendored headers, so the work was small, and the native smoke test can exercise them while a GL context is current. |
| Phase 1 only: the `Path` matrix form covered M, L, Q, C, and Z, codes 1 to 5. Phase 2 adds codes 6 to 12 (section 7.4). | Section 13 put arcs in the matrix form in phase 2. |
| `vertexCount` in `Stats` is the sum of NanoVG's fill, stroke, and text triangle counts, times three. | NanoVG counts triangles, not vertices. The three counts are reported separately as well, so nothing is lost. |
| Every script that opens a window, that is `tests/gl/*.m`, `perf/PsychNanoVGPerf('gl')`, and `m/PsychNanoVGDemo.m`, opens it through `tests/gl/ptb_test_window.m`. That helper sets `Screen('Preference', 'SkipSyncTests', 2)` and `Screen('Preference', 'VisualDebugLevel', 0)` before `PsychImaging('OpenWindow')`, and the tests ask it for a 640x480 windowed target. This is an addition to section 11.2. | An unattended `run_tests` must not stop for the display sync report or the welcome splash, and a 640x480 window cannot pass the sync tests in any case. One helper keeps the two preferences in one place, so a later change reaches every script at once. |
| `tests/gl/` needs a render target to be cleared before it is drawn into. `nvgluCreateFramebuffer` calls `nvgCreateImageRGBA` with a NULL pixel pointer, so the texture holds whatever was in that memory. The tests and the demo clear it with `glClear` through mogl. | Clearing inside `RenderTargetBind` would be a policy that section 5.3 does not describe, and a caller that draws a full-bleed background does not need the clear. The first version of `test_gl_target` missed this and reported a mismatch that was uninitialized memory, not a flip. |
| `test_gl_text` accepts a 5 pixel difference between `TextBounds` and the rendered extent. Section 11.2 asks for 2 pixels. | `TextBounds` reports the glyph quads, which carry the padding that fontstash puts around each glyph in the atlas. Measured against Arial at 48 points, the quads sit 3 to 4 pixels outside the ink on every side. The test also asserts the stronger property, that the bounds contain the ink. |

### 14.4 Continuous integration

Section 10.3 asks for the shape of `mex-msgpack`, with CI running the tests
that need no GL and compiling the GL code without running it.

| Deviation | Reason |
|---|---|
| The workflow is `.github/workflows/ci.yml` and has no path filter. | This project is its own repository, `PsychNanoVG`, not a directory inside a shared one. Every step runs at the repository root, so the workflow needs no `working-directory` and no prefix on any artifact path. |
| CI does more than compile the GL code: `smoke-gl-linux` builds `tests/smoke_gl.c` and runs it under `xvfb-run` with the Mesa llvmpipe software rasterizer. | Section 10.3 was written before `smoke_gl` existed. llvmpipe gives a real GL 4.5 compatibility context with an 8-bit stencil visual, so the whole backend, the shaders, the stencil fills, and a pixel readback all run on a machine with no GPU. The Windows job still compiles only, because the hosted Windows runner has no GPU and no desktop session for WGL. |
| The `smoke-gl-windows-compile` job builds the target and does not run it. | The hosted Windows runner cannot create a WGL context. |
| `tests/smoke_gl.c` includes the platform GL header rather than glad, and gained a GLX branch beside the WGL one. | The GLX declarations come from `GL/glx.h`, which includes `GL/gl.h`, and glad refuses to share a translation unit with it. Everything the test calls itself is GL 1.1, so the platform header is enough; the modern GL stays behind the core layer and its glad loader. |
| The build and install directories carry a platform suffix outside Windows, for example `build-octave-linux`. Section 10.2 names only `build-matlab/` and `build-octave/`. | One working tree is often shared between Windows and WSL. CMake refuses to configure a directory that another toolchain already used. |
| The MEX goes to `dist/<arch>/PsychNanoVG.<mexext>`, not to `dist/`. Section 10.2 says only `dist/PsychNanoVG.<mexext>`. | Octave names its MEX `PsychNanoVG.mex` on every operating system, so in a tree shared between Windows and WSL the second build replaced the first. The split also lets one tree hold every platform at once, which is what the CI artifacts carry. `m/PsychNanoVGSetup.m` owns the platform name and the path order, so nothing else has to know the layout, and it raises `psychnanovg:NotBuilt` naming the file it looked for. Octave's `computer('arch')` reports a GNU triplet rather than MATLAB's name, so the helper derives `win64`, `glnxa64`, `maci64`, or `maca64` itself. |
| Five M-files sit between a script and the MEX: `PsychNanoVGSetup`, `PsychNanoVGOpen`, `PsychNanoVGFrame`, `PsychNanoVGGL`, and `PsychNanoVGClose`. Section 5.4 lists only the help text, the opcodes, the font search, and the demo, and section 4.3 had the script write every `Screen('BeginOpenGL')` pair itself. | An unbalanced pair leaves Psychtoolbox in userspace rendering mode, and every later `Screen` drawing command then goes to the wrong place. A script cannot get that wrong if it never writes the pair. The helpers also give the three sibling projects one shape to share. The raw form stays documented, because several subcommands in one region, such as a render target, still need it. `PsychNanoVGOpen` adds one identifier to section 5.5, `psychnanovg:No3DGraphics`, for a window that was opened without `InitializeMatlabOpenGL`. |
| `h_FindSystemFont` duplicates its argument before it calls `mexCallMATLAB`, and `num_at` no longer calls `mexCallMATLAB` at all. | The first version passed `prhs[0]` straight into `mexCallMATLAB`, which gives the interpreter a second owner of an array that it already owns. The MEX API forbids it, and Octave can free the array. `num_at` used the same call to widen the rare integer classes; it now reads every numeric class from `mxGetData`, which is also one fewer place where the interpreter can run inside an argument reader. |
| `tests/run_tests.m` puts `tests/stub` on the path once, before the first call loads the MEX file, and `tests/test_helpers.m` changes nothing about the path. The stub is removed again only on a machine that has Psychtoolbox, just before `tests/gl`. | A load path change, or a `rehash`, while a locked MEX file is loaded sends Octave 10 into an endless recursion: after `warning: library ... not reloaded due to existing references`, `octave::out_of_date_check` asks the breakpoint table to drop the breakpoints of the function it is about to reload, and that lookup re-enters `out_of_date_check`. The two alternate for more than 35000 stack frames until the stack is exhausted and the process takes SIGSEGV. `test_helpers` used to add and remove the stub directory itself, with an `onCleanup`, which is what started the cycle on the `build . linux . Octave 10.1.0` job; Octave 6.4 has no such cycle and MATLAB is unaffected, which is why only that job failed. Changing the path once, before the MEX is loaded, removes the trigger, and `PsychNanoVGSetup` is idempotent for the same reason, because `PsychNanoVGOpen` calls it on every open. The backtrace comes from the PsychLVGL worker, who reproduced the recursion deterministically in the same image by touching the MEX and calling `rehash` while it was locked and live. |
| macOS on Apple silicon is built and tested by CI, with the GL2 backend. `Init` accepts `auto`, `null`, and the one GL backend that the build has, and refuses the other by name. glad is generated with `GL_ARB_framebuffer_object`. `tests/smoke_gl.c` gained a CGL branch that renders into a render target. | Sections 1.2, 12.1, and 12.3 called macOS best effort and not CI-blocking, and section 10.2 named no macOS build. The backend cannot be chosen at run time, because `nanovg_gl.h` is one implementation unit, so `opts.renderer` now validates against the compiled backend instead of pretending to pick one. The GL2 backend calls the unsuffixed framebuffer object entry points, which are not core in GL 2.1, so glad has to load them from `GL_ARB_framebuffer_object`; one glad header still serves every platform, because those names resolve from core on GL 3.3. `nanovg_gl_utils.h` includes `<OpenGL/glext.h>` on an Apple GL2 build, which would collide with glad, so `pnvg_gl.c` sets that header's include guards first. A CGL context has no drawable and therefore no default framebuffer or stencil buffer, so the macOS smoke test binds a NanoVG render target and reads the stencil size from that. Nobody on the team has a Mac: the three macOS CI jobs carry `continue-on-error` until the first green run, and everything in this row was checked by inspection and by compiling the shared code on Windows and Linux, not on macOS. |
| `FindSystemFont` walks three directory levels, not one. | The Linux layout is `/usr/share/fonts/truetype/<family>/<file>.ttf`, which is two levels below the root that section 6.3 lists. With one level the search found nothing on Ubuntu. `dir('**')` is a MATLAB extension that Octave does not have, so the walk is explicit and depth limited. |

### 14.5 Native GL smoke test

Section 11 has no equivalent. `tests/smoke_gl.c` and the CMake option
`PSYCHNANOVG_SMOKE_GL` add a native program that opens its own off-screen
window and a WGL compatibility context with an 8-bit stencil buffer, loads GL
through the same proc loader, and drives the core layer: `Init`, `BeginFrame`,
a filled circle, a stroked rectangle, a 10,000 point `Polyline`, text from a
system font, `EndFrame`, a `glGetError` check, a `glReadPixels` comparison
that proves the circle changed the pixels, a render target round trip, and
`Shutdown`. It exists because the GL path had to be testable before
Psychtoolbox was known to work on the development machine. It stays because it
runs in one second, needs no window manager, and covers Octave, where the
Psychtoolbox `Screen` MEX does not load.

### 14.6 Not verified

| Item | State |
|---|---|
| `tests/gl/` under Octave | Not run. The Psychtoolbox `Screen.mex` for Octave on this machine fails to load with Windows error 126. `run_tests` reports the directory as skipped. Under MATLAB the same tests run and pass. |
| Linux | Built and tested. Octave 6.4.0 on Ubuntu 22.04 under WSL passes the same 201 assertions as Octave on Windows, and `smoke_gl` passes all of its checks under Xvfb with llvmpipe. |
| macOS | Built and tested by CI on Apple silicon (section 12.3). No one on the team has run it on a Mac with a display. The phase 2 GPU timer and Tracy GPU zone code is compiled there but not run against a Psychtoolbox window. |
| The GitHub Actions workflow | Runs on every push, green on Linux, Windows, and macOS at commit 33888c1. |
| `m/PsychNanoVGDemo.m` | Run for two seconds under MATLAB R2023a on 2026-09-22, with the phase 2 gauge and wave. It exits without an error and prints the `EndFrame` line. Not inspected by eye for more than that. |
| Render target binds nest: the core keeps a stack of bound targets, and `RenderTargetBind` refuses a target that is already on it. Section 5.3 described one bound target at a time. | The macOS smoke test binds an offscreen target for the whole run because a drawable-less CGL context has no default framebuffer, and the render target round trip inside it used to overwrite the single "bound target" record, so the outer unbind reported nothing bound. `RenderTargetUnbind` now returns to the framebuffer that was current before the innermost bind, `RenderTargetDelete` of a bound target unwinds to it, binding the same target twice raises `psychnanovg:FrameState`, and the MEX handlers map the core status codes onto the section 5.5 identifiers instead of assuming `Handle`. `tests/gl/test_gl_target.m` covers the nesting. |

### 14.7 Phase 2

Phase 2 was implemented on 2026-09-22. The rows below record where it differs
from sections 5, 7, 9, and 11, or adds to them.

| Deviation | Reason |
|---|---|
| A new hand-written subcommand, `StrokeSegments` (sections 5.3 and 7.4). Section 13 asks for per-vertex color polylines "through `LinearGradient` helpers" and names no subcommand. | Before phase 2, one gradient segment took seven subcommands: `LinearGradient`, `BeginPath`, `MoveTo`, `LineTo`, `StrokePaint`, `Stroke`, and `PaintDelete`. For 1,000 segments that loop costs 14.5 us per segment under MATLAB with the GL renderer and 111 us per segment under Octave. `StrokeSegments` does the same work in C for all segments in one call. |
| The table now has 120 subcommands. The opcodes of the 27 subcommands whose names sort after `StrokeSegments`, from `StrokeWidth` to `Version`, went up by one. | The opcode is the position in the table that is sorted by name (section 9.1). A script that uses `PsychNanoVGOp` gets the new numbers. A script that hard-codes an opcode number must change. |
| The `Path` matrix form has codes 6 to 12: `Arc`, `ArcTo`, `Ellipse`, `Circle`, `Rect`, `RoundedRect`, and `Winding`. Section 13 asks only for arcs. | A gauge or a ring needs a hole, and a hole needs `Winding` after the inner shape. The four shape commands are one line each in the handler. With all of them, any path that the per-call subcommands can build is one `Path` call. |
| The cell form names the new commands by the NanoVG name, such as `{'Arc', ...}`, in any case, not by one letter. A name must now match a whole entry of the section 7.4 table. Phase 1 read only the first letter, so `{'MoveTo', x, y}` worked as `M`; it now raises `psychnanovg:Usage`. | The single letters of phase 1 follow SVG. SVG's `A` is an elliptical arc between two end points, which is not `nvgArc`, and a second meaning for a known letter would mislead. A first-letter match cannot tell `C` from `Circle`. |
| `Path` validates every row of the matrix before it sends one to NanoVG. In phase 1 the rows before a bad row were already in the path when the error came. The error identifier is still `psychnanovg:Range`, and the message names the row. | An error now leaves the path as it was, and the drawing loop has no error branch. The check is one pass over column 1, which costs little next to the NanoVG calls. `tests/test_paths.m` used code 9 as its example of a bad code; code 9 is now `Circle`, so the test uses 99. |
| `StrokeSegments` puts the caller's stroke paint back through a small accessor in `src/core/pnvg_nanovg_unit.c`, not through `nvgSave` and `nvgRestore`. | `nvgStrokePaint` multiplies the paint by the current transform, so it cannot restore a saved paint unchanged. `nvgSave` does nothing when the state stack is full, and the matching `nvgRestore` would then remove the caller's own state. |
| `StrokeSegments` strokes a run of contiguous segments that have one color as one path with `nvgStrokeColor`. | That is the "minimal set of strokes" for a polyline: a gradient buys nothing along one color, and one path gives the run real joins and one draw call instead of one per segment. |
| `PsychNanoVGPolylineGradient` leaves out segments of zero length. Gradient segments are separate strokes, so they have no joins between them. The help text tells the caller to set `LineCap` to `ROUND`. | A repeated vertex gives a gradient no direction. A join needs one path, and one path can have only one paint. |
| `Stats` reports `gpuNs` as NaN when the context cannot measure GPU time: the null renderer, and a GL context below 3.3 without `GL_ARB_timer_query`. It was 0 in phase 1. The first two frames on a context with timer queries still report 0. | 0 ns is a possible measurement; NaN is not. A script can now tell "not measured" from "fast". |
| The timer queries are created in `Init` and deleted in `Shutdown`. On a context below 3.3 that has `GL_ARB_timer_query`, the two entry points are loaded by hand. `CancelFrame` closes the query pair that `BeginFrame` opened. | In phase 1 the queries were created at the first `BeginFrame` and never deleted, so every `Init` left six query objects behind. glad loads the GL 3.3 entry points only from a 3.3 context, and the extension uses the same names. Without the close, a cancelled frame left a Tracy GPU zone with no end. |
| Tracy uses the C API, `___tracy_emit_gpu_*_serial`, not `TracyCGpuZone`. The GPU zone is named "NanoVG frame" and spans `BeginFrame` to `EndFrame`, from the same `GL_TIMESTAMP` query pair that gives `gpuNs`. Section 9.3 puts the zone around `nvgEndFrame` only. | `TracyC.h` has no GPU zone macro; its GPU functions take query ids that the caller manages, and the section 9.2 ring already manages them. NanoVG issues all of its GL work inside `nvgEndFrame`, so the longer span holds the same GPU work, and the zone costs no extra query. |
| With Tracy compiled in, `EndFrame` waits for a query pair that is not ready when its slot is about to be written again. Without Tracy it drops the pair, as in phase 1. | Tracy was told that the zone began and ended, and it waits for both timestamps. A dropped pair leaves a GPU zone with no end in the capture. Two frames of latency make the wait rare. |
| `src/core/pnvg_tracy.cpp` is a C++ file of one function, compiled only with Tracy. It takes the GPU context id from Tracy's shared counter. | The C API has no call that hands out a GPU context id, and a fixed id can collide with another GPU context in the same process. |
| The profiler uses `TRACY_MANUAL_LIFETIME` and `TRACY_DELAYED_INIT`. It starts at the first MEX call, and at `pnvg_init` for `smoke_gl`, and stops in `mexAtExit`. | The profiler threads run code inside the MEX file. Static constructors would start them at load time and leave them running while MATLAB unloads the file. |
| Tracy is compiled with `TRACY_NO_CRASH_HANDLER`. | MATLAB's JVM raises access violations on purpose and handles them. With a capture connected, Tracy's crash handler took one of them for a crash, suspended every other thread, and MATLAB hung in `Screen('CloseAll')`. Without a capture the handler does nothing, which is why the test suite did not show it. |
| The CPU zones carry the subcommand name, from a static table of source locations, instead of one "dispatch" zone. The core adds zones for `Polyline`, `Circles`, `Rects`, `PathMatrix`, `StrokeSegments`, `ImageTranspose`, `nvgEndFrame`, "GPU timer", and "GL restore and error drain". Text has no zone of its own: the `Text`, `TextBox`, and other text subcommands get their zone from the dispatcher. | One name per subcommand is what a timeline needs, and the table is filled once, so the per-call path does not allocate. The generated text handlers are not edited, because the dispatcher zone already covers the whole handler. |
| The zones go through a stack in the core, and `pnvg_err` closes every open zone before it raises. `mexFunction` also closes any zone that is still open when it starts. | `mexErrMsgIdAndTxt` does not return, so a plain `TracyCZoneEnd` after the handler never runs on an error, and the zones of the thread would nest deeper with every error. |
| `build.m` turns Tracy on with the environment variable `PSYCHNANOVG_TRACY=1`, and passes `-DPSYCHNANOVG_TRACY=ON` or `OFF` to CMake on every build. When Tracy is missing, `build.m` raises `build:tracy` before CMake runs, and CMake stops with `FATAL_ERROR`. Both messages give the clone command. Tracy is not vendored; v0.11.1 is the tested version. | Section 10.2 names the option but not how `build.m` sets it. The explicit OFF stops the CMake cache from keeping an ON from an earlier build. |
| `Stats.endFrameNs` times `nvgEndFrame` only, as in phase 1. The driver can do its command submission in the first GL call after `nvgEndFrame`: the timer query, or the error drain when there is no timer. For 500 gradient segments on Intel Iris Xe that is 1.0 to 1.5 ms, which appears in the `EndFrame` entry of `s.commands` but not in `endFrameNs`. | A finding, not a change. With the GPU timer turned off by hand, the same time moved to the error drain, and the whole frame did not get faster, so the timer adds no cost of its own. The "GPU timer" and "GL restore and error drain" Tracy zones show where the time goes. |
| `tests/gl/test_gl_paths.m` is a fourth GL test file. Section 11.2 names three. `tests/smoke_gl.c` also draws an arc gauge and a gradient segment, checks their pixels, and times 1,000 gradient segments. | The phase 2 paths need a pixel check. The smoke test gives them one under Octave and on the Linux and macOS CI runners, where Psychtoolbox does not run. |

Measured per-segment cost of `StrokeSegments`, 1,000 gradient segments of 2
pixels width on a spiral, Intel Iris Xe, 2026-09-22. The numbers move by a
factor of two between runs on a laptop.

| Measurement | Per segment |
|---|---|
| The call, null renderer, MATLAB R2023a (`perf/PsychNanoVGPerf`) | 265 ns |
| The call, null renderer, Octave 10.1 | 315 ns |
| The same work as a loop of seven subcommands, null renderer, MATLAB / Octave | 26 us / 111 us |
| The call, GL renderer, MATLAB (`PsychNanoVGPerf('gl')`) | 533 ns |
| The same loop, GL renderer, MATLAB | 14.5 us |
| `endFrameNs`, GL renderer, MATLAB | 405 ns |
| GPU time from the timer pair, GL renderer, MATLAB | 6.5 us |
| The call / `EndFrame` / GPU in `smoke_gl`, MSVC build | 240 to 600 ns / 190 to 410 ns / 2.7 to 5.3 us |

The call cost is NanoVG's stroke tessellation, which runs inside
`nvgStroke`. The GPU cost is mostly per draw call: with `NVG_STENCIL_STROKES`
each stroke is three draws. A script that needs thousands of segments per
frame and no overlap correction can turn `stencilStrokes` off in `Init`,
which draws each stroke in one pass.
