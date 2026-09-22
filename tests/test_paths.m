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
        @() PsychNanoVG('Path', [9 0 0 0 0 0 0]));
    tst('throws', 'Path rejects the wrong column count', 'psychnanovg:Usage', ...
        @() PsychNanoVG('Path', zeros(2, 5)));

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
