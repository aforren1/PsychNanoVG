/* Batched path subcommands.
 *
 * SPEC 3: a per-vertex call from MATLAB costs one to three microseconds, so a
 * path of any size has to cross the MEX boundary once. These handlers take a
 * whole matrix and drive NanoVG from C.
 */

#include <string.h>

#include "pnvg_internal.h"

/* Validates an Nx`cols` real double or single matrix and reports its shape. */
static const void *matrix_arg(const mxArray *a, const char *cmd, int cols,
                              int *n, int *isSingle)
{
    const mwSize *d;
    if (!a || mxIsComplex(a) || !(mxIsDouble(a) || mxIsSingle(a)) ||
        mxGetNumberOfDimensions(a) != 2)
        pnvg_err("psychnanovg:Type",
                 "%s: the argument must be a real Nx%d double or single matrix",
                 cmd, cols);
    d = mxGetDimensions(a);
    if (d[1] != (mwSize)cols)
        pnvg_err("psychnanovg:Usage",
                 "%s: the argument must have %d columns, not %d",
                 cmd, cols, (int)d[1]);
    *n = (int)d[0];
    *isSingle = mxIsSingle(a) ? 1 : 0;
    return mxGetData(a);
}

static void polyline_common(int nrhs, const mxArray *prhs[], const char *cmd,
                            int close)
{
    int n = 0, isSingle = 0, st;
    const void *data = matrix_arg(prhs[0], cmd, 2, &n, &isSingle);
    if (nrhs > 1)
        close = pnvg_arg_int(prhs[1], 1, cmd) != 0;
    st = pnvg_polyline(data, n, isSingle, close);
    if (st != PNVG_OK)
        pnvg_err("psychnanovg:Usage", "%s failed", cmd);
}

void h_Polyline(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    (void)nlhs; (void)plhs;
    polyline_common(nrhs, prhs, "Polyline", 0);
}

void h_Polygon(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    (void)nlhs; (void)plhs;
    polyline_common(1, prhs, "Polygon", 1);
}

void h_Circles(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    int n = 0, isSingle = 0;
    const void *data = matrix_arg(prhs[0], "Circles", 3, &n, &isSingle);
    (void)nlhs; (void)plhs; (void)nrhs;
    pnvg_circles(data, n, isSingle);
}

void h_Rects(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    int n = 0, isSingle = 0;
    const void *data = matrix_arg(prhs[0], "Rects", 4, &n, &isSingle);
    (void)nlhs; (void)plhs; (void)nrhs;
    pnvg_rects(data, n, isSingle);
}

/* ------------------------------------------------------------------ */
/* Path                                                                */
/* ------------------------------------------------------------------ */

static double cell_num(const mxArray *row, int k, int rowIndex)
{
    const mxArray *e = mxGetCell(row, (mwIndex)k);
    if (!e || mxIsComplex(e) || !(mxIsDouble(e) || mxIsSingle(e)) ||
        mxGetNumberOfElements(e) != 1)
        pnvg_err("psychnanovg:Type",
                 "Path: element %d of entry %d must be a real scalar",
                 k + 1, rowIndex + 1);
    if (mxIsSingle(e))
        return (double)((const float *)mxGetData(e))[0];
    return ((const double *)mxGetData(e))[0];
}

static void path_cell(const mxArray *a)
{
    NVGcontext *vg = PNVG_VG;
    mwSize i, n = mxGetNumberOfElements(a);
    for (i = 0; i < n; i++) {
        const mxArray *row = mxGetCell(a, i);
        char op[8];
        mwSize nel;
        if (!row || !mxIsCell(row))
            pnvg_err("psychnanovg:Type",
                     "Path: entry %d must be a cell such as {'L', x, y}",
                     (int)i + 1);
        nel = mxGetNumberOfElements(row);
        if (nel < 1 || !mxIsChar(mxGetCell(row, 0)) ||
            mxGetString(mxGetCell(row, 0), op, sizeof(op)) != 0)
            pnvg_err("psychnanovg:Type",
                     "Path: entry %d must start with a command letter",
                     (int)i + 1);
        switch (op[0]) {
        case 'M': case 'm':
            if (nel != 3)
                pnvg_err("psychnanovg:Usage", "Path: M takes x and y");
            nvgMoveTo(vg, (float)cell_num(row, 1, (int)i),
                      (float)cell_num(row, 2, (int)i));
            break;
        case 'L': case 'l':
            if (nel != 3)
                pnvg_err("psychnanovg:Usage", "Path: L takes x and y");
            nvgLineTo(vg, (float)cell_num(row, 1, (int)i),
                      (float)cell_num(row, 2, (int)i));
            break;
        case 'Q': case 'q':
            if (nel != 5)
                pnvg_err("psychnanovg:Usage", "Path: Q takes cx, cy, x, y");
            nvgQuadTo(vg, (float)cell_num(row, 1, (int)i),
                      (float)cell_num(row, 2, (int)i),
                      (float)cell_num(row, 3, (int)i),
                      (float)cell_num(row, 4, (int)i));
            break;
        case 'C': case 'c':
            if (nel != 7)
                pnvg_err("psychnanovg:Usage",
                         "Path: C takes c1x, c1y, c2x, c2y, x, y");
            nvgBezierTo(vg, (float)cell_num(row, 1, (int)i),
                        (float)cell_num(row, 2, (int)i),
                        (float)cell_num(row, 3, (int)i),
                        (float)cell_num(row, 4, (int)i),
                        (float)cell_num(row, 5, (int)i),
                        (float)cell_num(row, 6, (int)i));
            break;
        case 'Z': case 'z':
            nvgClosePath(vg);
            break;
        default:
            pnvg_err("psychnanovg:Usage",
                     "Path: entry %d has command '%s', not M, L, Q, C, or Z",
                     (int)i + 1, op);
        }
    }
}

void h_Path(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    (void)nlhs; (void)plhs; (void)nrhs;
    if (mxIsCell(prhs[0])) {
        path_cell(prhs[0]);
        return;
    }
    {
        int n = 0, isSingle = 0, st;
        const void *data = matrix_arg(prhs[0], "Path", 7, &n, &isSingle);
        st = pnvg_path_matrix(data, n, isSingle);
        if (st != PNVG_OK)
            pnvg_err("psychnanovg:Range", "Path: %s", pnvg_last_error());
    }
}
