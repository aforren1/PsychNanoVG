function vg = PsychNanoVGOpen(win, opts)
%PSYCHNANOVGOPEN  Create a NanoVG context for an open Psychtoolbox window.
%
%   vg = PsychNanoVGOpen(win)
%   vg = PsychNanoVGOpen(win, opts)
%
%   `win` is the handle of an onscreen window. `opts` is the option struct of
%   PsychNanoVG('Init'): `antialias`, `stencilStrokes`, `debug`, `renderer`.
%
%   The function opens the OpenGL region itself, so a script never writes a
%   Screen('BeginOpenGL') and Screen('EndOpenGL') pair. It also loads a
%   default sans font when FindSystemFont finds Arial or DejaVuSans, inside
%   the same region.
%
%   The returned struct is the handle that the other helpers take:
%
%     vg.win      the window handle
%     vg.rect     the window rect, the default frame size
%     vg.ctx      the context handle from PsychNanoVG('Init')
%     vg.opened   true when the context is live
%     vg.fonts    a struct of font handles. `sans` is the default font, and
%                 `sansFile` is the file it came from. Both are absent when
%                 no system font was found.
%
%   Call it once per window. Psychtoolbox gives every onscreen window its
%   own OpenGL context, and the fonts, images, and render targets of one
%   context do not exist in another, so each window needs its own. The new
%   context becomes the current one. PsychNanoVGFrame, PsychNanoVGGL, and
%   PsychNanoVGClose make vg.ctx current again before they act, so a script
%   with two windows never calls PsychNanoVG('SetContext') itself.
%
%   Example:
%       InitializeMatlabOpenGL(1);
%       [win, rect] = PsychImaging('OpenWindow', screenid, 0);
%       vg = PsychNanoVGOpen(win);
%       PsychNanoVGFrame('Begin', vg);
%       PsychNanoVG('BeginPath');
%       PsychNanoVG('Circle', 300, 300, 100);
%       PsychNanoVG('FillColor', [1 1 1 1]);
%       PsychNanoVG('Fill');
%       PsychNanoVGFrame('End', vg);
%       Screen('Flip', win);
%       PsychNanoVGClose(vg);
%
%   See also PsychNanoVGFrame, PsychNanoVGGL, PsychNanoVGClose, PsychNanoVG,
%   PsychNanoVGTwoWindowDemo.

    if nargin < 1 || isempty(win)
        error('psychnanovg:Usage', ...
              'PsychNanoVGOpen needs the handle of an open onscreen window');
    end
    if nargin < 2 || isempty(opts)
        opts = struct();
    end
    if ~isstruct(opts)
        error('psychnanovg:Type', 'PsychNanoVGOpen: opts must be a struct');
    end

    PsychNanoVGSetup();

    % Screen('BeginOpenGL') needs the window to have been opened with 3D
    % graphics on. That happens when InitializeMatlabOpenGL runs before
    % OpenWindow, and the preference keeps its value afterwards, so this
    % catches the mistake before the first confusing Screen error.
    if Screen('Preference', 'Enable3DGraphics') == 0
        error('psychnanovg:No3DGraphics', ...
              ['3D graphics are off, so Screen(''BeginOpenGL'') will fail.\n' ...
               'Call InitializeMatlabOpenGL(1) before you open the window:\n' ...
               '    InitializeMatlabOpenGL(1);\n' ...
               '    [win, rect] = PsychImaging(''OpenWindow'', screenid, 0);\n' ...
               '    vg = PsychNanoVGOpen(win);']);
    end

    vg = struct('win', win, 'rect', Screen('Rect', win), 'ctx', 0, ...
                'opened', false, 'fonts', struct());

    Screen('BeginOpenGL', win);
    try
        vg.ctx = PsychNanoVG('Init', opts);
    catch err
        end_gl(win);
        rethrow(err);
    end
    vg.opened = true;

    % A missing font is not a reason to refuse the context, so the default
    % font is best effort and the caller can still load its own.
    try
        vg.fonts = load_default_font();
    catch fontErr
        warning('psychnanovg:Font', ...
                'The default font did not load: %s', fontErr.message);
    end

    end_gl(win);
end

% ---------------------------------------------------------------------------

function fonts = load_default_font()
    fonts = struct();
    for family = {'Arial', 'DejaVuSans', 'LiberationSans', 'Helvetica'}
        p = PsychNanoVG('FindSystemFont', family{1});
        if isempty(p)
            continue;
        end
        id = PsychNanoVG('CreateFont', 'sans', p);
        if id >= 0
            fonts.sans = id;
            fonts.sansFile = p;
            return;
        end
    end
end

function end_gl(win)
% Screen('EndOpenGL') has to run even when the wrapped call failed. Without
% it Psychtoolbox stays in userspace rendering mode and every later Screen
% drawing command goes to the wrong place. A failure here must not hide the
% first error, so it is swallowed.
    try
        Screen('EndOpenGL', win);
    catch
    end
end
