function img = PsychNanoVGDemo(screenid, duration, opts)
%PSYCHNANOVGDEMO  Vector graphics inside a Psychtoolbox window.
%
%   PsychNanoVGDemo                      highest screen, six seconds
%   PsychNanoVGDemo(screenid)
%   PsychNanoVGDemo(screenid, duration)  duration in seconds
%   img = PsychNanoVGDemo(screenid, duration, opts)
%
%   Draws a ring whose edge is a radial gradient rather than an antialiasing
%   fringe, a Bezier trajectory traced over time, centered text placed with
%   TextBounds, the same ring cached in a render target and drawn 100 times
%   through Screen('DrawTexture'), a gauge built from arcs in one Path call,
%   a wave stroked with one color per vertex, and the eyes of the upstream
%   NanoVG demo, which follow the mouse pointer.
%
%   Press any key to stop early, on a machine where PsychHID loads.
%
%   `opts` is for a reproducible frame, such as the README screenshot that
%   tools/CaptureReadmeScreenshot.m makes. All fields are optional:
%
%     opts.size      [w h] of a window instead of the full screen
%     opts.frames    draw this many frames instead of running for `duration`
%     opts.phase     a fixed animation phase from 0 to 1
%     opts.pointer   a fixed [x y] for the eyes to look at, instead of the
%                    mouse
%     opts.time      a fixed time in seconds for the blink of the eyes
%
%   When `img` is asked for, the demo reads the last frame back with
%   Screen('GetImage') and returns it as an HxWx3 uint8 array.
%
%   Colors are 0 to 1 throughout, for Screen as well as for NanoVG, because
%   the demo calls PsychDefaultSetup(2) before it opens the window.
%
%   The demo shows the call pattern of SPEC 4.3: PsychNanoVGOpen once,
%   PsychNanoVGFrame around the drawing of each frame, PsychNanoVGGL around
%   a setup call that touches OpenGL, and PsychNanoVGClose at the end. No
%   Screen('BeginOpenGL') pair appears except around the render target,
%   which needs several subcommands in one region.
%
%   See also PsychNanoVGOpen, PsychNanoVGFrame, PsychNanoVGGL,
%   PsychNanoVGClose, PsychNanoVGTwoWindowDemo.

    if isempty(which('Screen'))
        error('psychnanovg:Usage', ...
              'PsychNanoVGDemo needs Psychtoolbox. Screen is not on the path.');
    end
    if nargin < 1 || isempty(screenid)
        screenid = max(Screen('Screens'));
    end
    if nargin < 2 || isempty(duration)
        duration = 6;
    end
    if nargin < 3 || isempty(opts)
        opts = struct();
    end
    img = [];

    global GL %#ok<GVMIS>
    PsychNanoVGSetup();
    % The window comes from the same helper as the tests, so the preferences
    % that keep an unattended run quiet are set in one place.
    addpath(fullfile(fileparts(fileparts(mfilename('fullpath'))), ...
                     'tests', 'gl'));
    oldSync = Screen('Preference', 'SkipSyncTests');

    % Every Psychtoolbox demo starts here. Feature level 2 asks PsychImaging
    % for the normalized 0 to 1 color range, so the Screen colors below read
    % the same way as the NanoVG colors, which are always 0 to 1. It also
    % runs AssertOpenGL and unifies the key names. It has to come before the
    % window opens, because PsychImaging reads the color mode at that point.
    PsychDefaultSetup(2);

    % The background of the upstream NanoVG demo, which the soft shadows of
    % the eyes were drawn for.
    bg = [0.3 0.3 0.32];

    vg = [];
    try
        if isfield(opts, 'size')
            [win, rect] = ptb_test_window(opts.size(1), opts.size(2), ...
                                          screenid, bg);
        else
            [win, rect] = ptb_test_window([], [], screenid, bg);
        end
        w = RectWidth(rect);
        h = RectHeight(rect);
        cx = w / 2;
        cy = h / 2;
        ifi = Screen('GetFlipInterval', win);

        % ---- setup, once ----
        vg = PsychNanoVGOpen(win);
        font = -1;
        if isfield(vg.fonts, 'sans')
            font = vg.fonts.sans;
        end

        % The ring never changes, so it is drawn once into a render target
        % and then costs one textured quad per frame instead of a fill.
        cacheSize = 256;
        [rt, glTex] = PsychNanoVGGL(vg, 'RenderTargetCreate', ...
                                    cacheSize, cacheSize);
        fill_render_target(vg, rt, cacheSize);
        cacheTex = Screen('SetOpenGLTexture', win, [], glTex, ...
                          GL.TEXTURE_2D, cacheSize, cacheSize);

        % A machine without a working PsychHID cannot poll the keyboard.
        % That is no reason to refuse to draw, so the demo then runs for its
        % full duration instead. A fixed frame count never polls.
        fixedFrames = isfield(opts, 'frames');
        useKb = ~fixedFrames;
        if useKb
            try
                KbReleaseWait();
            catch
                useKb = false;
                fprintf(['PsychHID does not load here, so a key press ' ...
                         'cannot stop the demo early.\n']);
            end
        end
        useMouse = ~isfield(opts, 'pointer');

        t0 = GetSecs();
        vbl = Screen('Flip', win);
        frame = 0;

        while true
            if fixedFrames
                if frame >= opts.frames
                    break;
                end
            elseif GetSecs() - t0 >= duration || (useKb && KbCheck())
                break;
            end
            frame = frame + 1;
            t = GetSecs() - t0;
            if isfield(opts, 'phase')
                phase = opts.phase;
            else
                phase = t / duration;
            end
            if isfield(opts, 'time')
                t = opts.time;
            end
            [mx, my] = pointer(win, useMouse, opts, w, h, t);

            % ---- Screen draws first ----
            Screen('FillRect', win, bg);
            Screen('DrawText', win, 'Screen and NanoVG in one frame', ...
                   20, 20, [0.75 0.75 0.75]);

            % ---- NanoVG ----
            PsychNanoVGFrame('Begin', vg);
            draw_ring(cx, cy, 140, 24);
            draw_trajectory(cx, cy, phase);
            draw_gauge(w - 170, 170, 110, phase);
            draw_wave(w, h, phase);
            draw_eyes(40, 70, 240, 160, mx, my, t);
            if font >= 0
                PsychNanoVG('FontFaceId', font);
                PsychNanoVG('FontSize', 32);
                % The bounds of the ink, measured at the origin, put the
                % middle of the word on the middle of the ring.
                [~, bounds] = PsychNanoVG('TextBounds', 0, 0, 'fixate');
                PsychNanoVG('FillColor', [1 1 1 0.9]);
                PsychNanoVG('Text', cx - (bounds(1) + bounds(3)) / 2, ...
                            cy - (bounds(2) + bounds(4)) / 2, 'fixate');
            end
            PsychNanoVGFrame('End', vg);

            % ---- the cached ring, 100 copies, at no NanoVG cost ----
            % NanoVG renders the target with premultiplied alpha. Screen's
            % default blend function would draw its clear corners as black
            % squares.
            [oldSrc, oldDst] = Screen('BlendFunction', win, 'GL_ONE', ...
                                      'GL_ONE_MINUS_SRC_ALPHA');
            for k = 1:100
                a = 2 * pi * k / 100 + phase * 2 * pi;
                dst = CenterRectOnPoint([0 0 24 24], ...
                                        cx + 300 * cos(a), cy + 200 * sin(a));
                Screen('DrawTexture', win, cacheTex, [], dst);
            end
            Screen('BlendFunction', win, oldSrc, oldDst);

            lastFrame = fixedFrames && frame == opts.frames;
            if nargout > 0 && lastFrame
                % dontclear keeps the frame in the draw buffer after the
                % flip, which is where GetImage reads it back.
                Screen('Flip', win, 0, 1);
                img = Screen('GetImage', win, [], 'drawBuffer');
            else
                vbl = Screen('Flip', win, vbl + 0.5 * ifi);
            end
        end

        s = PsychNanoVG('Stats');
        fprintf('EndFrame: mean %.2f ms, max %.2f ms over %d frames\n', ...
                s.endFrameSumNs / max(s.frames, 1) / 1e6, ...
                s.endFrameMaxNs / 1e6, s.frames);

        PsychNanoVGGL(vg, 'RenderTargetDelete', rt);
        PsychNanoVGClose(vg);
    catch err
        if ~isempty(vg)
            PsychNanoVGClose(vg);
        end
        sca;
        Screen('Preference', 'SkipSyncTests', oldSync);
        rethrow(err);
    end

    sca;
    Screen('Preference', 'SkipSyncTests', oldSync);
