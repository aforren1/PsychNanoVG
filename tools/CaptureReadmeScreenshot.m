function outFile = CaptureReadmeScreenshot(outFile)
%CAPTUREREADMESCREENSHOT  Make the screenshot at the top of README.md.
%
%   CaptureReadmeScreenshot
%   CaptureReadmeScreenshot(outFile)
%
%   Opens a 1280x720 Psychtoolbox window with the test preferences
%   (SkipSyncTests 2 and VisualDebugLevel 0), draws five frames of
%   PsychNanoVGDemo, reads the last one back with Screen('GetImage'),
%   closes the window, and writes docs/images/psychnanovg-demo.png.
%
%   The frame is fixed: the animation phase, the point that the eyes look
%   at, and the blink time come from this file, not from the clock or the
%   mouse. Two runs on one machine therefore give the same picture.
%
%   The file must stay below 400 KB. PNG is lossless, so a picture that is
%   too large is made smaller in steps of 3/4 of its size, not compressed
%   harder.
%
%   Run it under MATLAB with Psychtoolbox:
%       matlab -batch "addpath('tools'); CaptureReadmeScreenshot"

    here = fileparts(mfilename('fullpath'));
    root = fileparts(here);
    if nargin < 1 || isempty(outFile)
        outFile = fullfile(root, 'docs', 'images', 'psychnanovg-demo.png');
    end
    maxBytes = 400 * 1024;

    % This is a source-tree tool, not shipped, and it changes the path once,
    % before the MEX file is loaded, so the addpath is safe here. The shipped
    % demos never change the path.
    addpath(fullfile(root, 'm'));
    PsychNanoVGSetup();
    % PsychNanoVGDemo opens its window through m/private/
    % psychnanovg_demo_window, which sets these two as well. They are set
    % here too, so that this script states what it needs.
    Screen('Preference', 'SkipSyncTests', 2);
    Screen('Preference', 'VisualDebugLevel', 0);

    opts = struct('size', [1280 720], ...
                  'frames', 5, ...
                  'phase', 0.8, ...
                  'pointer', [560 330], ...
                  'time', 1.0);
    img = PsychNanoVGDemo([], [], opts);
    if isempty(img)
        error('psychnanovg:Capture', 'PsychNanoVGDemo returned no image');
    end

    outDir = fileparts(outFile);
    if ~exist(outDir, 'dir')
        mkdir(outDir);
    end
    imwrite(img, outFile);
    info = dir(outFile);
    while info.bytes > maxBytes
        img = shrink(img, 0.75);
        imwrite(img, outFile);
        info = dir(outFile);
    end
    fprintf('wrote %s, %dx%d, %.0f KB\n', outFile, size(img, 2), ...
            size(img, 1), info.bytes / 1024);
end

function out = shrink(img, f)
% Bilinear resampling with base MATLAB, so no toolbox is needed. Steps of
% 3/4 are small enough that bilinear keeps thin lines whole.
    [h, w, c] = size(img);
    nh = round(h * f);
    nw = round(w * f);
    [xq, yq] = meshgrid(linspace(1, w, nw), linspace(1, h, nh));
    out = zeros(nh, nw, c, 'uint8');
    src = double(img);
    for k = 1:c
        out(:, :, k) = uint8(round(interp2(src(:, :, k), xq, yq, 'linear')));
    end
end
