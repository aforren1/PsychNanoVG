function test_gl_contexts()
%TEST_GL_CONTEXTS  Two Psychtoolbox windows with a context each (phase 3).
%
%   Psychtoolbox gives each onscreen window its own userspace OpenGL
%   context, and the two share no objects. The test draws into both
%   windows, gives each context a render target, and checks that each
%   window shows its own pixels. It then checks that a context refuses the
%   GL context of the other window, and that a Shutdown with the wrong GL
%   context current makes no GL call.
%
%   When the display cannot open a second window, both contexts go into one
%   window. The pixel checks then look at two places in that window, and the
%   checks that need a second GL context are skipped.

    if isempty(which('Screen'))
        fprintf('   skipped test_gl_contexts: no Screen\n');
        return;
    end

    global GL %#ok<GVMIS>
    w = 128;
    h = 128;
    winA = ptb_test_window(w, h, [], 0, [0 0]);
    two = true;
    try
        winB = ptb_test_window(w, h, [], 0, [w + 40, 0]);
    catch openErr
        two = false;
        winB = winA;
        fprintf(['   note: a second window did not open (%s); both ' ...
                 'contexts use one window\n'], openErr.message);
    end
    try
        vgA = PsychNanoVGOpen(winA);
        vgB = PsychNanoVGOpen(winB);
    catch err
        sca;
        rethrow(err);
    end
    holder = struct('a', vgA, 'b', vgB);
    cleanup = onCleanup(@() close_all(holder)); %#ok<NASGU>

    tst('ok', 'two windows give two contexts', vgA.ctx ~= vgB.ctx);
    v = PsychNanoVG('Version');
    tst('ok', 'Version lists both', all(ismember([vgA.ctx vgB.ctx], v.contexts)));

    %% ---------- direct drawing, one shape per context ----------
    % Context A draws a red square on the left and B a green one on the
    % right. With two windows each window holds only its own square.
    Screen('FillRect', winA, 0);
    if two
        Screen('FillRect', winB, 0);
    end
    PsychNanoVGFrame('Begin', vgA);
    square(16, 48, [1 0 0 1]);
    PsychNanoVGFrame('End', vgA);
    PsychNanoVGFrame('Begin', vgB);
    square(80, 48, [0 1 0 1]);
    PsychNanoVGFrame('End', vgB);
    pa = grab(winA, w, h);
    tst('ok', 'context A drew its red square', is_color(pa, 32, 64, [1 0 0]));
    if two
        pb = grab(winB, w, h);
        tst('ok', 'context B drew its green square', ...
            is_color(pb, 96, 64, [0 1 0]));
        tst('ok', 'the square of B is not in window A', ...
            is_color(pa, 96, 64, [0 0 0]));
        tst('ok', 'the square of A is not in window B', ...
            is_color(pb, 32, 64, [0 0 0]));
    else
        tst('ok', 'context B drew its green square', ...
            is_color(pa, 96, 64, [0 1 0]));
    end

    %% ---------- a render target per context ----------
    [rtA, texA] = PsychNanoVGGL(vgA, 'RenderTargetCreate', 32, 32);
    [rtB, texB] = PsychNanoVGGL(vgB, 'RenderTargetCreate', 32, 32);
    tst('ok', 'each context made a render target', rtA > 0 && rtB > 0);
    fill_target(vgA, rtA, [0 0 1 1]);
    fill_target(vgB, rtB, [1 1 0 1]);
    tA = Screen('SetOpenGLTexture', winA, [], texA, GL.TEXTURE_2D, 32, 32);
    tB = Screen('SetOpenGLTexture', winB, [], texB, GL.TEXTURE_2D, 32, 32);
    Screen('FillRect', winA, 0);
    if two
        Screen('FillRect', winB, 0);
    end
    Screen('DrawTexture', winA, tA, [], [16 48 48 80]);
    Screen('DrawTexture', winB, tB, [], [80 48 112 80]);
    pa = grab(winA, w, h);
    tst('ok', 'the target of context A is blue in its window', ...
        is_color(pa, 32, 64, [0 0 1]));
    if two
        pb = grab(winB, w, h);
        tst('ok', 'the target of context B is yellow in its window', ...
            is_color(pb, 96, 64, [1 1 0]));
    else
        tst('ok', 'the target of context B is yellow', ...
            is_color(pa, 96, 64, [1 1 0]));
    end
    Screen('Close', [tA tB]);
    PsychNanoVGGL(vgA, 'RenderTargetDelete', rtA);
    PsychNanoVGGL(vgB, 'RenderTargetDelete', rtB);

    %% ---------- Stats per context ----------
    PsychNanoVG('SetContext', vgA.ctx);
    sA = PsychNanoVG('Stats');
    PsychNanoVG('SetContext', vgB.ctx);
    sB = PsychNanoVG('Stats');
    tst('eq', 'Stats of A counts its two frames', sA.frames, 2);
    tst('eq', 'Stats of B counts its two frames', sB.frames, 2);

    if ~two
        fprintf('   note: one window, so the GL context checks are skipped\n');
        return;
    end

    %% ---------- the GL context check ----------
    % The context of A with the GL context of B current: the script forgot
    % SetContext, or opened the region of the wrong window.
    PsychNanoVG('SetContext', vgA.ctx);
    Screen('BeginOpenGL', winB);
    tst('throws', 'BeginFrame in the GL context of another window', ...
        'psychnanovg:Context', @() PsychNanoVG('BeginFrame', w, h));
    tst('throws', 'CreateImageRGBA in the GL context of another window', ...
        'psychnanovg:Context', ...
        @() PsychNanoVG('CreateImageRGBA', 0, zeros(4, 4, 4, 'uint8')));
    % EndOpenGL aborts on a pending GL error, so reaching the next line
    % shows that the refusals made no GL call in the context of B.
    Screen('EndOpenGL', winB);
    tst('ok', 'the refusals left no GL error behind', true);
    ok = true;
    try
        PsychNanoVGFrame('Begin', vgA);
        PsychNanoVGFrame('End', vgA);
    catch frameErr
        ok = false;
        fprintf(2, '        threw %s: %s\n', frameErr.identifier, ...
                frameErr.message);
    end
    tst('ok', 'the right region makes the same context work again', ok);

    %% ---------- Shutdown of the context that is not current ----------
    PsychNanoVG('SetContext', vgB.ctx);
    lastwarn('');
    PsychNanoVGClose(vgA);
    [~, warnId] = lastwarn();
    tst('ok', 'Close in its own window gives no warning', isempty(warnId));
    tst('eq', 'Close of A keeps B current', PsychNanoVG('SetContext'), vgB.ctx);

    % A Shutdown with the GL context of B current must not delete the
    % objects of context A there: the names would mean objects of B.
    % The test shuts vgA2 down itself; the cleanup only needs the windows.
    vgA2 = PsychNanoVGOpen(winA);
    PsychNanoVG('SetContext', vgB.ctx);
    Screen('BeginOpenGL', winB);
    lastwarn('');
    fprintf('   (the next warning is expected)\n');
    PsychNanoVG('Shutdown', vgA2.ctx);
    [~, warnId] = lastwarn();
    Screen('EndOpenGL', winB);
    tst('eq', 'Shutdown from the wrong GL context warns', warnId, ...
        'psychnanovg:NoGLContext');
    tst('eq', 'and keeps B current', PsychNanoVG('SetContext'), vgB.ctx);
    ok = true;
    try
        PsychNanoVGFrame('Begin', vgB);
        square(80, 48, [0 1 0 1]);
        PsychNanoVGFrame('End', vgB);
    catch frameErr
        ok = false;
        fprintf(2, '        threw %s: %s\n', frameErr.identifier, ...
                frameErr.message);
    end
    tst('ok', 'B still draws after that Shutdown', ok);
    pb = grab(winB, w, h);
    tst('ok', 'B drew its square after that Shutdown', ...
        is_color(pb, 96, 64, [0 1 0]));

    [~, isUserspace] = Screen('GetOpenGLDrawMode');
    tst('eq', 'the test left 2D mode behind', isUserspace, 0);
