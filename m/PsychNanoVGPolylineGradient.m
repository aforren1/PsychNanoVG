function [seg, col] = PsychNanoVGPolylineGradient(xy, rgba)
%PSYCHNANOVGPOLYLINEGRADIENT  Stroke a polyline with one color per vertex.
%
%   PsychNanoVGPolylineGradient(xy, rgba)
%   [seg, col] = PsychNanoVGPolylineGradient(xy, rgba)
%
%   xy is an Nx2 matrix of vertices. rgba is Nx4 or Nx3 (alpha 1), one color
%   per vertex, 0 to 1. A single 1x4 or 1x3 row gives every vertex the same
%   color. The function draws segment k from vertex k to vertex k+1 with a
%   linear gradient from color k to color k+1, all in one MEX call to
%   StrokeSegments.
%
%   Call it between BeginFrame and EndFrame. It uses the current stroke
%   width, line cap, line join, transform, and global alpha, like Stroke.
%   It replaces the current path, and it leaves the stroke paint as it was.
%
%   Each gradient segment is its own stroke, so there is no join between
%   two segments. Set PsychNanoVG('LineCap', 'ROUND') to close the gaps at
%   the corners. Consecutive segments that all have one color are stroked
%   as one path, with real joins.
%
%   Segments of zero length are left out, because a repeated vertex gives
%   no direction for its gradient.
%
%   seg is the Mx4 matrix [x0 y0 x1 y1] and col the Mx8 matrix
%   [r0 g0 b0 a0 r1 g1 b1 a1] that went to StrokeSegments, M <= N-1. They
%   let a script cache the segments and call StrokeSegments itself.
%
%   Example:
%       t = linspace(0, 2*pi, 200)';
%       xy = [320 + 200*cos(t), 240 + 120*sin(2*t)];
%       rgba = [0.5 + 0.5*cos(t), 0.5 + 0.5*sin(t), 1 - t/(2*pi), ones(200, 1)];
%       PsychNanoVG('StrokeWidth', 6);
%       PsychNanoVG('LineCap', 'ROUND');
%       PsychNanoVGPolylineGradient(xy, rgba);
%
%   See also PsychNanoVG, PsychNanoVGFrame.

    if ~isnumeric(xy) || ~isreal(xy) || ndims(xy) ~= 2 || size(xy, 2) ~= 2
        error('psychnanovg:Type', ...
              'PsychNanoVGPolylineGradient: xy must be a real Nx2 matrix');
    end
    n = size(xy, 1);
    if ~isnumeric(rgba) || ~isreal(rgba) || ndims(rgba) ~= 2 || ...
       ~any(size(rgba, 2) == [3 4])
        error('psychnanovg:Type', ...
              'PsychNanoVGPolylineGradient: rgba must be a real Nx4 or Nx3 matrix');
    end
    if size(rgba, 1) == 1
        rgba = repmat(rgba, n, 1);
    elseif size(rgba, 1) ~= n
        error('psychnanovg:Usage', ...
              ['PsychNanoVGPolylineGradient: %d vertices but %d colors; ' ...
               'give one color per vertex or one for all'], n, size(rgba, 1));
    end
    if size(rgba, 2) == 3
        rgba = [rgba, ones(n, 1)];
    end
    % StrokeSegments reads double and single. Single stays single, so a
    % script that keeps its data in single pays for no conversion here.
    if ~isa(xy, 'single')
        xy = double(xy);
    end
    if ~isa(rgba, 'single')
        rgba = double(rgba);
    end

    if n < 2
        seg = zeros(0, 4, class(xy));
        col = zeros(0, 8, class(rgba));
        return;
    end
    a = 1:n - 1;
    b = 2:n;
    keep = any(xy(a, :) ~= xy(b, :), 2);
    a = a(keep);
    b = b(keep);
    seg = [xy(a, :), xy(b, :)];
    col = [rgba(a, :), rgba(b, :)];
    if ~isempty(seg)
        PsychNanoVG('StrokeSegments', seg, col);
    end
end