end

% ---------------------------------------------------------------------------

function fill_render_target(vg, rt, sz)
% Bind, clear, draw, and unbind have to share one OpenGL region, because
% Screen('EndOpenGL') resets the framebuffer binding. PsychNanoVGGL wraps
% one subcommand, so this is the one place that opens a region by hand.
    global GL %#ok<GVMIS>
    Screen('BeginOpenGL', vg.win);
    try
        % The raw region does not select the context the way the helpers
        % do, so it names it.
        PsychNanoVG('SetContext', vg.ctx);
        PsychNanoVG('RenderTargetBind', rt);
        % A new framebuffer texture is not cleared for you.
        glClearColor(0, 0, 0, 0);
        glClear(bitor(GL.COLOR_BUFFER_BIT, GL.STENCIL_BUFFER_BIT));
        PsychNanoVG('BeginFrame', sz, sz);
        draw_ring(sz / 2, sz / 2, 100, 16);
        PsychNanoVG('EndFrame');
        PsychNanoVG('RenderTargetUnbind');
    catch err
        Screen('EndOpenGL', vg.win);
        rethrow(err);
    end
    Screen('EndOpenGL', vg.win);
end

function draw_ring(cx, cy, r, edge)
% A gradient ring rather than a stroked circle: SPEC 6.2 warns that the
% one-pixel antialiasing fringe is not a controlled edge profile, so a
% stimulus that needs a known luminance ramp draws the ramp itself.
    inner = r - edge;
    paint = PsychNanoVG('RadialGradient', cx, cy, inner, r, ...
                        [1 1 1 1], [1 1 1 0]);
    PsychNanoVG('BeginPath');
    PsychNanoVG('Circle', cx, cy, r);
    PsychNanoVG('Circle', cx, cy, inner - edge);
    PsychNanoVG('PathWinding', 'NVG_HOLE');
    PsychNanoVG('FillPaint', paint);
    PsychNanoVG('Fill');
    PsychNanoVG('PaintDelete', paint);
