function PsychNanoVGTwoWindowDemo(screenid, duration)
%PSYCHNANOVGTWOWINDOWDEMO  One NanoVG context per Psychtoolbox window.
%
%   PsychNanoVGTwoWindowDemo                      highest screen, six seconds
%   PsychNanoVGTwoWindowDemo(screenid)
%   PsychNanoVGTwoWindowDemo(screenid, duration)  duration in seconds
%
%   Opens two small windows side by side on one screen and gives each one
%   its own NanoVG context with PsychNanoVGOpen. The left window shows a
%   gauge built from arcs, the right one a wave with one color per vertex.
%   Both flip together.
%
%   Psychtoolbox gives every onscreen window its own OpenGL context, and
%   the fonts, images, and render targets of one context do not exist in
%   the other. The helpers therefore take the struct of the window they act
%   on, and each struct carries its context:
%
%       vgA = PsychNanoVGOpen(winA);
%       vgB = PsychNanoVGOpen(winB);
%       PsychNanoVGFrame('Begin', vgA);  ...  PsychNanoVGFrame('End', vgA);
%       PsychNanoVGFrame('Begin', vgB);  ...  PsychNanoVGFrame('End', vgB);
%
%   When the display does not open a second window, the demo falls back to
%   one window with two contexts. The second context draws into a render
%   target, and Screen('DrawTexture') puts that target into the window.
%
%   Press any key to stop early, on a machine where PsychHID loads.
%
%   See also PsychNanoVGDemo, PsychNanoVGOpen, PsychNanoVGFrame.

    if isempty(which('Screen'))
        error('psychnanovg:Usage', ...
              'PsychNanoVGTwoWindowDemo needs Psychtoolbox. Screen is not on the path.');
    end
    if nargin < 1 || isempty(screenid)
        screenid = max(Screen('Screens'));
    end
    if nargin < 2 || isempty(duration)
        duration = 6;
    end

    global GL %#ok<GVMIS>
    PsychNanoVGSetup();
    % m/private/psychnanovg_demo_window sets the preferences that keep an
    % unattended run quiet: SkipSyncTests 2 and VisualDebugLevel 0. The demo
    % does not change the path; see that file.
    oldSync = Screen('Preference', 'SkipSyncTests');
    % Normalized 0 to 1 colors for Screen, the same range as NanoVG. It has
    % to come before the windows open.
    PsychDefaultSetup(2);

    w = 480;
    h = 360;
    gap = 40;
    vgA = [];
    vgB = [];
    try
        winA = psychnanovg_demo_window(w, h, screenid, 0.15, [40 60]);
        twoWindows = true;
        try
            winB = psychnanovg_demo_window(w, h, screenid, 0.1, [40 + w + gap, 60]);
        catch openErr
            twoWindows = false;
            winB = winA;
            fprintf(['A second window did not open (%s).\n' ...
                     'Both contexts draw into one window instead.\n'], ...
                    openErr.message);
        end

        vgA = PsychNanoVGOpen(winA);
        vgB = PsychNanoVGOpen(winB);
        fprintf('Context %d draws the gauge, context %d the wave.\n', ...
                vgA.ctx, vgB.ctx);

        % In one window, the second context draws half size into a render
        % target, which the first window's frame then shows on its right.
        rt = 0;
        cacheTex = 0;
        if ~twoWindows
            [rt, glTex] = PsychNanoVGGL(vgB, 'RenderTargetCreate', w / 2, h);
            cacheTex = Screen('SetOpenGLTexture', winA, [], glTex, ...
                              GL.TEXTURE_2D, w / 2, h);
        end

        useKb = true;
        try
            KbReleaseWait();
        catch
            useKb = false;
            fprintf(['PsychHID does not load here, so a key press ' ...
                     'cannot stop the demo early.\n']);
        end

        t0 = GetSecs();
        while GetSecs() - t0 < duration && ~(useKb && KbCheck())
            phase = mod((GetSecs() - t0) / 4, 1);

            if twoWindows
                Screen('FillRect', winA, 0.15);
                PsychNanoVGFrame('Begin', vgA);
                draw_gauge(w / 2, h / 2 + 20, 130, phase);
                label(vgA, sprintf('window %d, context %d', winA, vgA.ctx), w);
                PsychNanoVGFrame('End', vgA);

                Screen('FillRect', winB, 0.1);
                PsychNanoVGFrame('Begin', vgB);
                draw_wave(w, h, phase);
                label(vgB, sprintf('window %d, context %d', winB, vgB.ctx), w);
                PsychNanoVGFrame('End', vgB);

                % multiflip: one call flips every onscreen window.
                Screen('Flip', winA, [], [], [], 1);
            else
                fill_target(vgB, rt, w / 2, h, phase);
                Screen('FillRect', winA, 0.15);
                PsychNanoVGFrame('Begin', vgA);
                draw_gauge(w / 4, h / 2 + 20, 90, phase);
                label(vgA, sprintf('context %d, and context %d on the right', ...
                                   vgA.ctx, vgB.ctx), w);
                PsychNanoVGFrame('End', vgA);
                Screen('DrawTexture', winA, cacheTex, [], [w / 2 0 w h]);
                Screen('Flip', winA);
            end
        end

        report(vgA, 'gauge');
        report(vgB, 'wave');
        if rt > 0
            PsychNanoVGGL(vgB, 'RenderTargetDelete', rt);
        end
        PsychNanoVGClose(vgB);
        PsychNanoVGClose(vgA);
    catch err
        if ~isempty(vgB)
            PsychNanoVGClose(vgB);
        end
        if ~isempty(vgA)
            PsychNanoVGClose(vgA);
        end
        sca;
        Screen('Preference', 'SkipSyncTests', oldSync);
        rethrow(err);
    end

    sca;
    Screen('Preference', 'SkipSyncTests', oldSync);
