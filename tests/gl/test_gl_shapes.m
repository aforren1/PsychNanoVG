function test_gl_shapes()
%TEST_GL_SHAPES  A filled circle and a stroked rectangle, read back (SPEC 11.2).
%
%   Needs Psychtoolbox and a GPU. run_tests skips the tests/gl directory
%   when Screen does not answer. The test uses the convenience layer, so it
%   writes no Screen('BeginOpenGL') pair of its own.

    if isempty(which('Screen'))
        fprintf('   skipped test_gl_shapes: no Screen\n');
        return;
    end

    w = 640;
    h = 480;
    vg = pnvg_gl_open(w, h);
    cleanup = onCleanup(@() pnvg_gl_close(vg));

    cx = 200;
    cy = 200;
    r = 80;

    Screen('FillRect', vg.win, 0);

    PsychNanoVGFrame('Begin', vg);
    PsychNanoVG('BeginPath');
    PsychNanoVG('Circle', cx, cy, r);
    PsychNanoVG('FillColor', [1 1 1 1]);
    PsychNanoVG('Fill');
    PsychNanoVG('BeginPath');
    PsychNanoVG('Rect', 400.5, 100.5, 160, 120);
    PsychNanoVG('StrokeWidth', 4);
    PsychNanoVG('StrokeColor', [0 1 0 1]);
    PsychNanoVG('Stroke');
    PsychNanoVGFrame('End', vg);

    Screen('Flip', vg.win, 0, 1);

    img = double(Screen('GetImage', vg.win, [0 0 w h], 'drawBuffer')) / 255;

    inside = mean(mean(img(cy - 20 : cy + 20, cx - 20 : cx + 20, 1)));
    outside = mean(mean(img(cy - 20 : cy + 20, ...
                            cx + r + 20 : cx + r + 60, 1)));
    tst('ok', 'the circle interior is white', inside > 0.95);
    tst('ok', 'outside the circle is black', outside < 0.05);

    % The antialiased edge is about one pixel wide: the run of intermediate
    % values along a row through the center should be short.
    row = img(cy, :, 1);
    edge = find(row > 0.05 & row < 0.95);
    edge = edge(edge > cx & edge < cx + r + 10);
    tst('ok', 'the antialiased edge is about one pixel wide', numel(edge) <= 3);

    % The stroked rectangle sits on half pixel coordinates, so its 4 pixel
    % stroke lands on whole pixels and the green channel is saturated.
    strokeCol = img(160, 400, 2);
    tst('ok', 'the stroke is green', strokeCol > 0.9);
    tst('ok', 'the rectangle interior is empty', img(160, 480, 2) < 0.05);

    % The convenience layer has to put Psychtoolbox back in 2D mode.
    [~, isUserspace] = Screen('GetOpenGLDrawMode');
    tst('eq', 'the frame left 2D mode behind', isUserspace, 0);

    PsychNanoVG('Stats', 'reset');
end
