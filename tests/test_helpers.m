function test_helpers()
%TEST_HELPERS  The convenience layer, against a Screen stub and the null renderer.
%
%   PsychNanoVGOpen, PsychNanoVGFrame, PsychNanoVGGL, and PsychNanoVGClose
%   exist so that a script never writes a Screen('BeginOpenGL') and
%   Screen('EndOpenGL') pair. What has to be checked is the bracketing: the
%   region opens once, closes once, and closes again on an error. None of
%   that needs a GPU, so the tests put a recording Screen stub on the path
%   and run the real MEX with the null renderer.

    % run_tests puts tests/stub on the path before it loads the MEX file, and
    % leaves it there. This test therefore changes nothing about the path:
    % rewriting the load path while a MEX file is loaded made Octave 10 on
    % Linux crash. See SPEC section 14.
    if exist('pnvg_stub_reset', 'file') == 0
        error('psychnanovg:Usage', ...
              ['test_helpers needs tests/stub on the path. Run it through ' ...
               'run_tests, which puts it there.']);
    end

    global PNVG_SCREEN_STUB %#ok<GVMIS>

    %% ---------- Open refuses a window without 3D graphics ----------
    PNVG_SCREEN_STUB = pnvg_stub_reset();
    PNVG_SCREEN_STUB.enable3d = 0;
    tst('throws', 'Open without 3D graphics', 'psychnanovg:No3DGraphics', ...
        @() PsychNanoVGOpen(10, struct('renderer', 'null')));
    tst('ok', 'the refusal happens before BeginOpenGL', ...
        ~any(strcmp(PNVG_SCREEN_STUB.log, 'BeginOpenGL')));

    %% ---------- Open ----------
    PNVG_SCREEN_STUB = pnvg_stub_reset();
    vg = PsychNanoVGOpen(10, struct('renderer', 'null'));
    closer = onCleanup(@() shutdown_quietly()); %#ok<NASGU>

    tst('eq', 'Open reports the window', vg.win, 10);
    tst('eq', 'Open reports the rect', vg.rect, [0 0 640 480]);
    tst('ok', 'Open reports that the context is live', vg.opened);
    tst('ok', 'Open returns a fonts struct', isstruct(vg.fonts));
    tst('ok', 'Open leaves 2D mode behind', PNVG_SCREEN_STUB.drawMode == 0);
    tst('ok', 'Open opened and closed exactly one region', ...
        count_calls('BeginOpenGL') == 1 && count_calls('EndOpenGL') == 1);
    % The default font is best effort: this machine has one, but a bare
    % container may not, so only the shape is required.
    if isfield(vg.fonts, 'sans')
        tst('ok', 'the default font has a handle', vg.fonts.sans >= 0);
        tst('ok', 'the default font names its file', ...
            ischar(vg.fonts.sansFile) && ~isempty(vg.fonts.sansFile));
    else
        fprintf('   note: no system font found, the default font is skipped\n');
    end

    tst('throws', 'Open twice', 'psychnanovg:AlreadyInit', ...
        @() PsychNanoVGOpen(10, struct('renderer', 'null')));

    %% ---------- Frame ----------
    PNVG_SCREEN_STUB.log = {};
    PsychNanoVG('Stats', 'reset');
    PsychNanoVGFrame('Begin', vg);
    tst('ok', 'Frame Begin leaves userspace rendering active', ...
        PNVG_SCREEN_STUB.drawMode == 1);
    PsychNanoVG('BeginPath');
    PsychNanoVG('Circle', 100, 100, 50);
    PsychNanoVG('FillColor', [1 1 1 1]);
    PsychNanoVG('Fill');
    PsychNanoVGFrame('End', vg);
    tst('ok', 'Frame End goes back to 2D mode', PNVG_SCREEN_STUB.drawMode == 0);
    tst('eq', 'Frame is one region', ...
        [count_calls('BeginOpenGL'), count_calls('EndOpenGL')], [1 1]);
    s = PsychNanoVG('Stats');
    tst('eq', 'Frame drew one frame', s.frames, 1);

    % The default size comes from the window rect.
    PsychNanoVGFrame('Begin', vg);
    PsychNanoVG('BeginPath');
    PsychNanoVG('Rect', 0, 0, 10, 10);
    PsychNanoVGFrame('End', vg);
    % An explicit size is for an offscreen target.
    PsychNanoVGFrame('Begin', vg, 320, 240);
    PsychNanoVGFrame('End', vg);
    s = PsychNanoVG('Stats');
    tst('eq', 'three frames in all', s.frames, 3);

    tst('throws', 'Frame with an unknown operation', ...
        'psychnanovg:UnknownCommand', @() PsychNanoVGFrame('Middle', vg));
    tst('throws', 'Frame without the struct', 'psychnanovg:Type', ...
        @() PsychNanoVGFrame('Begin', 10));
    tst('ok', 'a rejected Frame call leaves 2D mode behind', ...
        PNVG_SCREEN_STUB.drawMode == 0);

    % A failure inside Begin must not leave the region open.
    PNVG_SCREEN_STUB.log = {};
    tst('throws', 'Frame Begin with a bad size', 'psychnanovg:Range', ...
        @() PsychNanoVGFrame('Begin', vg, 0, 0));
    tst('ok', 'a failed Frame Begin closes the region', ...
        PNVG_SCREEN_STUB.drawMode == 0 && count_calls('EndOpenGL') == 1);

    tst('throws', 'Frame End outside a frame', 'psychnanovg:FrameState', ...
        @() PsychNanoVGFrame('End', vg));
    tst('ok', 'a failed Frame End closes the region', ...
        PNVG_SCREEN_STUB.drawMode == 0);

    %% ---------- GL ----------
    PNVG_SCREEN_STUB.log = {};
    img = PsychNanoVGGL(vg, 'CreateImageRGBA', 0, zeros(8, 8, 4, 'uint8'));
    tst('ok', 'GL returns the handle of the wrapped call', img > 0);
    tst('eq', 'GL wraps one region', ...
        [count_calls('BeginOpenGL'), count_calls('EndOpenGL')], [1 1]);
    tst('ok', 'GL leaves 2D mode behind', PNVG_SCREEN_STUB.drawMode == 0);

    % Two outputs pass through.
    PNVG_SCREEN_STUB.log = {};
    sz = PsychNanoVGGL(vg, 'ImageSize', img);
    tst('eq', 'GL passes an output through', sz, [8 8]);

    % No output asked for, and no output produced.
    PsychNanoVGGL(vg, 'DeleteImage', img);
    tst('throws', 'the image is gone after the wrapped DeleteImage', ...
        'psychnanovg:Handle', @() PsychNanoVGGL(vg, 'ImageSize', img));

    % Inside a region the call must not nest a second one.
    PsychNanoVGFrame('Begin', vg);
    PNVG_SCREEN_STUB.log = {};
    img2 = PsychNanoVGGL(vg, 'CreateImageRGBA', 0, zeros(4, 4, 4, 'uint8'));
    tst('ok', 'GL inside a frame opens no second region', ...
        count_calls('BeginOpenGL') == 0 && count_calls('EndOpenGL') == 0);
    tst('ok', 'GL inside a frame still works', img2 > 0);
    PsychNanoVGFrame('End', vg);

    % An error inside the wrapped region must leave 2D mode behind.
    PNVG_SCREEN_STUB.log = {};
    tst('throws', 'GL with a bad argument', 'psychnanovg:Type', ...
        @() PsychNanoVGGL(vg, 'CreateImageRGBA', 0, 5));
    tst('ok', 'a failed GL call closes the region', ...
        PNVG_SCREEN_STUB.drawMode == 0 && count_calls('EndOpenGL') == 1);

    tst('throws', 'GL without the struct', 'psychnanovg:Type', ...
        @() PsychNanoVGGL(10, 'BeginPath'));

    %% ---------- Close ----------
    PNVG_SCREEN_STUB.log = {};
    PsychNanoVGClose(vg);
    tst('eq', 'Close is one region', ...
        [count_calls('BeginOpenGL'), count_calls('EndOpenGL')], [1 1]);
    tst('ok', 'Close leaves 2D mode behind', PNVG_SCREEN_STUB.drawMode == 0);
    tst('throws', 'the context is gone after Close', 'psychnanovg:NotInit', ...
        @() PsychNanoVG('BeginPath'));

    % Twice is safe.
    ok_call('Close twice', @() PsychNanoVGClose(vg));

    %% ---------- Close after the window is gone ----------
    PNVG_SCREEN_STUB = pnvg_stub_reset();
    vg2 = PsychNanoVGOpen(11, struct('renderer', 'null'));
    PNVG_SCREEN_STUB.failBegin = true;   % the window has been closed
    PNVG_SCREEN_STUB.log = {};
    ok_call('Close with the window already closed', @() PsychNanoVGClose(vg2));
    tst('ok', 'Close shut the context down anyway', ...
        PNVG_SCREEN_STUB.drawMode == 0);
    tst('throws', 'the context is gone after that Close too', ...
        'psychnanovg:NotInit', @() PsychNanoVG('BeginPath'));
end

% ---------------------------------------------------------------------------

function n = count_calls(name)
    global PNVG_SCREEN_STUB %#ok<GVMIS>
    n = sum(strcmp(PNVG_SCREEN_STUB.log, name));
end

function ok_call(name, fn)
    try
        fn();
        tst('ok', name, true);
    catch e
        tst('ok', name, false);
        fprintf(2, '        threw %s: %s\n', e.identifier, e.message);
    end
end

function shutdown_quietly()
    try
        PsychNanoVG('Shutdown');
    catch
    end
end
