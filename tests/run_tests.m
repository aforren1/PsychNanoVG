function run_tests()
%RUN_TESTS  PsychNanoVG test suite for MATLAB and Octave, no GPU needed.
%
%   run_tests
%
%   Every test runs against the null renderer of SPEC 11.1: a NanoVG context
%   whose backend callbacks do nothing. Path building, the state stack, text
%   layout, handle tables, and all of the marshaling run for real; only the
%   GL calls are absent. The tests under tests/gl need Psychtoolbox and a
%   GPU, and report themselves as skipped when Screen is missing.

    global TST_PASS TST_FAIL %#ok<GVMIS>
    TST_PASS = 0;
    TST_FAIL = 0;

    here = fileparts(mfilename('fullpath'));
    root = fileparts(here);

    % Decide about Psychtoolbox before anything shadows Screen.
    ptb = have_ptb();

    addpath(fullfile(root, 'm'));
    % Raises psychnanovg:NotBuilt, naming the expected path, when the MEX for
    % this engine and platform is missing.
    PsychNanoVGSetup();
    addpath(here);

    % A release zip needs PsychNanoVGSetup in its root, before m/ is on the
    % path, and the helpers need it in m/. The two copies must not drift.
    tst('ok', 'PsychNanoVGSetup.m in the root and in m/ agree', ...
        isequal(fileread(fullfile(root, 'PsychNanoVGSetup.m')), ...
                fileread(fullfile(root, 'm', 'PsychNanoVGSetup.m'))));

    % The demos open their window through a private copy of the test helper,
    % because a release zip has no tests/. The bodies must not drift.
    tst('ok', 'the demo window helper and the test window helper agree', ...
        strcmp(helper_body(fullfile(here, 'gl', 'ptb_test_window.m')), ...
               helper_body(fullfile(root, 'm', 'private', ...
                                    'psychnanovg_demo_window.m'))));

    % test_setup removes and adds the package path, so it runs here, before
    % the first call loads the MEX file.
    fprintf('-- test_setup\n');
    before = TST_FAIL;
    test_setup(root);
    if TST_FAIL == before
        fprintf('   ok\n');
    end

    % test_helpers needs a Screen that records its calls. The stub goes on the
    % path here, once, before the first call loads the MEX file, and the test
    % itself never touches the path. Rewriting the load path while a MEX file
    % is loaded made Octave 10 on Linux crash; see SPEC section 14.
    addpath(fullfile(here, 'stub'));
    rehash_if_matlab();

    v = PsychNanoVG('Version');
    fprintf('PsychNanoVG %s, NanoVG %s\n', v.psychnanovg, v.nanovg);
    fprintf('build: %s\n', v.build);
    if exist('OCTAVE_VERSION', 'builtin')
        fprintf('engine: Octave %s\n\n', version());
    else
        fprintf('engine: MATLAB %s\n\n', version());
    end

    suites = {'test_dispatch', 'test_gen_marshal', 'test_paths', ...
              'test_transforms', 'test_contexts', 'test_helpers'};
    for k = 1:numel(suites)
        fprintf('-- %s\n', suites{k});
        before = TST_FAIL;
        try
            feval(suites{k});
        catch e
            TST_FAIL = TST_FAIL + 1;
            fprintf(2, '  FAIL  %s raised %s: %s\n', suites{k}, ...
                    e.identifier, e.message);
            % A suite that dies part way leaves contexts behind.
            try
                PsychNanoVG('Shutdown', 'all');
            catch
            end
        end
        if TST_FAIL == before
            fprintf('   ok\n');
        end
    end

    fprintf('\n-- tests/gl\n');
    if ptb
        % The real Screen has to come back before the GL tests run. This is
        % the only path change after the MEX loaded, and it happens on a
        % machine with Psychtoolbox, never on the Linux build that crashed.
        rmpath(fullfile(here, 'stub'));
        rehash_if_matlab();
        gl = {'test_gl_shapes', 'test_gl_text', 'test_gl_target', ...
              'test_gl_paths', 'test_gl_contexts'};
        addpath(fullfile(here, 'gl'));
        for k = 1:numel(gl)
            feval(gl{k});
        end
    else
        fprintf('   skipped: Psychtoolbox is not installed (no Screen)\n');
    end

    fprintf('\n==== %d passed, %d failed ====\n', TST_PASS, TST_FAIL);
    if TST_FAIL > 0
        error('run_tests:failed', '%d test(s) failed', TST_FAIL);
    end
end

function body = helper_body(file)
% The code after the help text; the two copies differ only in name and help.
    txt = fileread(file);
    k = strfind(txt, 'if nargin < 1; w = 640; end');
    body = txt(k(1):end);
end

function rehash_if_matlab()
% MATLAB caches which file a name resolves to, so the stub needs a rehash to
% take over from a real Screen MEX and to give it back. Octave looks the file
% up each time and has no rehash of this kind.
    if exist('OCTAVE_VERSION', 'builtin') == 0
        rehash;
    end
end

function tf = have_ptb()
% A Psychtoolbox source tree on the path puts Screen.m within reach of
% exist() even when the Screen MEX was never built, so ask Screen itself.
    tf = false;
    if exist('Screen', 'file') == 0
        return;
    end
    try
        Screen('Version');
        tf = true;
    catch
        tf = false;
    end
end
