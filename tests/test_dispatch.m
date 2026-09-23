function test_dispatch()
%TEST_DISPATCH  Dispatch, lifecycle, handles, frame state, and Stats.
%
%   Runs with the null renderer (SPEC 11.1), so no GPU is needed.

    %% ---------- before Init ----------
    tst('throws', 'drawing before Init', 'psychnanovg:NotInit', ...
        @() PsychNanoVG('BeginPath'));
    tst('throws', 'unknown command', 'psychnanovg:UnknownCommand', ...
        @() PsychNanoVG('NoSuchThing'));
    tst('throws', 'opcode out of range', 'psychnanovg:UnknownCommand', ...
        @() PsychNanoVG(999999));
    tst('throws', 'first argument of the wrong class', 'psychnanovg:Usage', ...
        @() PsychNanoVG({1}));

    %% ---------- Init ----------
    ctx = PsychNanoVG('Init', struct('renderer', 'null'));
    cleanup = onCleanup(@() shutdown_quietly());
    tst('ok', 'Init returns a context handle', ...
        isscalar(ctx) && ctx >= 1 && ctx == fix(ctx));

    % Phase 3: a second Init is a second context, not an error. It becomes
    % the current one, so the first is selected again for the rest of this
    % file. test_contexts covers the rest of the context API.
    ctx2 = PsychNanoVG('Init', struct('renderer', 'null'));
    tst('ok', 'a second Init makes a second context', ctx2 ~= ctx);
    PsychNanoVG('Shutdown', ctx2);
    PsychNanoVG('SetContext', ctx);

    v = PsychNanoVG('Version');
    tst('ok', 'Version is a struct', isstruct(v));
    tst('eq', 'Version backend is null', v.backend, 'null');
    tst('ok', 'Version names the NanoVG commit', ~isempty(v.nanovg));
    tst('ok', 'Version names the binding', ~isempty(v.psychnanovg));

    %% ---------- names, opcodes, help ----------
    op = PsychNanoVG('Opcode', 'LineTo');
    tst('ok', 'Opcode returns a positive integer', op > 0 && op == fix(op));
    tst('eq', 'Opcode agrees with PsychNanoVGOp', op, opfield('LineTo'));
    tst('throws', 'Opcode of an unknown name', 'psychnanovg:UnknownCommand', ...
        @() PsychNanoVG('Opcode', 'Nope'));

    %% ---------- argument counts ----------
    tst('throws', 'too few arguments', 'psychnanovg:Usage', ...
        @() PsychNanoVG('Translate', 1));
    tst('throws', 'too many arguments', 'psychnanovg:Usage', ...
        @() PsychNanoVG('Translate', 1, 2, 3));
    % DegToRad is a pure function, so this checks the type and nothing else.
    tst('throws', 'wrong argument class', 'psychnanovg:Type', ...
        @() PsychNanoVG('DegToRad', 'wide'));
    tst('throws', 'NaN argument', 'psychnanovg:Range', ...
        @() PsychNanoVG('BeginFrame', NaN, 100));

    %% ---------- Enum ----------
    tst('eq', 'Enum with the NVG_ prefix', PsychNanoVG('Enum', 'NVG_CCW'), 1);
    tst('eq', 'Enum without the prefix', PsychNanoVG('Enum', 'ALIGN_CENTER'), 2);
    tst('eq', 'Enum with a joined pair', ...
        PsychNanoVG('Enum', 'ALIGN_CENTER|ALIGN_MIDDLE'), 2 + 16);
    tst('eq', 'Enum reaches the GL create flags', ...
        PsychNanoVG('Enum', 'NVG_STENCIL_STROKES'), 2);
    tst('throws', 'Enum of an unknown name', 'psychnanovg:Range', ...
        @() PsychNanoVG('Enum', 'NVG_NOT_A_THING'));

    % An enum name is accepted wherever an enum argument is (SPEC 7.2).
    PsychNanoVG('BeginFrame', 640, 480);
    PsychNanoVG('TextAlign', 'ALIGN_CENTER|ALIGN_MIDDLE');
    PsychNanoVG('LineCap', 'ROUND');
    PsychNanoVG('EndFrame');

    %% ---------- frame state ----------
    tst('throws', 'a path call outside a frame', 'psychnanovg:FrameState', ...
        @() PsychNanoVG('BeginPath'));
    tst('throws', 'EndFrame without BeginFrame', 'psychnanovg:FrameState', ...
        @() PsychNanoVG('EndFrame'));
    PsychNanoVG('BeginFrame', 640, 480);
    tst('throws', 'nested BeginFrame', 'psychnanovg:FrameState', ...
        @() PsychNanoVG('BeginFrame', 640, 480));
    PsychNanoVG('CancelFrame');
    tst('throws', 'CancelFrame twice', 'psychnanovg:FrameState', ...
        @() PsychNanoVG('CancelFrame'));

    %% ---------- image handles ----------
    img = PsychNanoVG('CreateImageRGBA', 0, zeros(16, 8, 4, 'uint8'));
    tst('ok', 'CreateImageRGBA returns a handle', img > 0);
    % HxWx4 in MATLAB is height by width, so ImageSize reports [w h].
    tst('eq', 'ImageSize is [w h]', PsychNanoVG('ImageSize', img), [8 16]);
    tst('throws', 'a stale image handle', 'psychnanovg:Handle', ...
        @() PsychNanoVG('ImageSize', img + 1000));
    PsychNanoVG('UpdateImage', img, ones(16, 8, 4, 'uint8'));
    tst('throws', 'UpdateImage with the wrong shape', 'psychnanovg:Type', ...
        @() PsychNanoVG('UpdateImage', img, zeros(4, 4, 'uint8')));
    PsychNanoVG('DeleteImage', img);
    tst('throws', 'the handle is dead after DeleteImage', ...
        'psychnanovg:Handle', @() PsychNanoVG('ImageSize', img));

    %% ---------- the image transpose keeps pixels in place ----------
    check_image_transpose();

    %% ---------- paint table ----------
    PsychNanoVG('BeginFrame', 640, 480);
    paints = zeros(1, 256);
    for k = 1:256
        paints(k) = PsychNanoVG('LinearGradient', 0, 0, 1, 1, ...
                                [1 0 0 1], [0 0 1 1]);
    end
    tst('eq', 'the paint table holds 256 entries', numel(unique(paints)), 256);
    tst('throws', 'the paint table runs out', 'psychnanovg:Range', ...
        @() PsychNanoVG('LinearGradient', 0, 0, 1, 1, [1 0 0 1], [0 0 1 1]));
    PsychNanoVG('PaintDelete', paints(1));
    again = PsychNanoVG('LinearGradient', 0, 0, 1, 1, [1 0 0 1], [0 0 1 1]);
    tst('eq', 'PaintDelete returns the slot', again, paints(1));
    tst('throws', 'a stale paint handle', 'psychnanovg:Handle', ...
        @() PsychNanoVG('FillPaint', 9999));
    PsychNanoVG('FillPaint', again);
    for k = 2:256
        PsychNanoVG('PaintDelete', paints(k));
    end
    PsychNanoVG('PaintDelete', again);
    tst('throws', 'PaintDelete twice', 'psychnanovg:Handle', ...
        @() PsychNanoVG('PaintDelete', again));

    %% ---------- font handles ----------
    % fontstash indexes its font array with the handle and does not check it,
    % so an id that was never created has to be caught here (SPEC 8.4).
    tst('throws', 'a font handle that was never created', ...
        'psychnanovg:Handle', @() PsychNanoVG('FontFaceId', 0));
    tst('throws', 'a negative font handle', 'psychnanovg:Handle', ...
        @() PsychNanoVG('AddFallbackFontId', 0, 0));
    tst('eq', 'FindFont reports a missing family as -1', ...
        PsychNanoVG('FindFont', 'no such family'), -1);
    tst('throws', 'ResetFallbackFonts with an unknown family', ...
        'psychnanovg:Handle', ...
        @() PsychNanoVG('ResetFallbackFonts', 'no such family'));

    %% ---------- colors and text measurement ----------
    tst('near', 'RGBAf round trip', PsychNanoVG('RGBAf', 0.25, 0.5, 0.75, 1), ...
        [0.25 0.5 0.75 1], 1e-6);
    tst('near', 'RGB is 0 to 255 in, 0 to 1 out', ...
        PsychNanoVG('RGB', 255, 0, 128), [1 0 128/255 1], 1e-6);
    [asc, desc, lineh] = PsychNanoVG('TextMetrics');
    tst('ok', 'TextMetrics returns three numbers', ...
        isscalar(asc) && isscalar(desc) && isscalar(lineh));
    PsychNanoVG('EndFrame');

    %% ---------- Stats ----------
    s = PsychNanoVG('Stats');
    tst('ok', 'Stats is a struct', isstruct(s));
    tst('ok', 'Stats counts frames', s.frames >= 2);
    tst('ok', 'Stats has a command table', isstruct(s.commands));
    tst('ok', 'Stats counted BeginFrame', ...
        any(strcmp({s.commands.name}, 'BeginFrame')));
    tst('ok', 'endFrameNs is positive', s.endFrameNs > 0);
    PsychNanoVG('Stats', 'reset');
    s2 = PsychNanoVG('Stats');
    tst('eq', 'Stats reset clears the frame count', s2.frames, 0);
    tst('throws', 'Stats takes only reset', 'psychnanovg:Usage', ...
        @() PsychNanoVG('Stats', 'clear'));

    %% ---------- the opcode path reaches the same handler ----------
    PsychNanoVG('BeginFrame', 64, 64);
    PsychNanoVG(opfield('BeginPath'));
    PsychNanoVG(opfield('MoveTo'), 1, 2);
    PsychNanoVG(opfield('LineTo'), 3, 4);
    PsychNanoVG('EndFrame');
    s3 = PsychNanoVG('Stats');
    tst('ok', 'the opcode path is counted like the name path', ...
        any(strcmp({s3.commands.name}, 'LineTo')));
