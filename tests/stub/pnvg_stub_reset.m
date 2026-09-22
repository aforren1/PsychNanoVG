function s = pnvg_stub_reset()
%PNVG_STUB_RESET  The initial state of the Screen test stub.
%
%   s = pnvg_stub_reset()
%
%   Call it through the global to start a test from a known state:
%       global PNVG_SCREEN_STUB
%       PNVG_SCREEN_STUB = pnvg_stub_reset();
%
%   See also Screen, test_helpers.

    s = struct('log', {{}}, ...
               'drawMode', 0, ...
               'win', 10, ...
               'rect', [0 0 640 480], ...
               'enable3d', 1, ...
               'failBegin', false);
end
