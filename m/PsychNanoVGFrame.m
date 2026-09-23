function PsychNanoVGFrame(op, vg, w, h)
%PSYCHNANOVGFRAME  Open and close one NanoVG frame, with the OpenGL region.
%
%   PsychNanoVGFrame('Begin', vg)
%   PsychNanoVGFrame('Begin', vg, w, h)
%   PsychNanoVGFrame('End', vg)
%
%   'Begin' enters the OpenGL region, makes the context of `vg` current,
%   and starts the frame. The default size is the window rect that
%   PsychNanoVGOpen recorded. Pass `w` and `h` when the drawing target is
%   not the window, for example an offscreen window or one eye of a stereo
%   pair.
%
%   'End' finishes the frame of `vg` and leaves the OpenGL region. All the
%   OpenGL work of the frame happens in 'End'.
%
%   With two windows, each window has its own struct and its own frame. The
%   frames follow each other and do not nest, because each one needs the
%   OpenGL region of its own window.
%
%   Between the two, draw with plain PsychNanoVG calls. Screen drawing
%   commands do not belong there: put them before 'Begin' or after 'End'.
%
%   Example:
%       Screen('FillRect', vg.win, 0.5);
%       PsychNanoVGFrame('Begin', vg);
%       PsychNanoVG('BeginPath');
%       PsychNanoVG('Circle', cx, cy, 100);
%       PsychNanoVG('FillColor', [1 1 1 1]);
%       PsychNanoVG('Fill');
%       PsychNanoVGFrame('End', vg);
%       Screen('DrawText', vg.win, 'Screen still works', 10, 10);
%       Screen('Flip', vg.win);
%
%   See also PsychNanoVGOpen, PsychNanoVGGL, PsychNanoVGClose.

    if nargin < 2
        error('psychnanovg:Usage', ...
              'PsychNanoVGFrame needs an operation and the struct from PsychNanoVGOpen');
    end
    if ~isstruct(vg) || ~isfield(vg, 'win')
        error('psychnanovg:Type', ...
              'PsychNanoVGFrame: the second argument must come from PsychNanoVGOpen');
    end

    switch lower(op)
        case 'begin'
            if nargin < 3 || isempty(w)
                w = vg.rect(3) - vg.rect(1);
            end
            if nargin < 4 || isempty(h)
                h = vg.rect(4) - vg.rect(2);
            end
            Screen('BeginOpenGL', vg.win);
            try
                select_context(vg);
                PsychNanoVG('BeginFrame', w, h);
            catch err
                % The frame never started, so leave the region rather than
                % hand the caller a half open one.
                end_gl(vg.win);
                rethrow(err);
            end

        case 'end'
            try
                % A PsychNanoVGGL call for another window between Begin and
                % End changes the current context, and End has to finish
                % this frame, not that one.
                select_context(vg);
                PsychNanoVG('EndFrame');
            catch err
                end_gl(vg.win);
                rethrow(err);
            end
            Screen('EndOpenGL', vg.win);

        otherwise
            error('psychnanovg:UnknownCommand', ...
                  'PsychNanoVGFrame: no operation "%s". Use Begin or End.', op);
    end
end

% ---------------------------------------------------------------------------

function select_context(vg)
% A struct from before phase 3, or one built by hand, has no ctx field. The
% current context is then the only one there is.
    if isfield(vg, 'ctx') && vg.ctx > 0
        PsychNanoVG('SetContext', vg.ctx);
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
