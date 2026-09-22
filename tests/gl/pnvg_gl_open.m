function vg = pnvg_gl_open(w, h)
%PNVG_GL_OPEN  Open a test window and create the NanoVG context in it.
%
%   vg = pnvg_gl_open(w, h)
%
%   The window comes from ptb_test_window, which is the one place that sets
%   the Psychtoolbox preferences for an unattended run. The context comes
%   from PsychNanoVGOpen, the same call that a script makes.

    win = ptb_test_window(w, h);
    vg = PsychNanoVGOpen(win);
end
