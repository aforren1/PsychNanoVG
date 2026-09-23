function build(target)
%BUILD  Compile the PsychNanoVG MEX for MATLAB or Octave.
%
%   build          build the static library with CMake, then the MEX
%   build gen      run the binding generator, then build
%   build test     build, then run tests/run_tests
%   build smoke    build the native GL smoke test and run it
%   build clean    remove the build and install directories
%
%   The two engines use different compilers, so they get separate build and
%   install directories. MATLAB uses MSVC, Octave uses the MinGW gcc that it
%   ships with, and a static library from one cannot be linked by the other.
%
%   Set MEX_CMAKE_GENERATOR to override the CMake generator.
%
%   Set PSYCHNANOVG_TRACY=1 to compile the Tracy profiler client into the
%   library and the zones into the MEX (SPEC 9.3). Tracy is not vendored:
%   clone it into third_party/tracy first. Unset, the build is the normal
%   one, and the zones compile to nothing.

    if nargin < 1
        target = 'build';
    end

    here = fileparts(mfilename('fullpath'));
    old = cd(here);
    restore = onCleanup(@() cd(old));

    % PsychNanoVGSetup owns the platform name, so build.m and the scripts
    % agree on where the MEX for this platform lives.
    addpath(fullfile(here, 'm'));

    is_octave = exist('OCTAVE_VERSION', 'builtin') ~= 0;
    if is_octave
        tag = 'octave';
    else
        tag = 'matlab';
    end
    % The platform is part of the name because one working tree is often
    % shared between Windows and WSL, and an object file from one toolchain
    % makes CMake refuse to configure for the other.
    builddir = ['build-' tag platform_suffix()];
    instdir = ['inst-' tag platform_suffix()];

    switch lower(target)
        case 'clean'
            for t = {'matlab', 'octave', 'smoke'}
                rm_dir(['build-' t{1} platform_suffix()]);
                rm_dir(['inst-' t{1} platform_suffix()]);
            end
            fprintf('clean\n');
            return;
        case 'gen'
            run_generator(here);
        case {'build', 'test', 'smoke'}
            % nothing extra
        otherwise
            error('build:target', 'unknown target "%s"', target);
    end

    check_nanovg(here);
    tracy = tracy_enabled();
    if tracy
        check_tracy(here);
    end
    build_library(builddir, instdir, is_octave, false, tracy);
    libfile = find_library(instdir, is_octave);
    build_mex(libfile, is_octave, tracy);

    if strcmpi(target, 'test')
        addpath(fullfile(here, 'tests'));
        run_tests();
    elseif strcmpi(target, 'smoke')
        build_smoke(is_octave);
    end
end

% ---------------------------------------------------------------------------

function p = platform_suffix()
    if ispc
        p = '';
    elseif ismac
        p = '-mac';
    else
        p = '-linux';
    end
end

function check_nanovg(here)
    if exist(fullfile(here, 'third_party', 'nanovg', 'src', 'nanovg.c'), 'file')
        return;
    end
    error('build:nanovg', ['NanoVG is missing. Run:\n' ...
        '  git clone https://github.com/memononen/nanovg.git third_party/nanovg\n' ...
        'See third_party/PINS.md for the pinned commit.']);
end

function tf = tracy_enabled()
    v = lower(strtrim(getenv('PSYCHNANOVG_TRACY')));
    tf = any(strcmp(v, {'1', 'on', 'true', 'yes'}));
end

function check_tracy(here)
% CMake refuses too, but only after the configure step has run, and its
% message is buried in the CMake log. This one names the fix first.
    if exist(fullfile(here, 'third_party', 'tracy', 'public', ...
                      'TracyClient.cpp'), 'file')
        return;
    end
    error('build:tracy', ['PSYCHNANOVG_TRACY is set but Tracy is missing. Run:\n' ...
        '  git clone --branch v0.11.1 https://github.com/wolfpld/tracy.git third_party/tracy\n' ...
        'or unset PSYCHNANOVG_TRACY. See third_party/PINS.md.']);
end

function run_generator(here)
    uv = 'uv';
    local_uv = fullfile(getenv('USERPROFILE'), '.local', 'bin', 'uv.exe');
    if ispc && exist(local_uv, 'file')
        uv = ['"' local_uv '"'];
    end
    cmd = sprintf('%s run --project "%s" "%s"', uv, fullfile(here, 'gen'), ...
                  fullfile(here, 'gen', 'generate.py'));
    run_cmd(cmd);
end

