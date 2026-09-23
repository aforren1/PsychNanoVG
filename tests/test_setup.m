function test_setup(root)
%TEST_SETUP  PsychNanoVGSetup: remove, a second remove, and add again.
%
%   run_tests calls this before the first call loads the MEX file, because a
%   path change while a locked MEX file is loaded crashes Octave 10.1 (SPEC
%   section 14.4). 'save' is not tested: it would rewrite the saved path of
%   the machine that runs the suite.

    mdir = fullfile(root, 'm');
    distdir = PsychNanoVGSetup('distdir');

    tst('ok', 'setup test runs before the MEX is locked', ...
        ~mislocked('PsychNanoVG'));
    tst('throws', 'remove with an unknown option', ...
        'psychnanovg:UnknownCommand', ...
        @() PsychNanoVGSetup('remove', 'nosuchoption'));

    PsychNanoVGSetup('remove');
    tst('ok', 'remove takes dist/<arch> off the path', ~on_path(distdir));
    tst('ok', 'remove takes m/ off the path', ~on_path(mdir));
    tst('ok', 'the package root is not on the path', ~on_path(root));

    % The m/ copy is off the path now. Use the root copy from the package
    % folder, which is what a user does after a remove.
    old = cd(root);
    restore = onCleanup(@() cd(old));

    before = path();
    PsychNanoVGSetup('remove');
    tst('ok', 'a second remove leaves the path as it was', ...
        strcmp(path(), before));

    PsychNanoVGSetup();
    parts = strsplit(path(), pathsep);
    di = find(strcmp(parts, distdir), 1);
    mi = find(strcmp(parts, mdir), 1);
    tst('ok', 'add puts dist/<arch> back ahead of m/', ...
        ~isempty(di) && ~isempty(mi) && di < mi);
    tst('ok', 'add does not put the package root on the path', ...
        ~on_path(root));
end

function tf = on_path(d)
    tf = any(strcmp(strsplit(path(), pathsep), d));
end
