function tst(op, name, varargin)
%TST  Assertion harness shared by the PsychNanoVG test files.
%
%   tst('ok',     name, cond)
%   tst('eq',     name, got, want)
%   tst('near',   name, got, want, tol)
%   tst('throws', name, id, fn)
%
%   The counters are globals and the helper is one file rather than a nested
%   function, because Octave and MATLAB agree on that and disagree about
%   nested function workspaces.

    global TST_PASS TST_FAIL %#ok<GVMIS>
    if isempty(TST_PASS); TST_PASS = 0; end
    if isempty(TST_FAIL); TST_FAIL = 0; end

    switch lower(op)
        case 'ok'
            pass = logical(varargin{1});
            report(name, pass, '');
        case 'eq'
            got = varargin{1};
            want = varargin{2};
            pass = isequaln(got, want);
            report(name, pass, describe(got, want));
        case 'near'
            got = varargin{1};
            want = varargin{2};
            tol = varargin{3};
            pass = isequal(size(got), size(want)) && ...
                   all(abs(double(got(:)) - double(want(:))) <= tol);
            report(name, pass, describe(got, want));
        case 'throws'
            id = varargin{1};
            fn = varargin{2};
            threw = false;
            gotId = '';
            try
                fn();
            catch e
                threw = true;
                gotId = e.identifier;
            end
            pass = threw && strcmp(gotId, id);
            if ~threw
                report(name, false, 'did not throw');
            else
                report(name, pass, sprintf('got id "%s"', gotId));
            end
        otherwise
            error('tst:op', 'unknown assertion "%s"', op);
    end
end

function report(name, pass, detail)
    global TST_PASS TST_FAIL %#ok<GVMIS>
    if pass
        TST_PASS = TST_PASS + 1;
    else
        TST_FAIL = TST_FAIL + 1;
        fprintf(2, '  FAIL  %s\n', name);
        if ~isempty(detail)
            fprintf(2, '        %s\n', detail);
        end
    end
end

function s = describe(got, want)
    s = sprintf('got %s, wanted %s', short(got), short(want));
end

function s = short(v)
    if ischar(v)
        s = ['''' v ''''];
    elseif isnumeric(v) || islogical(v)
        if numel(v) <= 8
            s = ['[' strtrim(sprintf('%g ', double(v))) ']'];
        else
            s = sprintf('%s %s', mat2str(size(v)), class(v));
        end
    else
        s = class(v);
    end
end
