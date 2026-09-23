function out = PsychNanoVGSetup(cmd, opt)
%PSYCHNANOVGSETUP  Put PsychNanoVG on the path, or take it off again.
%
%   PsychNanoVGSetup             adds dist/<arch> and m/ to the path
%   PsychNanoVGSetup('add', 'save')      adds them, then runs savepath
%   PsychNanoVGSetup('remove')   takes dist/<arch>, m/, and the package
%                                folder of this copy off the path
%   PsychNanoVGSetup('remove', 'save')   removes them, then runs savepath
%   a = PsychNanoVGSetup('arch') returns win64, glnxa64, maci64, or maca64
%   d = PsychNanoVGSetup('distdir')  returns dist/<arch>, path untouched
%   PsychNanoVGSetup('nocheck')  adds the path without checking for the MEX
%
%   The command form works too: PsychNanoVGSetup remove save
%
%   'remove' changes nothing when none of the three folders is on the path.
%   Otherwise it first unloads the MEX: PsychNanoVG('Shutdown', 'all'), which
%   unlocks it, then clear PsychNanoVG. Octave 10.1 crashes when the path
%   changes while a locked MEX file is loaded (SPEC section 14.4), so when
%   the MEX stays locked, 'remove' raises psychnanovg:Locked and leaves the
%   path as it was.
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
%   Two copies of this file exist, with the same content: one in the
%   package root and one in m/. The root copy lets a new user call it
%   before m/ is on the path. The m/ copy lets the helpers call it after.
%   A function cannot hand over to another file of its own name, because
%   the calling file and the current folder win the name lookup, so each
%   copy does the whole job. tests/run_tests checks that the two agree.
%
%   Example, from a release zip or a source checkout:
%       addpath('C:\toolbox\PsychNanoVG');
%       PsychNanoVGSetup();
%
%   See also PsychNanoVG, build.

    if nargin < 1
        cmd = 'add';
    end
    if nargin < 2
        opt = '';
    end
    if strcmpi(cmd, 'save')          % PsychNanoVGSetup save
        cmd = 'add';
        opt = 'save';
    end
    if ~isempty(opt) && ~strcmpi(opt, 'save')
        error('psychnanovg:UnknownCommand', ...
              'PsychNanoVGSetup: no option "%s"', opt);
    end
    dosave = strcmpi(opt, 'save');

    here = fileparts(mfilename('fullpath'));
    if exist(fullfile(here, 'm', 'PsychNanoVGOpen.m'), 'file') == 2
        root = here;                 % the copy in the package root
    else
        root = fileparts(here);      % the copy in m/
    end
    here = fullfile(root, 'm');      % the m directory, for both copies
    a = pnvg_arch();
    distdir = fullfile(root, 'dist', a);

    switch lower(cmd)
        case 'arch'
            out = a;
            return;
        case 'distdir'
            out = distdir;
            return;
        case 'remove'
            if remove_package({distdir, here, root}) && dosave
                save_path();
            end
            if nargout > 0
                out = distdir;
            end
            return;
        case {'add', 'nocheck'}
            % fall through
        otherwise
            error('psychnanovg:UnknownCommand', ...
                  'PsychNanoVGSetup: no option "%s"', cmd);
    end

    % Only touch the path when it is not already right. PsychNanoVGOpen calls
    % this on every open, and rewriting the load path while the MEX file is
    % loaded makes the interpreter rebuild its function cache for no reason.
    if ~path_is_ready(distdir, here)
        addpath(distdir, here);
    end

    if strcmpi(cmd, 'add')
        f = fullfile(distdir, ['PsychNanoVG.' mexext()]);
        if exist(f, 'file') == 0
            error('psychnanovg:NotBuilt', ...
                  ['The PsychNanoVG MEX for this platform is missing.\n' ...
                   'Expected: %s\n' ...
                   'Download the release zip for this engine and ' ...
                   'platform, or build it with:  cd(''%s''); build'], ...
                  f, root);
        end
    end

    if dosave
        save_path();
    end

    if nargout > 0
        out = distdir;
    end
end

% ---------------------------------------------------------------------------

function changed = remove_package(dirs)
% Takes the directories of this package off the path. The MEX has to be
% unloaded first, because a path change while a locked MEX file is loaded
% sends Octave 10.1 into an endless recursion.
    parts = strsplit(path(), pathsep);
    onpath = dirs(cellfun(@(d) any(strcmp(parts, d)), dirs));
    changed = ~isempty(onpath);
    if ~changed
        return;
    end

    if mislocked('PsychNanoVG')
        try
            PsychNanoVG('Shutdown', 'all');
        catch
            % The check below decides.
        end
    end
    clear('PsychNanoVG');
    if mislocked('PsychNanoVG')
        error('psychnanovg:Locked', ...
              ['PsychNanoVG is still loaded and locked, so the path was ' ...
               'not changed.\nClose your windows with sca, run ' ...
               'PsychNanoVG(''Shutdown'', ''all''), then clear PsychNanoVG, ' ...
               'and try again.\nIf that fails, restart MATLAB or Octave ' ...
               'and run PsychNanoVGSetup(''remove'') first.']);
    end

    rmpath(onpath{:});
end

function save_path()
% savepath reports failure through its status, for example when MATLAB may
% not write pathdef.m, so say so rather than let it pass unnoticed.
    if savepath() ~= 0
        warning('psychnanovg:SavePath', ...
                ['savepath could not save the path. Add the setup lines to ' ...
                 'startup.m or ~/.octaverc instead; see README.md.']);
    end
end

function tf = path_is_ready(distdir, mdir)
% True when both directories are on the path and dist comes first. A MEX file
% only takes precedence over an M-file of the same name inside one directory,
% so the order is what makes PsychNanoVG resolve to the MEX.
    parts = strsplit(path(), pathsep);
    di = find(strcmp(parts, distdir), 1);
    mi = find(strcmp(parts, mdir), 1);
    tf = ~isempty(di) && ~isempty(mi) && di < mi;
end

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
