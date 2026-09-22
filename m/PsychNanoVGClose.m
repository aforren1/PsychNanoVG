function PsychNanoVGClose(vg)
%PSYCHNANOVGCLOSE  Delete the NanoVG context of a window.
%
%   PsychNanoVGClose(vg)
%
%   Deletes the render targets, the images, the fonts, and the context, and
%   unlocks the MEX file. Put it in an onCleanup or a catch block so a script
%   that fails still gives the graphics driver its objects back.
%
%   The call is safe twice, and safe after the window is already closed. A
%   context that outlives its window cannot delete its OpenGL objects, so the
%   MEX warns and leaves them to the driver, which frees them with the
%   context anyway.
%
%   Example:
%       vg = PsychNanoVGOpen(win);
%       cleanup = onCleanup(@() PsychNanoVGClose(vg));
%
%   See also PsychNanoVGOpen, PsychNanoVGFrame, PsychNanoVGGL.

    win = [];
    if nargin >= 1 && isstruct(vg) && isfield(vg, 'win')
        win = vg.win;
    end

    inRegion = false;
    if ~isempty(win)
        try
            Screen('BeginOpenGL', win);
            inRegion = true;
        catch
            % The window is gone, or Screen is not loaded. Shutdown still has
            % to run so that the MEX unlocks and a later Init can succeed.
        end
    end

    try
        PsychNanoVG('Shutdown');
    catch err
        % A second close, or a close with no context, is not a failure.
        if ~strcmp(err.identifier, 'psychnanovg:NotInit')
            if inRegion
                end_gl(win);
            end
            rethrow(err);
        end
    end

    if inRegion
        end_gl(win);
    end
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
