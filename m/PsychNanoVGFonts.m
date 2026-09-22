function out = PsychNanoVGFonts(cmd, varargin)
%PSYCHNANOVGFONTS  Font file lookup for PsychNanoVG.
%
%   path = PsychNanoVGFonts('FindSystemFont', family)
%   dirs = PsychNanoVGFonts('FontDirs')
%
%   FindSystemFont searches the operating system font directories for a TTF
%   or OTF file whose name matches `family`, and returns '' when there is
%   none. PsychNanoVG('FindSystemFont', family) calls this function, so a
%   site can add its own directories here without rebuilding the MEX.
%
%   Example:
%       font = PsychNanoVG('CreateFont', 'sans', ...
%                          PsychNanoVG('FindSystemFont', 'Arial'));
%
%   See also PsychNanoVG.

    if nargin < 1
        cmd = 'FindSystemFont';
    end

    switch lower(cmd)
        case 'findsystemfont'
            if isempty(varargin)
                error('psychnanovg:Usage', 'FindSystemFont needs a family name');
            end
            out = find_font(varargin{1});
        case 'fontdirs'
            out = font_dirs();
        otherwise
            error('psychnanovg:UnknownCommand', ...
                  'PsychNanoVGFonts: no subcommand "%s"', cmd);
    end
end

% ---------------------------------------------------------------------------

function dirs = font_dirs()
% SPEC 6.3 lists these. The order decides which copy of a family wins.
    if ispc
        win = getenv('WINDIR');
        if isempty(win); win = 'C:\Windows'; end
        local = getenv('LOCALAPPDATA');
        dirs = {fullfile(win, 'Fonts')};
        if ~isempty(local)
            dirs{end+1} = fullfile(local, 'Microsoft', 'Windows', 'Fonts');
        end
    elseif ismac
        dirs = {'/System/Library/Fonts', ...
                '/System/Library/Fonts/Supplemental', ...
                '/Library/Fonts', ...
                fullfile(getenv('HOME'), 'Library', 'Fonts')};
    else
        dirs = {'/usr/share/fonts', '/usr/local/share/fonts', ...
                fullfile(getenv('HOME'), '.fonts'), ...
                fullfile(getenv('HOME'), '.local', 'share', 'fonts')};
    end
end

function p = find_font(family)
    p = '';
    if ~ischar(family) || isempty(family)
        error('psychnanovg:Usage', 'FindSystemFont needs a family name');
    end

    % An exact file path is accepted unchanged, so a caller can pass either.
    if exist(family, 'file') == 2
        p = family;
        return;
    end

    want = normalize(family);
    best = '';
    bestScore = inf;
    dirs = font_dirs();
    for k = 1:numel(dirs)
        files = list_fonts(dirs{k});
        for f = 1:numel(files)
            [~, base, ~] = fileparts(files{f});
            got = normalize(base);
            if strcmp(got, want)
                p = files{f};
                return;
            end
            % A prefix match keeps 'Arial' from picking 'ArialBlack' when
            % plain 'arial.ttf' exists, because the shorter name scores lower.
            if length(got) > length(want) && strncmp(got, want, length(want))
                score = length(got) - length(want);
                if score < bestScore
                    bestScore = score;
                    best = files{f};
                end
            end
        end
    end
    p = best;
end

function files = list_fonts(root)
% Walks down three levels, which reaches the Linux layout
% /usr/share/fonts/truetype/<family>/<file>.ttf. A depth limit keeps a
% misplaced symbolic link from turning the search into a full disk walk, and
% dir('**') is a MATLAB extension that Octave does not have.
    files = walk(root, 3);
end

function files = walk(root, depth)
    files = {};
    if depth < 1 || exist(root, 'dir') ~= 7
        return;
    end
    files = files_in(root);
    d = dir(root);
    for k = 1:numel(d)
        if ~d(k).isdir || strcmp(d(k).name, '.') || strcmp(d(k).name, '..')
            continue;
        end
        files = [files, walk(fullfile(root, d(k).name), depth - 1)]; %#ok<AGROW>
    end
end

function out = files_in(p)
    out = {};
    for ext = {'*.ttf', '*.otf', '*.ttc'}
        d = dir(fullfile(p, ext{1}));
        for k = 1:numel(d)
            if ~d(k).isdir
                out{end+1} = fullfile(p, d(k).name); %#ok<AGROW>
            end
        end
    end
end

function s = normalize(s)
    s = lower(s);
    s = s(s ~= ' ' & s ~= '-' & s ~= '_');
end
