function pnvg_gl_close(vg)
%PNVG_GL_CLOSE  Shut the NanoVG context down and close the window.
%
%   pnvg_gl_close(vg)
%
%   R7: PsychNanoVGClose deletes the context while the window is still open,
%   so the OpenGL objects go with it rather than being left to the driver.

    try
        PsychNanoVGClose(vg);
    catch
    end
    sca;
end
