function test_transforms()
%TEST_TRANSFORMS  Transform helpers against matrices worked out by hand.
%
%   NanoVG stores a 2x3 affine transform as [a b c d e f], which maps
%   (x, y) to (a*x + c*y + e, b*x + d*y + f). Every expected value below is
%   written out from that definition rather than from another PsychNanoVG
%   call, so a change of convention shows up here.

    PsychNanoVG('Init', struct('renderer', 'null'));
    cleanup = onCleanup(@() shutdown_quietly());

    tol = 1e-6;

    %% ---------- constructors ----------
    tst('near', 'TransformIdentity', PsychNanoVG('TransformIdentity'), ...
        [1 0 0 1 0 0], tol);
    tst('near', 'TransformTranslate', ...
        PsychNanoVG('TransformTranslate', 3, 4), [1 0 0 1 3 4], tol);
    tst('near', 'TransformScale', PsychNanoVG('TransformScale', 2, 3), ...
        [2 0 0 3 0 0], tol);
    a = pi / 3;
    tst('near', 'TransformRotate', PsychNanoVG('TransformRotate', a), ...
        [cos(a) sin(a) -sin(a) cos(a) 0 0], tol);
    tst('near', 'TransformSkewX', PsychNanoVG('TransformSkewX', a), ...
        [1 0 tan(a) 1 0 0], tol);
    tst('near', 'TransformSkewY', PsychNanoVG('TransformSkewY', a), ...
        [1 tan(a) 0 1 0 0], tol);

    %% ---------- angle helpers ----------
    tst('near', 'DegToRad', PsychNanoVG('DegToRad', 180), pi, 1e-5);
    tst('near', 'RadToDeg', PsychNanoVG('RadToDeg', pi), 180, 1e-4);

    %% ---------- multiply ----------
    t = PsychNanoVG('TransformScale', 2, 3);
    s = PsychNanoVG('TransformTranslate', 10, 20);
    tst('near', 'TransformMultiply matches the reference', ...
        PsychNanoVG('TransformMultiply', t, s), xmul(t, s), tol);
    tst('near', 'TransformPremultiply is Multiply with the operands swapped', ...
        PsychNanoVG('TransformPremultiply', t, s), xmul(s, t), tol);
    tst('near', 'identity is the multiply unit', ...
        PsychNanoVG('TransformMultiply', [1 0 0 1 0 0], s), s, tol);

    %% ---------- inverse ----------
    [inv1, ok1] = PsychNanoVG('TransformInverse', ...
                              PsychNanoVG('TransformTranslate', 3, 4));
    tst('eq', 'TransformInverse reports success', ok1, 1);
    tst('near', 'the inverse of a translation', inv1, [1 0 0 1 -3 -4], tol);

    m = xmul(PsychNanoVG('TransformScale', 2, 4), ...
             PsychNanoVG('TransformTranslate', 5, 6));
    [inv2, ok2] = PsychNanoVG('TransformInverse', m);
    tst('eq', 'TransformInverse of a composite reports success', ok2, 1);
    tst('near', 'a transform times its inverse is the identity', ...
        xmul(m, inv2), [1 0 0 1 0 0], tol);

    [~, ok3] = PsychNanoVG('TransformInverse', [0 0 0 0 0 0]);
    tst('eq', 'a singular transform reports failure', ok3, 0);

    %% ---------- points ----------
    tst('near', 'TransformPoint under a translation', ...
        PsychNanoVG('TransformPoint', [1 0 0 1 3 4], 1, 2), [4 6], tol);
    tst('near', 'TransformPoint under a scale', ...
        PsychNanoVG('TransformPoint', [2 0 0 3 0 0], 1, 2), [2 6], tol);
    r = PsychNanoVG('TransformRotate', pi / 2);
    tst('near', 'TransformPoint under a quarter turn', ...
        PsychNanoVG('TransformPoint', r, 1, 0), [0 1], tol);

    %% ---------- argument checks ----------
    tst('throws', 'a transform must have six elements', 'psychnanovg:Type', ...
        @() PsychNanoVG('TransformPoint', [1 0 0 1], 1, 2));
    tst('throws', 'a transform must be numeric', 'psychnanovg:Type', ...
        @() PsychNanoVG('TransformPoint', 'abc', 1, 2));

    %% ---------- the context transform ----------
    PsychNanoVG('BeginFrame', 640, 480);
    PsychNanoVG('ResetTransform');
    tst('near', 'CurrentTransform starts at the identity', ...
        PsychNanoVG('CurrentTransform'), [1 0 0 1 0 0], tol);
    PsychNanoVG('Translate', 5, 7);
    tst('near', 'Translate shows up in CurrentTransform', ...
        PsychNanoVG('CurrentTransform'), [1 0 0 1 5 7], tol);
    PsychNanoVG('Save');
    PsychNanoVG('Scale', 2, 2);
    tst('near', 'Scale composes onto the translation', ...
        PsychNanoVG('CurrentTransform'), ...
        xmul([2 0 0 2 0 0], [1 0 0 1 5 7]), tol);
    PsychNanoVG('Restore');
    tst('near', 'Restore puts the transform back', ...
        PsychNanoVG('CurrentTransform'), [1 0 0 1 5 7], tol);
    PsychNanoVG('EndFrame');
end

% ---------------------------------------------------------------------------

function t = xmul(t, s)
% The reference for nvgTransformMultiply(t, s): t becomes t * s.
    t0 = t(1) * s(1) + t(2) * s(3);
    t2 = t(3) * s(1) + t(4) * s(3);
    t4 = t(5) * s(1) + t(6) * s(3) + s(5);
    t1 = t(1) * s(2) + t(2) * s(4);
    t3 = t(3) * s(2) + t(4) * s(4);
    t5 = t(5) * s(2) + t(6) * s(4) + s(6);
    t = [t0 t1 t2 t3 t4 t5];
end

function shutdown_quietly()
    try
        PsychNanoVG('Shutdown');
    catch
    end
end
