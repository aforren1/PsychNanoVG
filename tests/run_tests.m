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
    addpath(fullfile(root, 'm'));
    % Raises psychnanovg:NotBuilt, naming the expected path, when the MEX for
    % this engine and platform is missing.
    PsychNanoVGSetup();
    addpath(here);

    v = PsychNanoVG('Version');
    fprintf('PsychNanoVG %s, NanoVG %s\n', v.psychnanovg, v.nanovg);
    fprintf('build: %s\n', v.build);
    if exist('OCTAVE_VERSION', 'builtin')
        fprintf('engine: Octave %s\n\n', version());
    else
        fprintf('engine: MATLAB %s\n\n', version());
    end

    suites = {'test_dispatch', 'test_gen_marshal', 'test_paths', ...
              'test_transforms'};
    for k = 1:numel(suites)
        fprintf('-- %s\n', suites{k});
        before = TST_FAIL;
        try
            feval(suites{k});
        catch e
            TST_FAIL = TST_FAIL + 1;
            fprintf(2, '  FAIL  %s raised %s: %s\n', suites{k}, ...
                    e.identifier, e.message);
            % A suite that dies part way leaves a context behind.
            try
                PsychNanoVG('Shutdown');
            catch
            end
        end
        if TST_FAIL == before
            fprintf('   ok\n');
        end
    end

    fprintf('\n-- tests/gl\n');
    if have_ptb()
        gl = {'test_gl_shapes', 'test_gl_text', 'test_gl_target'};
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