end

% ---------------------------------------------------------------------------

function v = opfield(name)
    op = PsychNanoVGOp();
    v = op.(name);
end

function check_image_transpose()
% The MEX transposes HxWx4 column-major pixels into row-major RGBA. NanoVG's
% null renderer throws the pixels away, so read them back through the only
% path that keeps them: nothing. Instead check the two properties that the
% transpose has to satisfy and that a wrong stride would break: the call
% accepts a non-square image, and it rejects anything that is not HxWx4.
    img = PsychNanoVG('CreateImageRGBA', 0, zeros(3, 7, 4, 'uint8'));
    tst('eq', 'a non-square image keeps its shape', ...
        PsychNanoVG('ImageSize', img), [7 3]);
    PsychNanoVG('DeleteImage', img);
    tst('throws', 'an HxW image is rejected', 'psychnanovg:Type', ...
        @() PsychNanoVG('CreateImageRGBA', 0, zeros(4, 4, 'uint8')));
    tst('throws', 'an HxWx3 image is rejected', 'psychnanovg:Type', ...
        @() PsychNanoVG('CreateImageRGBA', 0, zeros(4, 4, 3, 'uint8')));
    tst('throws', 'a double image is rejected', 'psychnanovg:Type', ...
        @() PsychNanoVG('CreateImageRGBA', 0, zeros(4, 4, 4)));
end

function shutdown_quietly()
    try
        PsychNanoVG('Shutdown');
    catch
    end
end