end

% ---------------------------------------------------------------------------

function square(x, y, rgba)
    PsychNanoVG('BeginPath');
    PsychNanoVG('Rect', x, y, 32, 32);
    PsychNanoVG('FillColor', rgba);
    PsychNanoVG('Fill');
end

function fill_target(vg, rt, rgba)
% Bind, clear, draw, and unbind share one region, because
% Screen('EndOpenGL') resets the framebuffer binding.
    global GL %#ok<GVMIS>
    Screen('BeginOpenGL', vg.win);
    try
        PsychNanoVG('SetContext', vg.ctx);
        PsychNanoVG('RenderTargetBind', rt);
        glClearColor(0, 0, 0, 1);
        glClear(bitor(GL.COLOR_BUFFER_BIT, GL.STENCIL_BUFFER_BIT));
        PsychNanoVG('BeginFrame', 32, 32);
        PsychNanoVG('BeginPath');
        PsychNanoVG('Rect', 0, 0, 32, 32);
        PsychNanoVG('FillColor', rgba);
        PsychNanoVG('Fill');
        PsychNanoVG('EndFrame');
        PsychNanoVG('RenderTargetUnbind');
    catch err
        Screen('EndOpenGL', vg.win);
        rethrow(err);
    end
    Screen('EndOpenGL', vg.win);
end

function img = grab(win, w, h)
    Screen('Flip', win, 0, 1);
    img = double(Screen('GetImage', win, [0 0 w h], 'drawBuffer')) / 255;
end

function tf = is_color(img, x, y, rgb)
    px = squeeze(img(y, x, 1:3))';
    tf = all(abs(px - rgb) < 0.1);
end

function close_all(holder)
    try
        PsychNanoVGClose(holder.a);
    catch
    end
    try
        PsychNanoVGClose(holder.b);
    catch
    end
    sca;
end