function build_library(builddir, instdir, is_octave, smoke, tracy)
    if ~exist(builddir, 'dir'); mkdir(builddir); end
    cfg = ['cmake -E chdir ' builddir ' cmake' ...
           ' -DCMAKE_BUILD_TYPE=Release' ...
           ' -DCMAKE_INSTALL_PREFIX=../' instdir ...
           ' -DCMAKE_INSTALL_LIBDIR=lib'];
    if smoke
        cfg = [cfg ' -DPSYCHNANOVG_SMOKE_GL=ON'];
    end
    % Passed both ways, because the CMake cache would otherwise keep an ON
    % from an earlier Tracy build and link the profiler into this one.
    if tracy
        cfg = [cfg ' -DPSYCHNANOVG_TRACY=ON'];
    else
        cfg = [cfg ' -DPSYCHNANOVG_TRACY=OFF'];
    end

    gen = getenv('MEX_CMAKE_GENERATOR');
    cleanup = [];  %#ok<NASGU>
    if ~isempty(gen)
        cfg = [cfg ' -G "' gen '"'];
        if strcmpi(gen, 'MinGW Makefiles')
            cleanup = drop_sh_from_path();  %#ok<NASGU>
        end
    elseif ismac
        % clang is the only compiler on a Mac runner, for both engines, and
        % "Unix Makefiles" is the generator that suits it. Xcode would work
        % too but puts the archive in a per-configuration subdirectory.
        cfg = [cfg ' -G "Unix Makefiles"'];
    elseif ispc && is_octave
        % Octave on Windows is an MSYS2 tree: MinGW gcc in mingw64/bin, and
        % GNU make with sh in usr/bin. The archive has to come from that gcc
        % to be link compatible with mkoctfile. "Unix Makefiles" suits that
        % layout; "MinGW Makefiles" refuses to run while sh.exe is on PATH,
        % and Octave puts sh.exe there itself.
        root = octave_root();
        cfg = [cfg ' -G "Unix Makefiles"'];
        mk = fullfile(root, 'usr', 'bin', 'make.exe');
        cc = fullfile(root, 'mingw64', 'bin', 'gcc.exe');
        if exist(mk, 'file')
            cfg = [cfg ' -DCMAKE_MAKE_PROGRAM="' strrep(mk, '\', '/') '"'];
        end
        if ~exist(cc, 'file')
            cc = octave_cc();
        end
        if ~isempty(cc)
            cfg = [cfg ' -DCMAKE_C_COMPILER="' strrep(cc, '\', '/') '"'];
        end
        cleanup = add_to_path({fullfile(root, 'usr', 'bin'), ...
                               fullfile(root, 'mingw64', 'bin')});  %#ok<NASGU>
    end
    cfg = [cfg ' ..'];

    % A build directory left over from another generator makes CMake refuse to
    % reconfigure, so wipe it and try once more before giving up.
    if system(cfg) ~= 0
        fprintf('cmake configure failed; wiping %s and retrying\n', builddir);
        rm_dir(builddir);
        % A build tree made by another account, for instance by a Docker
        % container running as root on a shared mount, cannot be deleted
        % from here. Say so, because the retry would then fail on the
        % stale cache with a message that does not point at the cause.
        assert(exist(builddir, 'dir') == 0, 'build:stale', ...
               ['%s could not be removed. Delete it with the account ' ...
                'that created it, then build again.'], builddir);
        mkdir(builddir);
        run_cmd(cfg);
    end
    run_cmd(['cmake --build ' builddir ' --config Release']);
    run_cmd(['cmake --build ' builddir ' --target install --config Release']);
end

function cc = octave_cc()
    cc = '';
    try
        cc = strtrim(mkoctfile('-p', 'CC'));
    catch
        try
            [st, out] = system('mkoctfile -p CC');
            if st == 0; cc = strtrim(out); end
        catch
        end
    end
    % mkoctfile reports "gcc" plus flags on some builds; keep the program only.
    if ~isempty(cc)
        parts = strsplit(cc, ' ');
        cc = parts{1};
    end
end

function r = octave_root()
% OCTAVE_HOME points at the mingw64 prefix inside the MSYS2 tree on Windows,
% but make and sh live one level above it, in usr/bin.
    r = '';
    try
        r = OCTAVE_HOME();
    catch
    end
    [parent, leaf] = fileparts(r);
    if ~isempty(parent) && (strcmpi(leaf, 'mingw64') || strcmpi(leaf, 'mingw32'))
        r = parent;
    end
end

function c = add_to_path(dirs)
% CMake finds the compiler and the archiver by walking PATH, and the MSYS
% make needs its own DLLs beside it.
    old = getenv('PATH');
    add = {};
    for k = 1:numel(dirs)
        if exist(dirs{k}, 'dir'); add{end+1} = dirs{k}; end %#ok<AGROW>
    end
    if ~isempty(add)
        setenv('PATH', [strjoin(add, pathsep) pathsep old]);
    end
    c = onCleanup(@() setenv('PATH', old));
end

function c = drop_sh_from_path()
% CMake's "MinGW Makefiles" generator refuses to run while sh.exe is on PATH,
% which it is whenever Git Bash is installed. Hide those directories for the
% duration of the configure step.
    old = getenv('PATH');
    parts = strsplit(old, pathsep);
    keep = true(1, numel(parts));
    for k = 1:numel(parts)
        if isempty(parts{k}); continue; end
        if exist(fullfile(parts{k}, 'sh.exe'), 'file')
            keep(k) = false;
        end
    end
    if ~all(keep)
        setenv('PATH', strjoin(parts(keep), pathsep));
    end
    c = onCleanup(@() setenv('PATH', old));
end

function libfile = find_library(instdir, is_octave)
    if ispc && ~is_octave
        libfile = fullfile(instdir, 'lib', 'pnvg.lib');
    else
        libfile = fullfile(instdir, 'lib', 'libpnvg.a');
    end
    assert(exist(libfile, 'file') ~= 0, 'build:lib', ...
           'static library not found: %s', libfile);
end

function build_mex(libfile, is_octave, tracy)
    % dist is split by platform because Octave calls its MEX PsychNanoVG.mex
    % everywhere, so a Linux build would otherwise replace the Windows one in
    % a working tree that is shared with WSL.
    outdir = fullfile('dist', PsychNanoVGSetup('arch'));
    if ~exist(outdir, 'dir'); mkdir(outdir); end
    srcs = {fullfile('src', 'psychnanovg.c'), ...
            fullfile('src', 'gen_dispatch.c'), ...
            fullfile('src', 'gen_enums.c'), ...
            fullfile('src', 'pnvg_batch.c'), ...
            fullfile('src', 'pnvg_targets.c')};
    args = {'-I./src', '-I./third_party/nanovg/src', ...
            '-I./third_party/glad/include'};
    if is_octave
        % SPEC 6.3: Octave hands char arrays over as UTF-8 bytes, MATLAB as
        % UTF-16 code units, and the string marshaling differs accordingly.
        args{end+1} = '-DPNVG_OCTAVE=1';
    else
        % The classic mx* API, so one source builds for both engines.
        args{end+1} = '-R2017b';
    end
    if ismac
        % SPEC 4.1: Psychtoolbox makes legacy GL 2.1 contexts on macOS, so
        % CMake compiles the GL2 backend there. The MEX has to agree, because
        % it reports the backend and validates opts.renderer.
        args{end+1} = '-DPNVG_GL2=1';
    end
    if tracy
        % The same defines as the library, or the MEX and the core would
        % disagree about the profiler lifetime (see CMakeLists.txt).
        args = [args, {'-DPSYCHNANOVG_TRACY=1', '-DTRACY_ENABLE', ...
                       '-DTRACY_DELAYED_INIT', '-DTRACY_MANUAL_LIFETIME', ...
                       '-DTRACY_NO_CRASH_HANDLER', ...
                       '-I./third_party/tracy/public'}];
    end
    args = [args, srcs, {libfile}];
    if tracy
        % The Tracy client is C++ inside a C MEX, so the C++ runtime and
        % the libraries that the client uses have to be named here.
        if ispc
            args = [args, {'-lws2_32', '-ldbghelp', '-ladvapi32', '-luser32'}];
            if is_octave
                args{end+1} = '-lstdc++';
            end
        elseif ismac
            args{end+1} = '-lc++';
        else
            args = [args, {'-lstdc++', '-lpthread'}];
        end
    end
    if ispc
        args{end+1} = '-lopengl32';
    elseif ismac
        if is_octave
            % mkoctfile passes an argument it does not know straight to the
            % linker, so the two words arrive intact.
            args{end+1} = '-framework';
            args{end+1} = 'OpenGL';
        else
            % MATLAB's mex parses its own arguments and rejects a bare
            % -framework. The flag has to travel inside a build variable, as
            % one argument.
            args{end+1} = 'LDFLAGS=$LDFLAGS -framework OpenGL';
            if ~isempty(getenv('CI'))
                % The link line is the first thing to look at when a macOS
                % runner fails, and no one here has a Mac to try it on.
                args = [{'-v'}, args];
            end
        end
    else
        args{end+1} = '-lGL';
        args{end+1} = '-ldl';
    end
    args{end+1} = '-output';
    args{end+1} = fullfile(outdir, 'PsychNanoVG');
    mex(args{:});
    fprintf('build complete: %s.%s\n', fullfile(outdir, 'PsychNanoVG'), ...
            mexext());
end

function build_smoke(is_octave)
    d = ['build-smoke' platform_suffix()];
    build_library(d, ['inst-smoke' platform_suffix()], is_octave, true, ...
                  tracy_enabled());
    if ispc
        exe = fullfile(d, 'Release', 'smoke_gl.exe');
        if ~exist(exe, 'file')
            exe = fullfile(d, 'smoke_gl.exe');
        end
    else
        exe = fullfile(d, 'smoke_gl');
    end
    assert(exist(exe, 'file') ~= 0, 'build:smoke', 'smoke_gl not built');
    if ispc
        run_cmd(['"' exe '"']);
    else
        % A CI runner and a WSL session have no display of their own.
        run_cmd(['xvfb-run -a "' exe '" || "' exe '"']);
    end
end

function rm_dir(d)
    if exist(d, 'dir'); rmdir(d, 's'); end
end

function run_cmd(cmd)
    status = system(cmd);
    assert(status == 0, 'build:cmd', 'command failed (status %d): %s', ...
           status, cmd);
end
