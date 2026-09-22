function [win, rect] = pnvg_gl_open(w, h)
%PNVG_GL_OPEN  Open a test window and create the NanoVG context in it.
%
%   The window itself comes from ptb_test_window, which is the one place
%   that sets the Psychtoolbox preferences for an unattended run.

    [win, rect] = ptb_test_window(w, h);
    Screen('BeginOpenGL', win);
    PsychNanoVG('Init');
    Screen('EndOpenGL', win);
end
