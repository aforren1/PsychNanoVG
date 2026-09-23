function test_paths()
%TEST_PATHS  Argument handling for the batched path subcommands.
%
%   The null renderer keeps the geometry out of reach, so these tests pin the
%   contract that the handlers enforce: accepted shapes and classes, rejected
%   ones, and the error identifier for each.

    PsychNanoVG('Init', struct('renderer', 'null'));
    cleanup = onCleanup(@() shutdown_quietly());
    PsychNanoVG('BeginFrame', 640, 480);

    xy = [0 0; 10 0; 10 10; 0 10];

    %% ---------- Polyline ----------
    PsychNanoVG('BeginPath');
    ok_call('Polyline accepts Nx2 double', @() PsychNanoVG('Polyline', xy));
    ok_call('Polyline accepts single', ...
            @() PsychNanoVG('Polyline', single(xy)));
    ok_call('Polyline closes on request', ...
            @() PsychNanoVG('Polyline', xy, true));
    ok_call('Polyline accepts one row', @() PsychNanoVG('Polyline', [1 2]));
    ok_call('Polyline accepts an empty path', ...
            @() PsychNanoVG('Polyline', zeros(0, 2)));
    ok_call('Polyline accepts 10000 points', ...
            @() PsychNanoVG('Polyline', [(1:10000)', (1:10000)']));

    tst('throws', 'Polyline rejects Nx3', 'psychnanovg:Usage', ...
        @() PsychNanoVG('Polyline', zeros(4, 3)));
    tst('throws', 'Polyline rejects a cell', 'psychnanovg:Type', ...
        @() PsychNanoVG('Polyline', {1, 2}));
    tst('throws', 'Polyline rejects uint8', 'psychnanovg:Type', ...
        @() PsychNanoVG('Polyline', uint8(xy)));
    tst('throws', 'Polyline rejects complex', 'psychnanovg:Type', ...
        @() PsychNanoVG('Polyline', xy + 1i));

    %% ---------- Polygon ----------
    ok_call('Polygon accepts Nx2', @() PsychNanoVG('Polygon', xy));
    tst('throws', 'Polygon takes one argument', 'psychnanovg:Usage', ...
        @() PsychNanoVG('Polygon', xy, true));

    %% ---------- Circles and Rects ----------
    ok_call('Circles accepts Nx3', ...
            @() PsychNanoVG('Circles', [0 0 5; 20 20 7]));
    ok_call('Circles accepts 1000 rows', ...
            @() PsychNanoVG('Circles', rand(1000, 3) * 100));
    tst('throws', 'Circles rejects Nx2', 'psychnanovg:Usage', ...
        @() PsychNanoVG('Circles', zeros(3, 2)));
    ok_call('Rects accepts Nx4', ...
            @() PsychNanoVG('Rects', [0 0 10 10; 5 5 2 2]));
    tst('throws', 'Rects rejects Nx3', 'psychnanovg:Usage', ...
        @() PsychNanoVG('Rects', zeros(3, 3)));

    %% ---------- Path, cell form ----------
    cells = {{'M', 0, 0}, {'L', 10, 0}, {'Q', 15, 5, 10, 10}, ...
             {'C', 8, 12, 4, 12, 0, 10}, {'Z'}};
    ok_call('Path accepts the cell form', @() PsychNanoVG('Path', cells));
    ok_call('Path accepts lowercase letters', ...
            @() PsychNanoVG('Path', {{'m', 1, 1}, {'l', 2, 2}, {'z'}}));
    tst('throws', 'Path rejects an unknown letter', 'psychnanovg:Usage', ...
        @() PsychNanoVG('Path', {{'X', 1, 2}}));
    tst('throws', 'Path rejects the wrong argument count', ...
        'psychnanovg:Usage', @() PsychNanoVG('Path', {{'L', 1}}));
    tst('throws', 'Path rejects a non-cell entry', 'psychnanovg:Type', ...
        @() PsychNanoVG('Path', {5}));
    tst('throws', 'Path rejects a non-numeric coordinate', ...
        'psychnanovg:Type', @() PsychNanoVG('Path', {{'L', 'a', 2}}));

    %% ---------- Path, matrix form ----------
    % Command codes: 1=M, 2=L, 3=Q, 4=C, 5=Z, zero padded to 7 columns.
    m = [1 0 0 0 0 0 0;
         2 10 0 0 0 0 0;
         3 15 5 10 10 0 0;
         4 8 12 4 12 0 10;
         5 0 0 0 0 0 0];
    ok_call('Path accepts the matrix form', @() PsychNanoVG('Path', m));
    ok_call('Path accepts a single matrix', ...
            @() PsychNanoVG('Path', single(m)));
    tst('throws', 'Path rejects a bad command code', 'psychnanovg:Range', ...
        @() PsychNanoVG('Path', [99 0 0 0 0 0 0]));
    tst('throws', 'Path rejects the wrong column count', 'psychnanovg:Usage', ...
        @() PsychNanoVG('Path', zeros(2, 5)));

    %% ---------- Path, matrix form with arcs and shapes ----------
    % 6=Arc, 7=ArcTo, 8=Ellipse, 9=Circle, 10=Rect, 11=RoundedRect,
    % 12=Winding. A whole gauge, arcs included, is one call.
    g = [6 320 240 100 -pi 0 2;           % outer arc, clockwise
         6 320 240 80 0 -pi 1;            % inner arc back, counterclockwise
         5 0 0 0 0 0 0;
         1 10 10 0 0 0 0;
         7 60 10 60 60 10 0;              % rounded corner
         2 60 60 0 0 0 0;
         8 200 200 30 20 0 0;
         9 100 100 5 0 0 0;
         10 0 0 20 10 0 0;
         11 0 0 20 10 3 0;
         9 100 100 3 0 0 0;
         12 2 0 0 0 0 0];                 % the last circle is a hole
    PsychNanoVG('BeginPath');
    ok_call('Path accepts every command code', @() PsychNanoVG('Path', g));
    ok_call('Path accepts arcs in single', @() PsychNanoVG('Path', single(g)));
    PsychNanoVG('FillColor', [1 1 1 1]);
    ok_call('a path of arcs fills', @() PsychNanoVG('Fill'));
    tst('throws', 'Path rejects code 13', 'psychnanovg:Range', ...
        @() PsychNanoVG('Path', [13 0 0 0 0 0 0]));
    tst('throws', 'Path rejects code 0', 'psychnanovg:Range', ...
        @() PsychNanoVG('Path', [0 0 0 0 0 0 0]));
    tst('throws', 'Path rejects a fractional code', 'psychnanovg:Range', ...
        @() PsychNanoVG('Path', [6.5 0 0 1 0 1 1]));
    tst('throws', 'Path rejects a NaN code', 'psychnanovg:Range', ...
        @() PsychNanoVG('Path', [NaN 0 0 0 0 0 0]));
    tst('throws', 'Path rejects an Arc direction of 0', 'psychnanovg:Range', ...
        @() PsychNanoVG('Path', [6 0 0 10 0 1 0]));
    tst('throws', 'Path rejects an Arc direction of 3', 'psychnanovg:Range', ...
        @() PsychNanoVG('Path', [6 0 0 10 0 1 3]));
    tst('throws', 'Path rejects a Winding of 5', 'psychnanovg:Range', ...
        @() PsychNanoVG('Path', [12 5 0 0 0 0 0]));
    tst('throws', 'Path names the bad row', 'psychnanovg:Range', ...
        @() PsychNanoVG('Path', [1 0 0 0 0 0 0; 2 1 1 0 0 0 0; 99 0 0 0 0 0 0]));
    try
        PsychNanoVG('Path', [1 0 0 0 0 0 0; 99 0 0 0 0 0 0]);
        msg = '';
    catch e
        msg = e.message;
    end
    tst('ok', 'the message names row 2', ~isempty(strfind(msg, 'row 2')));

    %% ---------- Path, cell form with arcs and shapes ----------
    cellArcs = {{'M', 0, 0}, {'Arc', 50, 50, 20, 0, pi, 'CW'}, ...
                {'ArcTo', 90, 0, 90, 40, 5}, {'Ellipse', 1, 2, 3, 4}, ...
                {'Circle', 5, 5, 2}, {'Rect', 0, 0, 3, 3}, ...
                {'RoundedRect', 0, 0, 9, 9, 2}, {'Winding', 'NVG_HOLE'}, ...
                {'arc', 0, 0, 1, 0, 1, 1}, {'Z'}};
    ok_call('Path accepts arcs in the cell form', ...
            @() PsychNanoVG('Path', cellArcs));
    tst('throws', 'the cell form checks the Arc argument count', ...
        'psychnanovg:Usage', @() PsychNanoVG('Path', {{'Arc', 0, 0, 1, 0, 1}}));
    tst('throws', 'the cell form checks the Arc direction', ...
        'psychnanovg:Range', @() PsychNanoVG('Path', {{'Arc', 0, 0, 1, 0, 1, 4}}));
    tst('throws', 'the cell form checks the direction name', ...
        'psychnanovg:Range', ...
        @() PsychNanoVG('Path', {{'Winding', 'NVG_ALIGN_RIGHT'}}));
    tst('throws', 'the cell form needs the whole command name', ...
        'psychnanovg:Usage', @() PsychNanoVG('Path', {{'MoveTo', 1, 2}}));
    tst('throws', 'the cell form rejects an unknown name', ...
        'psychnanovg:Range', @() PsychNanoVG('Path', {{'Winding', 'NOPE'}}));

    %% ---------- StrokeSegments ----------
    seg = [0 0 10 0; 10 0 10 10; 10 10 0 10];
    col = [1 0 0 1 0 0 1 1; 0 0 1 1 0 1 0 1; 0 1 0 1 0 1 0 1];
    ok_call('StrokeSegments accepts Nx4 and Nx8', ...
            @() PsychNanoVG('StrokeSegments', seg, col));
    ok_call('StrokeSegments accepts single and double mixed', ...
            @() PsychNanoVG('StrokeSegments', single(seg), col));
    ok_call('StrokeSegments accepts no segments', ...
            @() PsychNanoVG('StrokeSegments', zeros(0, 4), zeros(0, 8)));
    ok_call('StrokeSegments accepts 1000 segments', ...
            @() PsychNanoVG('StrokeSegments', rand(1000, 4) * 100, ...
                            rand(1000, 8)));
    tst('throws', 'StrokeSegments rejects Nx3 segments', 'psychnanovg:Usage', ...
        @() PsychNanoVG('StrokeSegments', zeros(3, 3), col));
    tst('throws', 'StrokeSegments rejects Nx4 colors', 'psychnanovg:Usage', ...
        @() PsychNanoVG('StrokeSegments', seg, zeros(3, 4)));
    tst('throws', 'StrokeSegments rejects a row count mismatch', ...
        'psychnanovg:Usage', @() PsychNanoVG('StrokeSegments', seg, col(1:2, :)));
    tst('throws', 'StrokeSegments rejects uint8 colors', 'psychnanovg:Type', ...
        @() PsychNanoVG('StrokeSegments', seg, uint8(col)));
    tst('throws', 'StrokeSegments rejects a cell', 'psychnanovg:Type', ...
        @() PsychNanoVG('StrokeSegments', {seg}, col));
    tst('throws', 'StrokeSegments takes two arguments', 'psychnanovg:Usage', ...
        @() PsychNanoVG('StrokeSegments', seg));

    % StrokeSegments swaps the stroke paint in and out behind NanoVG's back,
    % so the state stack and a later Stroke must still be sound. The pixels
    % are checked in tests/gl.
    PsychNanoVG('Save');
    PsychNanoVG('StrokeColor', [0.25 0.5 0.75 1]);
    PsychNanoVG('StrokeSegments', seg, col);
    PsychNanoVG('BeginPath');
    PsychNanoVG('Rect', 0, 0, 5, 5);
    ok_call('Stroke works after StrokeSegments', @() PsychNanoVG('Stroke'));
    PsychNanoVG('Restore');

    %% ---------- PsychNanoVGPolylineGradient ----------
    xyp = [0 0; 10 0; 10 0; 20 5; 30 5];     % one repeated vertex
    rgbp = [1 0 0 1; 0 1 0 1; 0 1 0 1; 0 0 1 1; 1 1 1 1];
    [s1, c1] = PsychNanoVGPolylineGradient(xyp, rgbp);
    tst('eq', 'the helper drops the zero-length segment', size(s1), [3 4]);
    tst('eq', 'the helper gives one color pair per segment', size(c1), [3 8]);
    tst('eq', 'segment 1 starts at vertex 1', s1(1, :), [0 0 10 0]);
    tst('eq', 'segment 2 starts after the repeat', s1(2, :), [10 0 20 5]);
    tst('eq', 'segment 2 runs from color 3 to color 4', c1(2, :), ...
        [0 1 0 1 0 0 1 1]);
    [s2, c2] = PsychNanoVGPolylineGradient(xyp, rgbp(:, 1:3));
    tst('eq', 'an Nx3 color gets alpha 1', c2(:, [4 8]), ones(3, 2));
    tst('eq', 'Nx3 gives the same segments', s2, s1);
    [~, c3] = PsychNanoVGPolylineGradient(xyp, [0.5 0.5 0.5]);
    tst('eq', 'one color is used for every vertex', c3, ...
        repmat([0.5 0.5 0.5 1], 3, 2));
    [s4, c4] = PsychNanoVGPolylineGradient(single(xyp), single(rgbp));
    tst('ok', 'single stays single', isa(s4, 'single') && isa(c4, 'single'));
    [s5, c5] = PsychNanoVGPolylineGradient([1 1], [1 1 1 1]);
    tst('eq', 'one vertex gives no segments', [size(s5), size(c5)], [0 4 0 8]);
    tst('throws', 'the helper rejects Nx3 vertices', 'psychnanovg:Type', ...
        @() PsychNanoVGPolylineGradient(zeros(3, 3), rgbp));
    tst('throws', 'the helper rejects Nx5 colors', 'psychnanovg:Type', ...
        @() PsychNanoVGPolylineGradient(xyp, zeros(5, 5)));
    tst('throws', 'the helper rejects a color count mismatch', ...
        'psychnanovg:Usage', @() PsychNanoVGPolylineGradient(xyp, rgbp(1:2, :)));

    %% ---------- the batch form matches the per-call form ----------
    % Both paths reach the same NanoVG calls, so a fill after either has to
    % succeed and leave the frame usable.
    PsychNanoVG('BeginPath');
    PsychNanoVG('MoveTo', 0, 0);
    PsychNanoVG('LineTo', 10, 0);
    PsychNanoVG('LineTo', 10, 10);
    PsychNanoVG('ClosePath');
    PsychNanoVG('FillColor', [1 1 1 1]);
    PsychNanoVG('Fill');
    PsychNanoVG('BeginPath');
    PsychNanoVG('Polygon', [0 0; 10 0; 10 10]);
    PsychNanoVG('Fill');
    ok_call('a frame with both path forms ends cleanly', ...
            @() PsychNanoVG('EndFrame'));

    %% ---------- the batch commands need a frame ----------
    tst('throws', 'Polyline outside a frame', 'psychnanovg:FrameState', ...
        @() PsychNanoVG('Polyline', xy));
    tst('throws', 'Path outside a frame', 'psychnanovg:FrameState', ...
        @() PsychNanoVG('Path', m));
    tst('throws', 'StrokeSegments outside a frame', 'psychnanovg:FrameState', ...
        @() PsychNanoVG('StrokeSegments', seg, col));

    %% ---------- Stats reports NaN GPU time without timer queries ----------
    s = PsychNanoVG('Stats');
    tst('ok', 'the null renderer reports gpuNs as NaN', isnan(s.gpuNs));
    PsychNanoVG('Stats', 'reset');
    s = PsychNanoVG('Stats');
    tst('ok', 'Stats reset keeps gpuNs NaN', isnan(s.gpuNs));
end

% ---------------------------------------------------------------------------

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