end

% ---------------------------------------------------------------------------

function report(vg, what)
    PsychNanoVG('SetContext', vg.ctx);
    s = PsychNanoVG('Stats');
    fprintf('Context %d (%s): %d frames, EndFrame mean %.3f ms\n', vg.ctx, ...
            what, s.frames, s.endFrameSumNs / max(s.frames, 1) / 1e6);
end

function label(vg, txt, w)
% The font belongs to the context that loaded it, so each window uses the
% sans font of its own struct.
    if ~isfield(vg.fonts, 'sans')
        return;
    end
    PsychNanoVG('FontFaceId', vg.fonts.sans);
    PsychNanoVG('FontSize', 20);
    PsychNanoVG('TextAlign', 'ALIGN_CENTER|ALIGN_TOP');
    PsychNanoVG('FillColor', [1 1 1 0.9]);
    PsychNanoVG('Text', w / 2, 16, txt);
end

function fill_target(vg, rt, tw, th, phase)
% Bind, clear, draw, and unbind share one OpenGL region, because
% Screen('EndOpenGL') resets the framebuffer binding.
    global GL %#ok<GVMIS>
    Screen('BeginOpenGL', vg.win);
    try
        PsychNanoVG('SetContext', vg.ctx);
        PsychNanoVG('RenderTargetBind', rt);
        glClearColor(0.1, 0.1, 0.1, 1);
        glClear(bitor(GL.COLOR_BUFFER_BIT, GL.STENCIL_BUFFER_BIT));
        PsychNanoVG('BeginFrame', tw, th);
        draw_wave(tw, th, phase);
        PsychNanoVG('EndFrame');
        PsychNanoVG('RenderTargetUnbind');
    catch err
        Screen('EndOpenGL', vg.win);
        rethrow(err);
    end
    Screen('EndOpenGL', vg.win);
end

function draw_gauge(gx, gy, r, value)
% A 270 degree band from two Arc rows of one Path matrix. y points down, so
% the sweep from 3/4 pi runs clockwise over the top.
    a0 = 0.75 * pi;
    a1 = 2.25 * pi;
    av = a0 + (a1 - a0) * value;
    ri = r - 18;
    PsychNanoVG('BeginPath');
    PsychNanoVG('Path', [6 gx gy r a0 a1 2; 6 gx gy ri a1 a0 1; 5 0 0 0 0 0 0]);
    PsychNanoVG('FillColor', [0.3 0.3 0.3 1]);
    PsychNanoVG('Fill');
    if av > a0
        paint = PsychNanoVG('LinearGradient', gx - r, gy, gx + r, gy, ...
                            [0.2 0.8 0.3 1], [1 0.3 0.2 1]);
        PsychNanoVG('BeginPath');
        PsychNanoVG('Path', [6 gx gy r a0 av 2; 6 gx gy ri av a0 1; ...
                             5 0 0 0 0 0 0]);
        PsychNanoVG('FillPaint', paint);
        PsychNanoVG('Fill');
        PsychNanoVG('PaintDelete', paint);
    end
end

function draw_wave(w, h, phase)
% One color per vertex, sent in one StrokeSegments call.
    n = 90;
    x = linspace(30, w - 30, n)';
    y = h / 2 + 20 + (h / 5) * sin(linspace(0, 4 * pi, n)' + 2 * pi * phase);
    hue = mod(linspace(0, 1, n)' + phase, 1);
    k = mod(hue * 6 + [5 3 1], 6);
    rgb = 1 - max(0, min(1, min(k, 4 - k)));
    PsychNanoVG('StrokeWidth', 6);
    PsychNanoVG('LineCap', 'ROUND');
    PsychNanoVGPolylineGradient([x, y], [rgb, ones(n, 1)]);
end
