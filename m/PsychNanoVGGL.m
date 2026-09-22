function varargout = PsychNanoVGGL(vg, subcommand, varargin)
%PSYCHNANOVGGL  Run one OpenGL subcommand inside an OpenGL region.
%
%   [...] = PsychNanoVGGL(vg, subcommand, ...)
%
%   Some PsychNanoVG subcommands issue OpenGL calls outside a frame:
%   CreateFont, CreateImage, CreateImageRGBA, UpdateImage, DeleteImage,
%   CreateImageFromTexture, and the RenderTarget commands. This helper wraps
%   one of them in Screen('BeginOpenGL') and Screen('EndOpenGL'), so a setup
%   script never writes that pair.
%
%   When userspace rendering is already active, that is between a
%   Screen('BeginOpenGL') and its Screen('EndOpenGL'), or inside a
%   PsychNanoVGFrame region, the subcommand runs directly. Nesting the
%   regions would end the outer one too early.
%
%   Example:
%       img  = PsychNanoVGGL(vg, 'CreateImageRGBA', 0, rgba);
%       font = PsychNanoVGGL(vg, 'CreateFont', 'mono', fontPath);
%       [rt, glTex] = PsychNanoVGGL(vg, 'RenderTargetCreate', 256, 256);
%
%   A render target needs several subcommands in one region: bind, clear,
%   draw, unbind. This helper wraps exactly one subcommand, so that sequence
%   still uses an explicit region. Every call inside it can go through this
%   helper, because it passes through when a region is already open.
%
%   See also PsychNanoVGOpen, PsychNanoVGFrame, PsychNanoVGClose.

    if nargin < 2
        error('psychnanovg:Usage', ...
              'PsychNanoVGGL needs the struct from PsychNanoVGOpen and a subcommand');
    end
    if ~isstruct(vg) || ~isfield(vg, 'win')
        error('psychnanovg:Type', ...
              'PsychNanoVGGL: the first argument must come from PsychNanoVGOpen');
    end

    [~, isUserspace] = Screen('GetOpenGLDrawMode');
    if isUserspace > 0
        [varargout{1:nargout}] = PsychNanoVG(subcommand, varargin{:});
        return;
    end

    Screen('BeginOpenGL', vg.win);
    try
        [varargout{1:nargout}] = PsychNanoVG(subcommand, varargin{:});
    catch err
        end_gl(vg.win);
        rethrow(err);
    end
    Screen('EndOpenGL', vg.win);
end

% ---------------------------------------------------------------------------

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
