function PsychNanoVGDemo(screenid)
%PSYCHNANOVGDEMO  Vector graphics inside a Psychtoolbox window.
%
%   PsychNanoVGDemo            uses the highest numbered screen
%   PsychNanoVGDemo(screenid)
%
%   Draws, for about six seconds:
%     a ring whose edge is a radial gradient rather than an antialiasing
%     fringe, a Bezier trajectory traced over time, centered text placed
%     with TextBounds, and the same ring cached in a render target and drawn
%     100 times through Screen('DrawTexture').
%
%   Press any key to stop early.
%
%   The demo shows the call order of SPEC 4.3: every PsychNanoVG call sits
%   between Screen('BeginOpenGL') and Screen('EndOpenGL'), and Screen keeps
%   drawing before and after.

    if isempty(which('Screen'))
        error('psychnanovg:Usage', ...
              'PsychNanoVGDemo needs Psychtoolbox. Screen is not on the path.');
    end
    if nargin < 1
        screenid = max(Screen('Screens'));
    end

    global GL %#ok<GVMIS>
    PsychNanoVGSetup();
    % The window comes from the same helper as the tests, so the preferences
    % that keep an unattended run quiet are set in one place.
    addpath(fullfile(fileparts(fileparts(mfilename('fullpath'))), ...
                     'tests', 'gl'));
    oldSync = Screen('Preference', 'SkipSyncTests');

    win = [];
    try
        [win, rect] = ptb_test_window([], [], screenid, 0.15);
        w = RectWidth(rect);
        h = RectHeight(rect);
        cx = w / 2;
        cy = h / 2;
        ifi = Screen('GetFlipInterval', win);

        % ---- setup, once, inside a GL context ----
        Screen('BeginOpenGL', win);
        PsychNanoVG('Init');
        fontPath = PsychNanoVG('FindSystemFont', 'Arial');
        font = -1;
        if ~isempty(fontPath)
            font = PsychNanoVG('CreateFont', 'sans', fontPath);
        end
        % The ring never changes, so it is drawn once into a render target
        % and then costs one textured quad per frame instead of a fill.
        cacheSize = 256;
        [rt, glTex] = PsychNanoVG('RenderTargetCreate', cacheSize, cacheSize);
        PsychNanoVG('RenderTargetBind', rt);
        % A new framebuffer texture is not cleared for you.
        glClearColor(0, 0, 0, 0);
        glClear(bitor(GL.COLOR_BUFFER_BIT, GL.STENCIL_BUFFER_BIT));
        PsychNanoVG('BeginFrame', cacheSize, cacheSize);
        draw_ring(cacheSize / 2, cacheSize / 2, 100, 16);
        PsychNanoVG('EndFrame');
        PsychNanoVG('RenderTargetUnbind');
        Screen('EndOpenGL', win);

        cacheTex = Screen('SetOpenGLTexture', win, [], glTex, ...
                          GL.TEXTURE_2D, cacheSize, cacheSize);

        KbReleaseWait();
        t0 = GetSecs();
        vbl = Screen('Flip', win);

        while GetSecs() - t0 < 6 && ~KbCheck()
            phase = (GetSecs() - t0) / 6;

            % ---- Screen draws first ----
            Screen('FillRect', win, 0.15);
            Screen('DrawText', win, 'Screen and NanoVG in one frame', ...
                   20, 20, [0.6 0.6 0.6]);

            % ---- NanoVG ----
            Screen('BeginOpenGL', win);
            PsychNanoVG('BeginFrame', w, h);

            draw_ring(cx, cy, 140, 24);
            draw_trajectory(cx, cy, phase);

            if font >= 0
                PsychNanoVG('FontFaceId', font);
                PsychNanoVG('FontSize', 32);
                [~, bounds] = PsychNanoVG('TextBounds', 0, 0, 'fixate');
                tw = bounds(3) - bounds(1);
                PsychNanoVG('FillColor', [1 1 1 0.9]);
                PsychNanoVG('Text', cx - tw / 2, cy + 220, 'fixate');
            end

            PsychNanoVG('EndFrame');
            Screen('EndOpenGL', win);

            % ---- the cached ring, 100 copies, at no NanoVG cost ----
            for k = 1:100
                a = 2 * pi * k / 100 + phase * 2 * pi;
                dst = CenterRectOnPoint([0 0 24 24], ...
                                        cx + 300 * cos(a), cy + 200 * sin(a));
                Screen('DrawTexture', win, cacheTex, [], dst);
            end

            vbl = Screen('Flip', win, vbl + 0.5 * ifi);
        end

        s = PsychNanoVG('Stats');
        fprintf('EndFrame: mean %.2f ms, max %.2f ms over %d frames\n', ...
                s.endFrameSumNs / max(s.frames, 1) / 1e6, ...
                s.endFrameMaxNs / 1e6, s.frames);

        Screen('BeginOpenGL', win);
        PsychNanoVG('RenderTargetDelete', rt);
        PsychNanoVG('Shutdown');
        Screen('EndOpenGL', win);
    catch err
        if ~isempty(win)
            try
                Screen('BeginOpenGL', win);
                PsychNanoVG('Shutdown');
                Screen('EndOpenGL', win);
            catch
            end
        end
        sca;
        Screen('Preference', 'SkipSyncTests', oldSync);
        rethrow(err);
    end

    sca;
    Screen('Preference', 'SkipSyncTests', oldSync);
end

% ---------------------------------------------------------------------------

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
    if n < 2
        return;
    end
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
