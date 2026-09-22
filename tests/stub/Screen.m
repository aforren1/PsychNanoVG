function varargout = Screen(cmd, varargin)
%SCREEN  Test stub that stands in for Psychtoolbox, for tests/test_helpers.
%
%   The stub records every call and answers the few subcommands that the
%   convenience layer uses. It exists so run_tests can check that the
%   helpers open and close the OpenGL region in the right order, and that
%   they leave 2D mode behind on an error, on a machine with no
%   Psychtoolbox and no GPU.
%
%   The test puts this directory on the path for the length of the test
%   only, so the real Screen MEX is shadowed and then restored.
%
%   State lives in the global PNVG_SCREEN_STUB:
%
%     .log         cell array of the subcommand names, in order
%     .drawMode    0 for 2D, 1 for userspace OpenGL rendering
%     .win         the window of the last BeginOpenGL
%     .rect        what Screen('Rect') returns
%     .enable3d    what Screen('Preference', 'Enable3DGraphics') returns
%     .failBegin   when true, BeginOpenGL raises, as it does for a closed window
%
%   See also test_helpers.

    global PNVG_SCREEN_STUB %#ok<GVMIS>
    if isempty(PNVG_SCREEN_STUB)
        PNVG_SCREEN_STUB = pnvg_stub_reset();
    end
    s = PNVG_SCREEN_STUB;
    s.log{end + 1} = cmd;
    PNVG_SCREEN_STUB = s;

    switch cmd
        case 'Preference'
            if isempty(varargin)
                error('Screen:Stub', 'Preference needs a name');
            end
            if strcmp(varargin{1}, 'Enable3DGraphics')
                varargout{1} = s.enable3d;
                if numel(varargin) > 1
                    s.enable3d = varargin{2};
                end
            else
                varargout{1} = 0;
            end

        case 'BeginOpenGL'
            if s.failBegin
                error('Screen:Stub', ...
                      'stub: the window is closed, BeginOpenGL is not possible');
            end
            if s.drawMode > 0
                error('Screen:Stub', 'stub: BeginOpenGL inside BeginOpenGL');
            end
            s.drawMode = 1;
            if ~isempty(varargin)
                s.win = varargin{1};
            end

        case 'EndOpenGL'
            if s.drawMode == 0
                error('Screen:Stub', 'stub: EndOpenGL without BeginOpenGL');
            end
            s.drawMode = 0;

        case 'GetOpenGLDrawMode'
            varargout{1} = s.win;
            if nargout > 1
                varargout{2} = s.drawMode;
            end

        case 'Rect'
            varargout{1} = s.rect;

        case {'Flip', 'FillRect', 'DrawText', 'Close', 'CloseAll'}
            % accepted and recorded, nothing to return

        otherwise
            error('Screen:Stub', ...
                  'the stub does not implement Screen(''%s'')', cmd);
    end

    PNVG_SCREEN_STUB = s;
end
