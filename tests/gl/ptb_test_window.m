function [win, rect] = ptb_test_window(w, h, screenid, bg)
%PTB_TEST_WINDOW  Open a Psychtoolbox window for the GL tests and the demo.
%
%   [win, rect] = ptb_test_window()              640x480, black
%   [win, rect] = ptb_test_window(w, h)          w by h, black
%   [win, rect] = ptb_test_window([], [], id, bg) full screen on screen id
%
%   Every script in this project that opens a window goes through this
%   function, so the two preferences below are set in one place. A test run
%   must not stop for the display sync report or show the welcome splash,
%   and a windowed target cannot pass the sync tests anyway.
%
%   The function also calls InitializeMatlabOpenGL, which Screen('BeginOpenGL')
%   requires and which defines the global GL constant struct.

    if nargin < 1; w = 640; end
    if nargin < 2; h = 480; end
    if nargin < 3 || isempty(screenid); screenid = max(Screen('Screens')); end
    if nargin < 4 || isempty(bg); bg = 0; end

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
        [win, rect] = PsychImaging('OpenWindow', screenid, bg, [0 0 w h]);
    end
end
