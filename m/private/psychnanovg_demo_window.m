function [win, rect] = psychnanovg_demo_window(w, h, screenid, bg, xy)
%PSYCHNANOVG_DEMO_WINDOW  Open a Psychtoolbox window for the shipped demos.
%
%   [win, rect] = psychnanovg_demo_window()              640x480, black
%   [win, rect] = psychnanovg_demo_window(w, h)          w by h, black
%   [win, rect] = psychnanovg_demo_window([], [], id, bg) full screen on id
%   [win, rect] = psychnanovg_demo_window(w, h, id, bg, xy) w by h at xy
%
%   A copy of tests/gl/ptb_test_window.m for PsychNanoVGDemo and
%   PsychNanoVGTwoWindowDemo. A release zip does not ship tests/, and a demo
%   that put tests/gl on the path at run time would change the path while
%   the MEX file can be loaded and locked, which crashes Octave 10.1 (SPEC
%   section 14.4). As a private function it is on no path and cannot shadow
%   anything. Keep the body the same as the test copy.
%
%   The demos run unattended as well, for example from
%   tools/CaptureReadmeScreenshot.m, so the window must not stop for the
%   display sync report or show the welcome splash.
%
%   The function also calls InitializeMatlabOpenGL, which Screen('BeginOpenGL')
%   requires and which defines the global GL constant struct.

    if nargin < 1; w = 640; end
    if nargin < 2; h = 480; end
    if nargin < 3 || isempty(screenid); screenid = max(Screen('Screens')); end
    if nargin < 4 || isempty(bg); bg = 0; end
    if nargin < 5 || isempty(xy); xy = [0 0]; end

    global GL %#ok<GVMIS>
    AssertOpenGL();
    InitializeMatlabOpenGL(1);
    if isempty(GL)
        error('psychnanovg:Usage', ...
              'InitializeMatlabOpenGL did not define the global GL struct');
    end

    Screen('Preference', 'SkipSyncTests', 2);
    Screen('Preference', 'VisualDebugLevel', 0);

    if isempty(w) || isempty(h)
        [win, rect] = PsychImaging('OpenWindow', screenid, bg);
    else
        [win, rect] = PsychImaging('OpenWindow', screenid, bg, ...
                                   [xy(1) xy(2) xy(1) + w xy(2) + h]);
    end
end
