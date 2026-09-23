function test_contexts()
%TEST_CONTEXTS  Several contexts, switching, and the MEX lock (phase 3).
%
%   Runs with the null renderer (SPEC 11.1), so no GPU is needed. Every
%   null context has no GL context of its own, so the GL context check is
%   not reached here; tests/gl/test_gl_contexts and tests/smoke_gl.c cover
%   it.

    cleanup = onCleanup(@() shutdown_all()); %#ok<NASGU>
    nullr = struct('renderer', 'null');

    %% ---------- no context ----------
    v = PsychNanoVG('Version');
    tst('eq', 'no context is current at the start', v.context, 0);
    tst('eq', 'no context is open at the start', v.contexts, zeros(1, 0));
    tst('eq', 'SetContext reports no current context', ...
        PsychNanoVG('SetContext'), 0);
    lockedAtStart = is_locked();
    if ~isnan(lockedAtStart)
        tst('ok', 'the MEX file is unlocked with no context', ~lockedAtStart);
    end
    tst('throws', 'Shutdown with no context', 'psychnanovg:NotInit', ...
        @() PsychNanoVG('Shutdown'));

    %% ---------- two contexts ----------
    a = PsychNanoVG('Init', nullr);
    b = PsychNanoVG('Init', nullr);
    tst('ok', 'two Inits give two handles', a ~= b && a >= 1 && b >= 1);
    tst('eq', 'the last Init is current', PsychNanoVG('SetContext'), b);
    v = PsychNanoVG('Version');
    tst('eq', 'Version lists both contexts', v.contexts, [a b]);
    tst('eq', 'Version names the current context', v.context, b);
    if ~isnan(lockedAtStart)
        tst('ok', 'the MEX file is locked while a context is open', is_locked());
    end

    %% ---------- switching ----------
    tst('eq', 'SetContext returns the context it replaced', ...
        PsychNanoVG('SetContext', a), b);
    tst('eq', 'SetContext switched', PsychNanoVG('SetContext'), a);
    tst('throws', 'SetContext with a handle that was never made', ...
        'psychnanovg:Handle', @() PsychNanoVG('SetContext', 999));
    tst('throws', 'SetContext with a text handle', 'psychnanovg:Type', ...
        @() PsychNanoVG('SetContext', 'a'));
    tst('eq', 'a refused SetContext changes nothing', ...
        PsychNanoVG('SetContext'), a);

    %% ---------- frame state is per context ----------
    PsychNanoVG('BeginFrame', 64, 64);
    PsychNanoVG('SetContext', b);
    tst('throws', 'a frame in one context is not a frame in the other', ...
        'psychnanovg:FrameState', @() PsychNanoVG('BeginPath'));
    PsychNanoVG('BeginFrame', 32, 32);
    PsychNanoVG('BeginPath');
    PsychNanoVG('Rect', 0, 0, 10, 10);
    PsychNanoVG('Fill');
    PsychNanoVG('EndFrame');
    PsychNanoVG('SetContext', a);
    PsychNanoVG('BeginPath');
    PsychNanoVG('Circle', 10, 10, 5);
    PsychNanoVG('Fill');
    ok_call('the first context ends the frame it began before the switch', ...
            @() PsychNanoVG('EndFrame'));

    %% ---------- Stats are per context ----------
    PsychNanoVG('Stats', 'reset');
    for k = 1:3
        PsychNanoVG('BeginFrame', 64, 64);
        PsychNanoVG('EndFrame');
    end
    PsychNanoVG('SetContext', b);
    PsychNanoVG('Stats', 'reset');
    PsychNanoVG('BeginFrame', 64, 64);
    PsychNanoVG('BeginPath');
    PsychNanoVG('EndFrame');
    sB = PsychNanoVG('Stats');
    PsychNanoVG('SetContext', a);
    sA = PsychNanoVG('Stats');
    tst('eq', 'Stats of the first context counts its frames', sA.frames, 3);
    tst('eq', 'Stats of the second context counts its frames', sB.frames, 1);
    tst('ok', 'the command counts are per context', ...
        any(strcmp({sB.commands.name}, 'BeginPath')) && ...
        ~any(strcmp({sA.commands.name}, 'BeginPath')));
    tst('ok', 'gpuNs is NaN for a null context', isnan(sA.gpuNs));
    PsychNanoVG('SetContext', b);
    PsychNanoVG('Stats', 'reset');
    PsychNanoVG('SetContext', a);
    sA = PsychNanoVG('Stats');
    tst('eq', 'a reset in one context keeps the other', sA.frames, 3);

    %% ---------- images, paints, and fonts are per context ----------
    img = PsychNanoVG('CreateImageRGBA', 0, zeros(4, 4, 4, 'uint8'));
    PsychNanoVG('SetContext', b);
    tst('throws', 'an image of one context is not open in the other', ...
        'psychnanovg:Handle', @() PsychNanoVG('ImageSize', img));
    PsychNanoVG('SetContext', a);
    tst('eq', 'the image is open in its own context', ...
        PsychNanoVG('ImageSize', img), [4 4]);

    PsychNanoVG('BeginFrame', 64, 64);
    paints = zeros(1, 256);
    for k = 1:256
        paints(k) = PsychNanoVG('LinearGradient', 0, 0, 1, 1, ...
                                [1 0 0 1], [0 0 1 1]);
    end
    PsychNanoVG('SetContext', b);
    p = PsychNanoVG('LinearGradient', 0, 0, 1, 1, [1 0 0 1], [0 0 1 1]);
    tst('eq', 'a full paint table in one context leaves the other empty', ...
        p, 1);
    PsychNanoVG('PaintDelete', p);
    PsychNanoVG('SetContext', a);
    tst('throws', 'the full table is still full', 'psychnanovg:Range', ...
        @() PsychNanoVG('LinearGradient', 0, 0, 1, 1, [1 0 0 1], [0 0 1 1]));
    for k = 1:256
        PsychNanoVG('PaintDelete', paints(k));
    end
    PsychNanoVG('EndFrame');

    fontFile = PsychNanoVGFonts('FindSystemFont', 'Arial');
    if isempty(fontFile)
        fontFile = PsychNanoVGFonts('FindSystemFont', 'DejaVuSans');
    end
    if ~isempty(fontFile)
        f = PsychNanoVG('CreateFont', 'sans', fontFile);
        PsychNanoVG('BeginFrame', 64, 64);
        PsychNanoVG('FontFaceId', f);
        PsychNanoVG('EndFrame');
        PsychNanoVG('SetContext', b);
        PsychNanoVG('BeginFrame', 64, 64);
        tst('throws', 'a font of one context is not open in the other', ...
            'psychnanovg:Handle', @() PsychNanoVG('FontFaceId', f));
        PsychNanoVG('CancelFrame');
        PsychNanoVG('SetContext', a);
    else
        fprintf('   note: no system font found, the font check is skipped\n');
    end

    %% ---------- Shutdown ----------
    % Of the context that is not current: the current one stays current.
    PsychNanoVG('Shutdown', b);
    tst('eq', 'Shutdown of the other context keeps the current one', ...
        PsychNanoVG('SetContext'), a);
    tst('throws', 'a stale handle in SetContext', 'psychnanovg:Handle', ...
        @() PsychNanoVG('SetContext', b));
    tst('throws', 'a stale handle in Shutdown', 'psychnanovg:Handle', ...
        @() PsychNanoVG('Shutdown', b));
    c = PsychNanoVG('Init', nullr);
    tst('ok', 'a handle is never reused', c > b);
    PsychNanoVG('SetContext', a);

    % Of the current context: none is current, and a call says what to do.
    PsychNanoVG('Shutdown');
    tst('eq', 'Shutdown of the current context leaves none current', ...
        PsychNanoVG('SetContext'), 0);
    tst('throws', 'a call with no current context', 'psychnanovg:NotInit', ...
        @() PsychNanoVG('BeginFrame', 64, 64));
    tst('throws', 'Shutdown without a handle and with none current', ...
        'psychnanovg:NotInit', @() PsychNanoVG('Shutdown'));
    if ~isnan(lockedAtStart)
        tst('ok', 'the MEX file stays locked while a context is open', ...
            is_locked());
    end
    tst('throws', 'Shutdown with a word other than all', 'psychnanovg:Usage', ...
        @() PsychNanoVG('Shutdown', 'every'));

    % The last one unlocks.
    PsychNanoVG('Shutdown', c);
    v = PsychNanoVG('Version');
    tst('eq', 'no context is left', v.contexts, zeros(1, 0));
    if ~isnan(lockedAtStart)
        tst('ok', 'the MEX file unlocks when the last context goes', ...
            ~is_locked());
    end

    %% ---------- the table has a bound; all ----------
    made = [];
    err = '';
    try
        for k = 1:17
            made(end + 1) = PsychNanoVG('Init', nullr); %#ok<AGROW>
        end
    catch e
        err = e.identifier;
    end
    tst('eq', 'sixteen contexts fit', numel(made), 16);
    tst('eq', 'the seventeenth is refused', err, 'psychnanovg:Range');
    PsychNanoVG('Shutdown', 'all');
    v = PsychNanoVG('Version');
    tst('eq', 'Shutdown all leaves no context', v.contexts, zeros(1, 0));
    tst('eq', 'and none current', v.context, 0);
    if ~isnan(lockedAtStart)
        tst('ok', 'Shutdown all unlocks the MEX file', ~is_locked());
    end
    ok_call('Shutdown all with no context is safe', ...
            @() PsychNanoVG('Shutdown', 'all'));
end

% ---------------------------------------------------------------------------

function tf = is_locked()
% mislocked answers for a MEX file in MATLAB and in Octave. NaN means this
% engine cannot say, and the lock checks are then skipped.
    try
        tf = double(mislocked('PsychNanoVG'));
    catch
        tf = NaN;
    end
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

function shutdown_all()
    try
        PsychNanoVG('Shutdown', 'all');
    catch
    end
end
