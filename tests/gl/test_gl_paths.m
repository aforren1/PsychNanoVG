function test_gl_paths()
%TEST_GL_PATHS  Arcs in the Path matrix form and a gradient polyline, read back.
%
%   Needs Psychtoolbox and a GPU. run_tests skips the tests/gl directory
%   when Screen does not answer.
%
%   The arc test fills half an annulus, a gauge, that one Path call builds
%   from two Arc rows, and checks the pixels on the arc, in its hole, and on
%   the side where no arc was drawn. The gradient test strokes a three
%   vertex polyline, red to green to blue, through
%   PsychNanoVGPolylineGradient and checks the color at both ends and in
%   the middle.

    if isempty(which('Screen'))
        fprintf('   skipped test_gl_paths: no Screen\n');
        return;
    end

    w = 640;
    h = 480;
    vg = pnvg_gl_open(w, h);
    cleanup = onCleanup(@() pnvg_gl_close(vg));

    cx = 200;
    cy = 220;
    ro = 120;
    ri = 80;

    Screen('FillRect', vg.win, 0);

    PsychNanoVGFrame('Begin', vg);
    % y points down, so the angles from -pi to 0 run over the top half.
    gauge = [6 cx cy ro -pi 0 2;      % outer arc, clockwise
             6 cx cy ri 0 -pi 1;      % inner arc back, counterclockwise
             5 0 0 0 0 0 0;
             9 480 120 40 0 0 0;      % a circle with a hole in it
             9 480 120 20 0 0 0;
             12 2 0 0 0 0 0];
    PsychNanoVG('BeginPath');
    PsychNanoVG('Path', gauge);
    PsychNanoVG('FillColor', [1 1 1 1]);
    PsychNanoVG('Fill');

    xy = [100 400; 320 400; 540 400];
    rgba = [1 0 0 1; 0 1 0 1; 0 0 1 1];
    PsychNanoVG('StrokeWidth', 12);
    PsychNanoVG('LineCap', 'ROUND');
    [seg, col] = PsychNanoVGPolylineGradient(xy, rgba);
    PsychNanoVGFrame('End', vg);

    Screen('Flip', vg.win, 0, 1);
    img = double(Screen('GetImage', vg.win, [0 0 w h], 'drawBuffer')) / 255;

    %% ---------- the gauge ----------
    rm = (ro + ri) / 2;
    top = img(round(cy - rm), cx, 1);
    left = img(cy - 10, round(cx - rm), 1);
    bottom = img(round(cy + rm), cx, 1);
    hole = img(cy - 30, cx, 1);
    tst('ok', 'the arc band is white at the top', top > 0.95);
    tst('ok', 'the arc band reaches the left end', left > 0.95);
    tst('ok', 'no arc below the center', bottom < 0.05);
    tst('ok', 'the inner arc leaves a hole', hole < 0.05);

    tst('ok', 'the circle row fills its ring', img(120, 480 + 30, 1) > 0.95);
    tst('ok', 'the winding row makes a hole', img(120, 480, 1) < 0.05);

    %% ---------- the gradient polyline ----------
    tst('eq', 'the helper gave two segments', size(seg), [2 4]);
    tst('eq', 'and two color pairs', size(col), [2 8]);
    leftEnd = squeeze(img(400, 104, 1:3))';
    middle = squeeze(img(400, 320, 1:3))';
    rightEnd = squeeze(img(400, 536, 1:3))';
    tst('ok', 'the left end is red', ...
        leftEnd(1) > 0.9 && leftEnd(3) < 0.1);
    tst('ok', 'the middle is green', middle(2) > 0.9 && ...
        middle(1) < 0.1 && middle(3) < 0.1);
    tst('ok', 'the right end is blue', ...
        rightEnd(3) > 0.9 && rightEnd(1) < 0.1);
    tst('ok', 'the two ends differ in color', ...
        norm(leftEnd - rightEnd) > 1);
    quarter = squeeze(img(400, 210, 1:3))';
    tst('ok', 'a quarter of the way along, red and green mix', ...
        quarter(1) > 0.3 && quarter(2) > 0.3);
    tst('ok', 'the stroke has its width', img(400 - 4, 320, 2) > 0.9 && ...
        img(400 - 10, 320, 2) < 0.05);

    %% ---------- GPU timer ----------
    % A GL 3.3 context always has timer queries. After three more frames
    % at least one pair has been read. A GL 2.1 context, as on macOS, has
    % them only with GL_ARB_timer_query, and reports NaN otherwise.
    for k = 1:3
        PsychNanoVGFrame('Begin', vg);
        PsychNanoVG('BeginPath');
        PsychNanoVG('Path', gauge);
        PsychNanoVG('Fill');
        PsychNanoVGFrame('End', vg);
    end
    s = PsychNanoVG('Stats');
    v = PsychNanoVG('Version');
    if strncmp(v.glVersion, '2.', 2) || strncmp(v.glVersion, '3.0', 3) || ...
       strncmp(v.glVersion, '3.1', 3) || strncmp(v.glVersion, '3.2', 3)
        tst('ok', 'gpuNs is NaN or measured on an older context', ...
            isnan(s.gpuNs) || s.gpuNs > 0);
    else
        tst('ok', 'gpuNs is measured on a GL 3.3 context', ...
            ~isnan(s.gpuNs) && s.gpuNs > 0);
    end
end