end

function draw_trajectory(cx, cy, phase)
% The matrix form of Path: one MEX call for the whole curve.
    n = 64;
    t = linspace(0, phase, n)';
    x = cx + 260 * sin(2 * pi * t) .* cos(pi * t);
    y = cy + 180 * sin(4 * pi * t);
    cmds = [2 * ones(n, 1), x, y, zeros(n, 4)];
    cmds(1, 1) = 1;   % 1 = MoveTo, 2 = LineTo
    PsychNanoVG('BeginPath');
    PsychNanoVG('Path', cmds);
    PsychNanoVG('StrokeWidth', 3);
    PsychNanoVG('LineCap', 'ROUND');
    PsychNanoVG('LineJoin', 'ROUND');
    PsychNanoVG('StrokeColor', [0.3 0.8 1 0.9]);
    PsychNanoVG('Stroke');
end

function draw_gauge(gx, gy, r, value)
% A 270 degree gauge. The track, the value band, and the needle hub are one
% Path matrix each: Arc rows (code 6) and a Circle row (code 9), so no arc
% costs a MEX call of its own. y points down, so the start angle of 3/4 pi
% is at the lower left and the sweep runs clockwise over the top.
    a0 = 0.75 * pi;
    a1 = 2.25 * pi;
    av = a0 + (a1 - a0) * value;
    band = 16;
    ro = r;
    ri = r - band;
    track = [6 gx gy ro a0 a1 2;
             6 gx gy ri a1 a0 1;
             5 0 0 0 0 0 0];
    PsychNanoVG('BeginPath');
    PsychNanoVG('Path', track);
    PsychNanoVG('FillColor', [0.2 0.2 0.22 1]);
    PsychNanoVG('Fill');

    if av > a0
        level = [6 gx gy ro a0 av 2;
                 6 gx gy ri av a0 1;
                 5 0 0 0 0 0 0];
        paint = PsychNanoVG('LinearGradient', gx - r, gy, gx + r, gy, ...
                            [0.2 0.8 0.3 1], [1 0.3 0.2 1]);
        PsychNanoVG('BeginPath');
        PsychNanoVG('Path', level);
        PsychNanoVG('FillPaint', paint);
        PsychNanoVG('Fill');
        PsychNanoVG('PaintDelete', paint);
    end

    hub = [1 gx gy 0 0 0 0;
           2 gx + (ri - 8) * cos(av), gy + (ri - 8) * sin(av), 0 0 0 0];
    PsychNanoVG('BeginPath');
    PsychNanoVG('Path', hub);
    PsychNanoVG('StrokeWidth', 4);
    PsychNanoVG('LineCap', 'ROUND');
    PsychNanoVG('StrokeColor', [1 1 1 1]);
    PsychNanoVG('Stroke');
    PsychNanoVG('BeginPath');
    PsychNanoVG('Path', [9 gx gy 8 0 0 0]);
    PsychNanoVG('FillColor', [1 1 1 1]);
    PsychNanoVG('Fill');
end

