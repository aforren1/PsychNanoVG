function test_gl_target()
%TEST_GL_TARGET  Render target to Psychtoolbox texture round trip (SPEC 11.2).
%
%   Draws one shape directly into the window and the same shape into a
%   NanoVG render target that is then wrapped with Screen('SetOpenGLTexture')
%   and drawn with Screen('DrawTexture'). The two images have to match,
%   which is what pins the orientation question of SPEC 6.1.

    if isempty(which('Screen'))
        fprintf('   skipped test_gl_target: no Screen\n');
        return;
    end

    global GL %#ok<GVMIS>
    w = 256;
    h = 256;
    vg = pnvg_gl_open(w, h);
    cleanup = onCleanup(@() pnvg_gl_close(vg));

    % ---- direct ----
    Screen('FillRect', vg.win, 0);
    PsychNanoVGFrame('Begin', vg);
    draw_marker();
    PsychNanoVGFrame('End', vg);
    Screen('Flip', vg.win, 0, 1);
    direct = double(Screen('GetImage', vg.win, [0 0 w h], 'drawBuffer')) / 255;

    % ---- through a render target ----
    Screen('FillRect', vg.win, 0);

    [rt, glTex] = PsychNanoVGGL(vg, 'RenderTargetCreate', w, h);
    tst('ok', 'RenderTargetCreate returned a handle', rt > 0);
    tst('ok', 'RenderTargetCreate returned a GL texture id', glTex > 0);
    tst('ok', 'RenderTargetImage is a live image', ...
        PsychNanoVGGL(vg, 'RenderTargetImage', rt) > 0);

    % A render target needs bind, clear, draw, and unbind in one OpenGL
    % region, because Screen('EndOpenGL') resets the framebuffer binding.
    % PsychNanoVGGL wraps one subcommand, so this sequence opens the region
    % itself. Every call inside could still go through PsychNanoVGGL, which
    % passes through while a region is open.
    Screen('BeginOpenGL', vg.win);
    try
        PsychNanoVG('RenderTargetBind', rt);
        % A new framebuffer texture holds whatever was in that memory, and
        % nvgluCreateFramebuffer does not clear it. The caller clears it.
        glClearColor(0, 0, 0, 1);
        glClear(bitor(GL.COLOR_BUFFER_BIT, GL.STENCIL_BUFFER_BIT));
        PsychNanoVG('BeginFrame', w, h);
        draw_marker();
        PsychNanoVG('EndFrame');
        PsychNanoVG('RenderTargetUnbind');
    catch err
        Screen('EndOpenGL', vg.win);
        rethrow(err);
    end
    Screen('EndOpenGL', vg.win);

    tex = Screen('SetOpenGLTexture', vg.win, [], glTex, GL.TEXTURE_2D, w, h);
    Screen('DrawTexture', vg.win, tex, [], [0 0 w h]);
    Screen('Flip', vg.win, 0, 1);
    viaTarget = double(Screen('GetImage', vg.win, [0 0 w h], 'drawBuffer')) / 255;

    d = abs(direct(:, :, 1) - viaTarget(:, :, 1));
    tst('ok', 'the render target matches direct drawing', mean(d(:)) < 0.02);

    % The marker is asymmetric top to bottom, so a flipped texture fails this.
    topDirect = mean(mean(direct(1 : h / 4, :, 1)));
    topTarget = mean(mean(viaTarget(1 : h / 4, :, 1)));
    tst('near', 'the render target is not flipped in y', topTarget, ...
        topDirect, 0.02);

    PsychNanoVGGL(vg, 'RenderTargetDelete', rt);
    tst('throws', 'a deleted render target is gone', 'psychnanovg:Handle', ...
        @() PsychNanoVGGL(vg, 'RenderTargetImage', rt));

    % ---- binds nest ----
    % A script can render into a target while another one is bound, and the
    % inner unbind has to give the outer one back rather than forget it. The
    % macOS smoke test found this: it binds an offscreen target for the whole
    % run, and the round trip above used to clear the record of it.
    outer = PsychNanoVGGL(vg, 'RenderTargetCreate', 64, 64);
    inner = PsychNanoVGGL(vg, 'RenderTargetCreate', 32, 32);
    Screen('BeginOpenGL', vg.win);
    ok = true;
    try
        PsychNanoVG('RenderTargetBind', outer);
        PsychNanoVG('RenderTargetBind', inner);
        PsychNanoVG('RenderTargetUnbind');   % back to outer
        PsychNanoVG('RenderTargetUnbind');   % back to the window
    catch nestErr
        ok = false;
    end
    Screen('EndOpenGL', vg.win);
    tst('ok', 'a nested bind and unbind pair works', ok);
    if ~ok
        fprintf(2, '        threw %s: %s\n', nestErr.identifier, ...
                nestErr.message);
    end
    tst('throws', 'one unbind too many', 'psychnanovg:FrameState', ...
        @() PsychNanoVGGL(vg, 'RenderTargetUnbind'));

    PsychNanoVGGL(vg, 'RenderTargetBind', outer);
    tst('throws', 'binding the same target twice', 'psychnanovg:FrameState', ...
        @() PsychNanoVGGL(vg, 'RenderTargetBind', outer));
    PsychNanoVGGL(vg, 'RenderTargetUnbind');
    PsychNanoVGGL(vg, 'RenderTargetDelete', outer);
    PsychNanoVGGL(vg, 'RenderTargetDelete', inner);

    [~, isUserspace] = Screen('GetOpenGLDrawMode');
    tst('eq', 'the test left 2D mode behind', isUserspace, 0);
end

function draw_marker()
% Asymmetric on purpose: a bar near the top and a disc near the bottom.
    PsychNanoVG('BeginPath');
    PsychNanoVG('Rect', 32, 24, 192, 24);
    PsychNanoVG('FillColor', [1 1 1 1]);
    PsychNanoVG('Fill');
    PsychNanoVG('BeginPath');
    PsychNanoVG('Circle', 128, 180, 40);
    PsychNanoVG('FillColor', [1 1 1 1]);
    PsychNanoVG('Fill');
end
