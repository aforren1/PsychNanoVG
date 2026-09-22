function out = PsychNanoVGSetup(cmd)
%PSYCHNANOVGSETUP  Put PsychNanoVG on the path for this engine and platform.
%
%   PsychNanoVGSetup             adds dist/<arch> and m/ to the path
%   a = PsychNanoVGSetup('arch') returns win64, glnxa64, maci64, or maca64
%   d = PsychNanoVGSetup('distdir')  returns dist/<arch>, path untouched
%   PsychNanoVGSetup('nocheck')  adds the path without checking for the MEX
%
%   The MEX goes to dist/<arch> because Octave names its MEX file
%   PsychNanoVG.mex on every operating system. One working tree is often
%   shared between Windows and WSL, and without the split the second build
%   would silently replace the first.
%
%   dist/<arch> goes on the path before m/, because a MEX file only takes
%   precedence over an M-file of the same name inside one directory, and
%   m/PsychNanoVG.m holds the help text.
%
%   Example:
%       addpath(fullfile(root, 'm'));
%       PsychNanoVGSetup();
%
%   See also PsychNanoVG, build.

    if nargin < 1
        cmd = 'add';
    end

    here = fileparts(mfilename('fullpath'));   % the m directory
    root = fileparts(here);
    a = pnvg_arch();
    distdir = fullfile(root, 'dist', a);

    switch lower(cmd)
        case 'arch'
            out = a;
            return;
        case 'distdir'
            out = distdir;
            return;
        case {'add', 'nocheck'}
            % fall through
        otherwise
            error('psychnanovg:UnknownCommand', ...
                  'PsychNanoVGSetup: no option "%s"', cmd);
    end

    addpath(distdir, here);

    if strcmpi(cmd, 'add')
        f = fullfile(distdir, ['PsychNanoVG.' mexext()]);
        if exist(f, 'file') == 0
            error('psychnanovg:NotBuilt', ...
                  ['The PsychNanoVG MEX for this platform is missing.\n' ...
                   'Expected: %s\n' ...
                   'Build it with:  cd(''%s''); build'], f, root);
        end
    end

    if nargout > 0
        out = distdir;
    end
end

% ---------------------------------------------------------------------------

function a = pnvg_arch()
% The MATLAB names for the platform, used for the dist layout on both
% engines. Octave's computer('arch') reports a GNU triplet such as
% gnu-linux-x86_64 instead, so derive the name there.
    if exist('OCTAVE_VERSION', 'builtin') ~= 0
        if ispc
            a = 'win64';
        elseif ismac
            if isempty(strfind(lower(computer()), 'x86'))  %#ok<STREMP>
                a = 'maca64';
            else
                a = 'maci64';
            end
        else
            a = 'glnxa64';
        end
    else
        a = computer('arch');
    end
end
