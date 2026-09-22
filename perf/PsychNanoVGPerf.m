function s = PsychNanoVGPerf(mode)
%PSYCHNANOVGPERF  Timings for the measurements of SPEC 9.4.
%
%   PsychNanoVGPerf          null renderer, MATLAB-side cost only
%   PsychNanoVGPerf('gl')    inside an open Psychtoolbox window
%
%   In null mode the GL work is absent, so the numbers are the cost of
%   crossing the MEX boundary: dispatch plus marshaling. That is the cost
%   that batching removes, so it is the number that decides whether a script
%   should call LineTo per vertex or hand Polyline a matrix.
%
%   In gl mode the same measurements run against a real context and
%   EndFrame reports the NanoVG tessellation and draw cost as well.
%
%   Returns a struct with every measured value in nanoseconds.

    if nargin < 1
        mode = 'null';
    end

    addpath(fullfile(fileparts(fileparts(mfilename('fullpath'))), 'm'));
    PsychNanoVGSetup();

    isGL = strcmpi(mode, 'gl');
    if isGL
        if isempty(which('Screen'))
            error('psychnanovg:Usage', 'gl mode needs Psychtoolbox');
        end
        addpath(fullfile(fileparts(fileparts(mfilename('fullpath'))), ...
                         'tests', 'gl'));
        [win, rect] = ptb_test_window(640, 480);
        w = RectWidth(rect);
        h = RectHeight(rect);
        Screen('BeginOpenGL', win);
        PsychNanoVG('Init');
    else
        w = 640;
        h = 480;
        PsychNanoVG('Init', struct('renderer', 'null'));
    end
    cleanup = onCleanup(@() cleanup_all(isGL));

    n = 1000;
    reps = 200;
    xy = [linspace(10, w - 10, n)', linspace(10, h - 10, n)'];
    px = xy(:, 1);
    py = xy(:, 2);
    op = PsychNanoVGOp();

    s = struct();
    s.points = n;
    s.reps = reps;

    s.baselineTotalNs = best(@() loop_baseline(px, py, n), reps);
    s.lineToTotalNs = best(@() loop_lineto_name(px, py, n), reps);
    s.opcodeTotalNs = best(@() loop_lineto_op(px, py, n, op.MoveTo, ...
                                              op.LineTo), reps);
    s.polylineTotalNs = best(@() loop_polyline(xy), reps);

    s.baselinePerCallNs = s.baselineTotalNs / (reps * n);
    s.lineToPerCallNs = s.lineToTotalNs / (reps * n);
    s.opcodePerCallNs = s.opcodeTotalNs / (reps * n);
    s.lineToNetNs = s.lineToPerCallNs - s.baselinePerCallNs;
    s.opcodeNetNs = s.opcodePerCallNs - s.baselinePerCallNs;
    s.polylinePerCallNs = s.polylineTotalNs / reps;
    s.polylinePerPointNs = s.polylineTotalNs / (reps * n);

    s.speedup = s.lineToTotalNs / s.polylineTotalNs;

    %% ---------- EndFrame for a demo ring and a 10000 point polyline ----------
    big = [linspace(10, w - 10, 10000)', ...
           h / 2 + 100 * sin(linspace(0, 20 * pi, 10000))'];
    PsychNanoVG('Stats', 'reset');
    for r = 1:20
        PsychNanoVG('BeginFrame', w, h);
        PsychNanoVG('BeginPath');
        PsychNanoVG('Circle', w / 2, h / 2, 120);
        PsychNanoVG('Circle', w / 2, h / 2, 90);
        PsychNanoVG('PathWinding', 'NVG_HOLE');
        PsychNanoVG('FillColor', [1 1 1 1]);
        PsychNanoVG('Fill');
        PsychNanoVG('EndFrame');
    end
    st = PsychNanoVG('Stats');
    s.ringEndFrameNs = st.endFrameSumNs / st.frames;

    PsychNanoVG('Stats', 'reset');
    for r = 1:20
        PsychNanoVG('BeginFrame', w, h);
        PsychNanoVG('BeginPath');
        PsychNanoVG('Polyline', big);
        PsychNanoVG('StrokeWidth', 2);
        PsychNanoVG('StrokeColor', [1 1 1 1]);
        PsychNanoVG('Stroke');
        PsychNanoVG('EndFrame');
    end
    st = PsychNanoVG('Stats');
    s.polylineEndFrameNs = st.endFrameSumNs / st.frames;
    s.polylineGpuNs = st.gpuNs;

    %% ---------- report ----------
    fprintf('\nPsychNanoVG perf (%s renderer, %d points, %d repetitions)\n', ...
            lower(mode), n, reps);
    fprintf('  empty loop          %7.2f us per iteration\n', ...
            s.baselinePerCallNs / 1000);
    fprintf('  LineTo by name      %7.2f us per call (%.2f us net)\n', ...
            s.lineToPerCallNs / 1000, s.lineToNetNs / 1000);
    fprintf('  LineTo by opcode    %7.2f us per call (%.2f us net)\n', ...
            s.opcodePerCallNs / 1000, s.opcodeNetNs / 1000);
    fprintf('  Polyline            %7.2f us per call, %6.1f ns per point\n', ...
            s.polylinePerCallNs / 1000, s.polylinePerPointNs);
    fprintf('  Polyline is %.0f times faster than per-vertex LineTo\n', s.speedup);
    fprintf('  EndFrame, ring      %7.2f us\n', s.ringEndFrameNs / 1000);
    fprintf('  EndFrame, 10000 pt  %7.2f us\n', s.polylineEndFrameNs / 1000);
    if isGL
        fprintf('  GPU, 10000 pt       %7.2f us\n', s.polylineGpuNs / 1000);
    end
end

function t = best(fn, reps)
% Runs the body twice and keeps the faster pass, in nanoseconds. The first
% pass pays for the JIT and for warming the caches, and that cost belongs to
% neither of the two ways of building a path.
    t = inf;
    for pass = 1:2
        PsychNanoVG('BeginFrame', 64, 64);
        PsychNanoVG('BeginPath');
        t0 = tic;
        for r = 1:reps
            fn();
        end
        el = toc(t0) * 1e9;
        PsychNanoVG('CancelFrame');
        if el < t
            t = el;
        end
    end
end

function loop_baseline(px, py, n)
% The same loop with the same indexing, and no MEX call in it.
    a = px(1);
    b = py(1);
    for k = 2:n
        a = px(k);
        b = py(k);
    end
    if a == b && a == -1
        disp('');   % keeps the loop from being optimized away
    end
end

function loop_lineto_name(px, py, n)
    PsychNanoVG('MoveTo', px(1), py(1));
    for k = 2:n
        PsychNanoVG('LineTo', px(k), py(k));
    end
end

function loop_lineto_op(px, py, n, opMoveTo, opLineTo)
    PsychNanoVG(opMoveTo, px(1), py(1));
    for k = 2:n
        PsychNanoVG(opLineTo, px(k), py(k));
    end
end

function loop_polyline(xy)
    PsychNanoVG('Polyline', xy);
end

function cleanup_all(isGL)
    try
        PsychNanoVG('Shutdown');
    catch
    end
    if isGL
        try
            Screen('EndOpenGL', max(Screen('Windows')));
        catch
        end
        sca;
    end
end
