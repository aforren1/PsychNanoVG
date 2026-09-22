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
    [win, ~] = pnvg_gl_open(w, h);
    cleanup = onCleanup(@() pnvg_gl_close(win)); %#ok<NASGU>

    % ---- direct ----
    Screen('FillRect', win, 0);
    Screen('BeginOpenGL', win);
    PsychNanoVG('BeginFrame', w, h);
    draw_marker();
    PsychNanoVG('EndFrame');
    Screen('EndOpenGL', win);
    Screen('Flip', win, 0, 1);
    direct = double(Screen('GetImage', win, [0 0 w h], 'drawBuffer')) / 255;

    % ---- through a render target ----
    Screen('FillRect', win, 0);
    Screen('BeginOpenGL', win);
    [rt, glTex] = PsychNanoVG('RenderTargetCreate', w, h);
    tst('ok', 'RenderTargetCreate returned a handle', rt > 0);
    tst('ok', 'RenderTargetCreate returned a GL texture id', glTex > 0);
    tst('ok', 'RenderTargetImage is a live image', ...
        PsychNanoVG('RenderTargetImage', rt) > 0);

    PsychNanoVG('RenderTargetBind', rt);
    % A new framebuffer texture holds whatever was in that memory, and
    % nvgluCreateFramebuffer does not clear it. The caller clears it.
    glClearColor(0, 0, 0, 1);
    glClear(bitor(GL.COLOR_BUFFER_BIT, GL.STENCIL_BUFFER_BIT));
    PsychNanoVG('BeginFrame', w, h);
    draw_marker();
    PsychNanoVG('EndFrame');
    PsychNanoVG('RenderTargetUnbind');
    Screen('EndOpenGL', win);

    tex = Screen('SetOpenGLTexture', win, [], glTex, GL.TEXTURE_2D, w, h);
    Screen('DrawTexture', win, tex, [], [0 0 w h]);
    Screen('Flip', win, 0, 1);
    viaTarget = double(Screen('GetImage', win, [0 0 w h], 'drawBuffer')) / 255;

    d = abs(direct(:, :, 1) - viaTarget(:, :, 1));
    tst('ok', 'the render target matches direct drawing', mean(d(:)) < 0.02);

    % The marker is asymmetric top to bottom, so a flipped texture fails this.
    topDirect = mean(mean(direct(1 : h / 4, :, 1)));
    topTarget = mean(mean(viaTarget(1 : h / 4, :, 1)));
    tst('near', 'the render target is not flipped in y', topTarget, ...
        topDirect, 0.02);

    Screen('BeginOpenGL', win);
    PsychNanoVG('RenderTargetDelete', rt);
    tst('throws', 'a deleted render target is gone', 'psychnanovg:Handle', ...
        @() PsychNanoVG('RenderTargetImage', rt));
    Screen('EndOpenGL', win);
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
