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

/* The cell form names each command. The single letters are the SVG-like
 * names that the matrix codes 1 to 5 started with; the others are the
 * NanoVG names without the prefix, because SVG's own arc command has a
 * different meaning. nargs counts the elements after the name. */
typedef struct {
    const char *name;
    int code;
    int nargs;
    const char *usage;
} path_op;

static const path_op g_pathops[] = {
    {"M", PNVG_PATH_M, 2, "M takes x and y"},
    {"L", PNVG_PATH_L, 2, "L takes x and y"},
    {"Q", PNVG_PATH_Q, 4, "Q takes cx, cy, x, y"},
    {"C", PNVG_PATH_C, 6, "C takes c1x, c1y, c2x, c2y, x, y"},
    {"Z", PNVG_PATH_Z, 0, "Z takes no arguments"},
    {"Arc", PNVG_PATH_ARC, 6, "Arc takes cx, cy, r, a0, a1, dir"},
    {"ArcTo", PNVG_PATH_ARCTO, 5, "ArcTo takes x1, y1, x2, y2, r"},
    {"Ellipse", PNVG_PATH_ELLIPSE, 4, "Ellipse takes cx, cy, rx, ry"},
    {"Circle", PNVG_PATH_CIRCLE, 3, "Circle takes cx, cy, r"},
    {"Rect", PNVG_PATH_RECT, 4, "Rect takes x, y, w, h"},
    {"RoundedRect", PNVG_PATH_ROUNDEDRECT, 5,
     "RoundedRect takes x, y, w, h, r"},
    {"Winding", PNVG_PATH_WINDING, 1, "Winding takes dir"},
};

static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char y = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (x != y)
            return 0;
    }
    return *a == *b;
}

/* dir accepts a number or a constant name, as the Arc and PathWinding
 * subcommands do, and must end up as one of NanoVG's two directions. */
static int cell_dir(const mxArray *row, int k, int rowIndex)
{
    int d = pnvg_arg_enum(mxGetCell(row, (mwIndex)k), k, "Path");
    if (d != 1 && d != 2)
        pnvg_err("psychnanovg:Range",
                 "Path: entry %d: dir must be 1 (NVG_CCW, NVG_SOLID) or 2 "
                 "(NVG_CW, NVG_HOLE), not %d", rowIndex + 1, d);
    return d;
}

static void path_cell(const mxArray *a)
{
    NVGcontext *vg = PNVG_VG;
    mwSize i, n = mxGetNumberOfElements(a);
    for (i = 0; i < n; i++) {
        const mxArray *row = mxGetCell(a, i);
        const path_op *op = NULL;
        char name[16];
        float v[6];
        mwSize nel;
        int k, r = (int)i;
        if (!row || !mxIsCell(row))
            pnvg_err("psychnanovg:Type",
                     "Path: entry %d must be a cell such as {'L', x, y}",
                     r + 1);
        nel = mxGetNumberOfElements(row);
        if (nel < 1 || !mxIsChar(mxGetCell(row, 0)))
            pnvg_err("psychnanovg:Type",
                     "Path: entry %d must start with a command name", r + 1);
        if (mxGetString(mxGetCell(row, 0), name, sizeof(name)) == 0) {
            for (k = 0; k < (int)(sizeof(g_pathops) / sizeof(g_pathops[0]));
                 k++) {
                if (ieq(name, g_pathops[k].name)) {
                    op = &g_pathops[k];
                    break;
                }
            }
        }
        if (!op)
            pnvg_err("psychnanovg:Usage",
                     "Path: entry %d has command '%s', not M, L, Q, C, Z, "
                     "Arc, ArcTo, Ellipse, Circle, Rect, RoundedRect, or "
                     "Winding", r + 1, name);
        if ((int)nel != op->nargs + 1)
            pnvg_err("psychnanovg:Usage", "Path: entry %d: %s", r + 1,
                     op->usage);
        /* The direction is the last argument of Arc and the only one of
         * Winding, and it may be a name, so it is read on its own. */
        for (k = 0; k < op->nargs; k++) {
            if ((op->code == PNVG_PATH_ARC && k == 5) ||
                op->code == PNVG_PATH_WINDING)
                v[k] = (float)cell_dir(row, k + 1, r);
            else
                v[k] = (float)cell_num(row, k + 1, r);
        }
        switch (op->code) {
        case PNVG_PATH_M:
            nvgMoveTo(vg, v[0], v[1]);
            break;
        case PNVG_PATH_L:
            nvgLineTo(vg, v[0], v[1]);
            break;
        case PNVG_PATH_Q:
            nvgQuadTo(vg, v[0], v[1], v[2], v[3]);
            break;
        case PNVG_PATH_C:
            nvgBezierTo(vg, v[0], v[1], v[2], v[3], v[4], v[5]);
            break;
        case PNVG_PATH_Z:
            nvgClosePath(vg);
            break;
        case PNVG_PATH_ARC:
            nvgArc(vg, v[0], v[1], v[2], v[3], v[4], (int)v[5]);
            break;
        case PNVG_PATH_ARCTO:
            nvgArcTo(vg, v[0], v[1], v[2], v[3], v[4]);
            break;
        case PNVG_PATH_ELLIPSE:
            nvgEllipse(vg, v[0], v[1], v[2], v[3]);
            break;
        case PNVG_PATH_CIRCLE:
            nvgCircle(vg, v[0], v[1], v[2]);
            break;
        case PNVG_PATH_RECT:
            nvgRect(vg, v[0], v[1], v[2], v[3]);
            break;
        case PNVG_PATH_ROUNDEDRECT:
            nvgRoundedRect(vg, v[0], v[1], v[2], v[3], v[4]);
            break;
        default:
            nvgPathWinding(vg, (int)v[0]);
            break;
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
            pnvg_raise(st, "Path");
    }
}

/* ------------------------------------------------------------------ */
/* StrokeSegments                                                      */
/* ------------------------------------------------------------------ */

void h_StrokeSegments(int nlhs, mxArray *plhs[], int nrhs,
                      const mxArray *prhs[])
{
    int n = 0, nc = 0, segSingle = 0, colSingle = 0, st;
    const void *seg = matrix_arg(prhs[0], "StrokeSegments", 4, &n,
                                 &segSingle);
    const void *col = matrix_arg(prhs[1], "StrokeSegments", 8, &nc,
                                 &colSingle);
    (void)nlhs; (void)plhs; (void)nrhs;
    if (nc != n)
        pnvg_err("psychnanovg:Usage",
                 "StrokeSegments: %d segments but %d color rows; the two "
                 "matrices need one row per segment", n, nc);
    st = pnvg_stroke_segments(seg, segSingle, col, colSingle, n);
    if (st != PNVG_OK)
        pnvg_raise(st, "StrokeSegments");
}
