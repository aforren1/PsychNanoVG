function pnvg_gl_close(win)
%PNVG_GL_CLOSE  Shut the NanoVG context down and close the window.
%
%   R7: the context is deleted while a GL context is still current, so the
%   GL objects go with it rather than being left to the driver.

    try
        Screen('BeginOpenGL', win);
        PsychNanoVG('Shutdown');
        Screen('EndOpenGL', win);
    catch
    end
    sca;
end