function draw_wave(w, h, phase)
% A wave with a color per vertex: the hue runs along x and moves with time.
% PsychNanoVGPolylineGradient sends every segment and both of its colors in
% one StrokeSegments call. Round caps hide the joins between segments.
    n = 120;
    x = linspace(60, w - 60, n)';
    y = h - 90 + 40 * sin(linspace(0, 6 * pi, n)' + 2 * pi * phase);
    hue = mod(linspace(0, 1, n)' + phase, 1);
    rgba = [hue2rgb(hue), ones(n, 1)];
    PsychNanoVG('StrokeWidth', 6);
    PsychNanoVG('LineCap', 'ROUND');
    PsychNanoVGPolylineGradient([x, y], rgba);
end

function [mx, my] = pointer(win, useMouse, opts, w, h, t)
% The eyes follow the mouse. Without one, or for a fixed frame, they follow
% opts.pointer or a slow figure eight.
    if ~useMouse
        mx = opts.pointer(1);
        my = opts.pointer(2);
        return;
    end
    try
        [mx, my] = GetMouse(win);
    catch
        mx = w / 2 + 0.4 * w * sin(0.7 * t);
        my = h / 2 + 0.3 * h * sin(1.4 * t);
    end
end

function draw_eyes(x, y, w, h, mx, my, t)
% The eyes of the NanoVG example (drawEyes in example/demo.c), with the same
% paints. Each shape of two eyes that share one paint is one Path matrix of
% Ellipse rows (code 8), so the pair costs one MEX call and one fill. One
% change: upstream aims both pupils from the right eye; here each pupil aims
% from its own eye.
    ex = w * 0.23;
    ey = h * 0.5;
    lx = x + ex;
    ly = y + ey;
    rx = x + w - ex;
    ry = y + ey;
    br = min(ex, ey) * 0.5;
    blink = 1 - sin(t * 0.5) ^ 200 * 0.8;

    % Drop shadow, offset down and right.
    bg = PsychNanoVG('LinearGradient', x, y + h * 0.5, x + w * 0.1, y + h, ...
                     [0 0 0 32 / 255], [0 0 0 16 / 255]);
    PsychNanoVG('BeginPath');
    PsychNanoVG('Path', [8 lx + 3 ly + 16 ex ey 0 0; ...
                         8 rx + 3 ry + 16 ex ey 0 0]);
    PsychNanoVG('FillPaint', bg);
    PsychNanoVG('Fill');
    PsychNanoVG('PaintDelete', bg);

    % The whites.
    bg = PsychNanoVG('LinearGradient', x, y + h * 0.25, x + w * 0.1, y + h, ...
                     [220 220 220 255] / 255, [128 128 128 255] / 255);
    PsychNanoVG('BeginPath');
    PsychNanoVG('Path', [8 lx ly ex ey 0 0; 8 rx ry ex ey 0 0]);
    PsychNanoVG('FillPaint', bg);
    PsychNanoVG('Fill');
    PsychNanoVG('PaintDelete', bg);

    % The pupils, one fill for both. Each looks toward the pointer, and
    % stops at the edge of its eye.
    [ldx, ldy] = gaze(mx - lx, my - ly, ex, ey);
    [rdx, rdy] = gaze(mx - rx, my - ry, ex, ey);
    lift = ey * 0.25 * (1 - blink);
    PsychNanoVG('BeginPath');
    PsychNanoVG('Path', [8 lx + ldx ly + ldy + lift br br * blink 0 0; ...
                         8 rx + rdx ry + rdy + lift br br * blink 0 0]);
    PsychNanoVG('FillColor', [32 32 32 255] / 255);
    PsychNanoVG('Fill');

    % The highlights. The radial gradient has its own center per eye, so
    % these are two paints and two fills.
    for c = [lx ly; rx ry]'
        gloss = PsychNanoVG('RadialGradient', c(1) - ex * 0.25, ...
                            c(2) - ey * 0.5, ex * 0.1, ex * 0.75, ...
                            [1 1 1 128 / 255], [1 1 1 0]);
        PsychNanoVG('BeginPath');
        PsychNanoVG('Path', [8 c(1) c(2) ex ey 0 0]);
        PsychNanoVG('FillPaint', gloss);
        PsychNanoVG('Fill');
        PsychNanoVG('PaintDelete', gloss);
    end
end

function [dx, dy] = gaze(vx, vy, ex, ey)
    dx = vx / (ex * 10);
    dy = vy / (ey * 10);
    d = sqrt(dx * dx + dy * dy);
    if d > 1
        dx = dx / d;
        dy = dy / d;
    end
    dx = dx * ex * 0.4;
    dy = dy * ey * 0.5;
end

function rgb = hue2rgb(hue)
% Fully saturated colors around the hue circle, 0 to 1.
    k = mod(hue * 6 + [5 3 1], 6);
    rgb = 1 - max(0, min(1, min(k, 4 - k)));
end
