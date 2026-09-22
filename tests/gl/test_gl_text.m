function test_gl_text()
%TEST_GL_TEXT  TextBounds against the rendered extent (SPEC 11.2).

    if isempty(which('Screen'))
        fprintf('   skipped test_gl_text: no Screen\n');
        return;
    end

    w = 640;
    h = 480;
    [win, ~] = pnvg_gl_open(w, h);
    cleanup = onCleanup(@() pnvg_gl_close(win)); %#ok<NASGU>

    path = PsychNanoVG('FindSystemFont', 'Arial');
    if isempty(path)
        path = PsychNanoVG('FindSystemFont', 'DejaVuSans');
    end
    tst('ok', 'FindSystemFont found a font', ~isempty(path));
    if isempty(path)
        return;
    end

    Screen('FillRect', win, 0);
    Screen('BeginOpenGL', win);
    font = PsychNanoVG('CreateFont', 'sans', path);
    tst('ok', 'CreateFont returned a handle', font >= 0);

    PsychNanoVG('BeginFrame', w, h);
    PsychNanoVG('FontFaceId', font);
    PsychNanoVG('FontSize', 48);
    PsychNanoVG('TextAlign', 'ALIGN_LEFT|ALIGN_TOP');

    [asc, desc, lineh] = PsychNanoVG('TextMetrics');
    tst('ok', 'the ascender is positive', asc > 0);
    tst('ok', 'the descender is negative or zero', desc <= 0);
    tst('ok', 'the line height is positive', lineh > 0);
    tst('ok', 'the line height covers the ascender', lineh >= asc);

    str = 'Hamburgefonstiv';
    x0 = 60;
    y0 = 120;
    [advance, bounds] = PsychNanoVG('TextBounds', x0, y0, str);
    tst('ok', 'TextBounds returns a positive advance', advance > 0);
    tst('ok', 'the bounds are 1x4', isequal(size(bounds), [1 4]));

    PsychNanoVG('FillColor', [1 1 1 1]);
    PsychNanoVG('Text', x0, y0, str);
    PsychNanoVG('EndFrame');
    Screen('EndOpenGL', win);
    Screen('Flip', win, 0, 1);

    img = double(Screen('GetImage', win, [0 0 w h], 'drawBuffer')) / 255;
    % A low threshold so the faint edge of the antialiasing counts as ink.
    lit = img(:, :, 1) > 0.02;
    cols = find(any(lit, 1));
    rows = find(any(lit, 2));
    tst('ok', 'text was drawn', ~isempty(cols));
    if isempty(cols)
        return;
    end

    % Screen('GetImage') is 1-based and the bounds are in the same pixel
    % coordinates. TextBounds reports the glyph quads, which carry the atlas
    % padding of fontstash, so the bounds sit a few pixels outside the ink.
    ink = [cols(1) - 1, rows(1) - 1, cols(end) - 1, rows(end) - 1];
    tst('near', 'TextBounds left edge', ink(1), bounds(1), 5);
    tst('near', 'TextBounds right edge', ink(3), bounds(3), 5);
    tst('near', 'TextBounds top edge', ink(2), bounds(2), 5);
    tst('near', 'TextBounds bottom edge', ink(4), bounds(4), 5);

    % Glyph positions and line breaking come from the same layout.
    Screen('BeginOpenGL', win);
    PsychNanoVG('BeginFrame', w, h);
    PsychNanoVG('FontFaceId', font);
    PsychNanoVG('FontSize', 48);
    [n, pos] = PsychNanoVG('TextGlyphPositions', x0, y0, str);
    tst('eq', 'one glyph position per character', n, numel(str));
    tst('ok', 'the glyph positions rise', all(diff([pos.x]) >= 0));
    tst('eq', 'the first glyph index is 1', pos(1).index, 1);
    [nr, rowsOut] = PsychNanoVG('TextBreakLines', ...
                                'one two three four five six', 120);
    tst('ok', 'TextBreakLines returned rows', nr > 1);
    tst('ok', 'each row has text', all(cellfun(@(t) ~isempty(t), ...
                                               {rowsOut.text})));
    PsychNanoVG('CancelFrame');
    Screen('EndOpenGL', win);
end
