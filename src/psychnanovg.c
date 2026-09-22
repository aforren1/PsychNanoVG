/* psychnanovg: the MEX entry point, dispatch, marshaling, and lifecycle.
 *
 * Build with build.m. The generated command table lives in gen_dispatch.c and
 * the enum table in gen_enums.c; both come from nanovg.h through
 * gen/generate.py.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "pnvg_internal.h"

#define PNVG_MAX_CMDS 256
#define PNVG_NAME_MAX 64

/* SPEC 9.1: the version of the binding itself, separate from the NanoVG
 * commit that the generator stamps into pnvg_version_string. */
#define PNVG_VERSION "0.1.0"

/* ------------------------------------------------------------------ */
/* Errors                                                              */
/* ------------------------------------------------------------------ */

void pnvg_err(const char *id, const char *fmt, ...)
{
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    mexErrMsgIdAndTxt(id, "%s", msg);
}

/* Maps a core status code onto the identifier table of SPEC 5.5. Shared with
 * the hand-written handlers in pnvg_targets.c, so that a core status keeps
 * its identifier no matter which handler reports it. */
void pnvg_raise(int status, const char *cmd)
{
    const char *id;
    switch (status) {
    case PNVG_E_NOTINIT:      id = "psychnanovg:NotInit"; break;
    case PNVG_E_ALREADYINIT:  id = "psychnanovg:AlreadyInit"; break;
    case PNVG_E_NOGLCONTEXT:  id = "psychnanovg:NoGLContext"; break;
    case PNVG_E_GLINIT:       id = "psychnanovg:GLInit"; break;
    case PNVG_E_GLERROR:      id = "psychnanovg:GLError"; break;
    case PNVG_E_FRAMESTATE:   id = "psychnanovg:FrameState"; break;
    case PNVG_E_HANDLE:       id = "psychnanovg:Handle"; break;
    case PNVG_E_RANGE:        id = "psychnanovg:Range"; break;
    default:                  id = "psychnanovg:Usage"; break;
    }
    pnvg_err(id, "%s: %s", cmd, pnvg_last_error());
}

/* ------------------------------------------------------------------ */
/* Argument readers                                                    */
/* ------------------------------------------------------------------ */

static int is_num(const mxArray *a)
{
    return a && !mxIsComplex(a) &&
           (mxIsDouble(a) || mxIsSingle(a) || mxIsLogical(a) ||
            mxIsInt32(a) || mxIsUint32(a) || mxIsInt8(a) || mxIsUint8(a) ||
            mxIsInt16(a) || mxIsUint16(a) || mxIsInt64(a) || mxIsUint64(a));
}

static double num_at(const mxArray *a, size_t i)
{
    /* Every numeric class is read straight from the data pointer. An earlier
     * version sent the rare integer classes through mexCallMATLAB("double"),
     * which meant the interpreter could run inside an argument reader. That
     * is a poor place for reentrancy, and it is not needed: mxGetData plus
     * the class test covers all of them. */
    const void *d = mxGetData(a);
    if (mxIsDouble(a))
        return ((const double *)d)[i];
    if (mxIsSingle(a))
        return (double)((const float *)d)[i];
    if (mxIsLogical(a))
        return ((const mxLogical *)d)[i] ? 1.0 : 0.0;
    if (mxIsInt8(a))
        return (double)((const signed char *)d)[i];
    if (mxIsUint8(a))
        return (double)((const unsigned char *)d)[i];
    if (mxIsInt16(a))
        return (double)((const short *)d)[i];
    if (mxIsUint16(a))
        return (double)((const unsigned short *)d)[i];
    if (mxIsInt32(a))
        return (double)((const int *)d)[i];
    if (mxIsUint32(a))
        return (double)((const unsigned int *)d)[i];
    if (mxIsInt64(a))
        return (double)((const long long *)d)[i];
    if (mxIsUint64(a))
        return (double)((const unsigned long long *)d)[i];
    return 0.0;
}

double pnvg_arg_double(const mxArray *a, int i, const char *cmd)
{
    if (!is_num(a) || mxGetNumberOfElements(a) != 1)
        pnvg_err("psychnanovg:Type",
                 "%s: argument %d must be a real numeric scalar", cmd, i + 1);
    return num_at(a, 0);
}

float pnvg_arg_float(const mxArray *a, int i, const char *cmd)
{
    double v = pnvg_arg_double(a, i, cmd);
    if (v != v)
        pnvg_err("psychnanovg:Range", "%s: argument %d is NaN", cmd, i + 1);
    return (float)v;
}

int pnvg_arg_int(const mxArray *a, int i, const char *cmd)
{
    double v = pnvg_arg_double(a, i, cmd);
    if (v != v || v > 2147483647.0 || v < -2147483648.0)
        pnvg_err("psychnanovg:Range",
                 "%s: argument %d does not fit in an int", cmd, i + 1);
    return (int)v;
}

unsigned char pnvg_arg_uchar(const mxArray *a, int i, const char *cmd)
{
    double v = pnvg_arg_double(a, i, cmd);
    if (v < 0.0 || v > 255.0)
        pnvg_err("psychnanovg:Range",
                 "%s: argument %d must be 0 to 255", cmd, i + 1);
    return (unsigned char)(v + 0.5);
}

int pnvg_enum_lookup(const char *name, int *ok)
{
    int lo = 0, hi = pnvg_nenums - 1;
    char buf[PNVG_NAME_MAX];
    *ok = 1;
    if (strncmp(name, "NVG_", 4) != 0) {
        snprintf(buf, sizeof(buf), "NVG_%s", name);
        name = buf;
    }
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strcmp(pnvg_enums[mid].name, name);
        if (c == 0)
            return pnvg_enums[mid].value;
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    *ok = 0;
    return 0;
}

static int enum_from_string(const char *s, const char *cmd)
{
    char part[PNVG_NAME_MAX];
    int value = 0;
    const char *p = s;
    while (*p) {
        const char *bar = strchr(p, '|');
        size_t n = bar ? (size_t)(bar - p) : strlen(p);
        size_t a = 0, b = n;
        int ok = 0;
        while (a < b && (p[a] == ' ' || p[a] == '\t')) a++;
        while (b > a && (p[b - 1] == ' ' || p[b - 1] == '\t')) b--;
        if (b - a >= sizeof(part))
            pnvg_err("psychnanovg:Range", "%s: enum name is too long", cmd);
        memcpy(part, p + a, b - a);
        part[b - a] = '\0';
        value |= pnvg_enum_lookup(part, &ok);
        if (!ok)
            pnvg_err("psychnanovg:Range", "%s: no NanoVG constant named %s",
                     cmd, part);
        if (!bar)
            break;
        p = bar + 1;
    }
    return value;
}

int pnvg_arg_enum(const mxArray *a, int i, const char *cmd)
{
    if (mxIsChar(a)) {
        char buf[256];
        if (mxGetString(a, buf, sizeof(buf)) != 0)
            pnvg_err("psychnanovg:Range", "%s: argument %d is too long",
                     cmd, i + 1);
        return enum_from_string(buf, cmd);
    }
    return pnvg_arg_int(a, i, cmd);
}

int pnvg_arg_image(const mxArray *a, int i, const char *cmd)
{
    int id = pnvg_arg_int(a, i, cmd);
    if (!pnvg_image_is_live(id))
        pnvg_err("psychnanovg:Handle",
                 "%s: %d is not an open image handle", cmd, id);
    return id;
}

int pnvg_arg_font(const mxArray *a, int i, const char *cmd)
{
    int id = pnvg_arg_int(a, i, cmd);
    if (!pnvg_font_is_live(id))
        pnvg_err("psychnanovg:Handle",
                 "%s: %d is not an open font handle", cmd, id);
    return id;
}

NVGcolor pnvg_arg_color(const mxArray *a, int i, const char *cmd)
{
    size_t n = a ? mxGetNumberOfElements(a) : 0;
    float c[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    size_t k;
    if (!is_num(a) || (n != 3 && n != 4))
        pnvg_err("psychnanovg:Type",
                 "%s: argument %d must be a 1x3 or 1x4 color in 0 to 1",
                 cmd, i + 1);
    for (k = 0; k < n; k++)
        c[k] = (float)num_at(a, k);
    return nvgRGBAf(c[0], c[1], c[2], c[3]);
}

int pnvg_paint_store(const NVGpaint *p)
{
    int idx = pnvg_paint_alloc(p);
    if (idx < 0)
        pnvg_err("psychnanovg:Range",
                 "the paint table holds %d entries and all are in use; "
                 "free one with PaintDelete", PNVG_MAX_PAINTS);
    return idx;
}

NVGpaint pnvg_arg_paint(const mxArray *a, int i, const char *cmd)
{
    NVGpaint p;
    int idx = pnvg_arg_int(a, i, cmd);
    if (pnvg_paint_fetch(idx, &p) != PNVG_OK)
        pnvg_err("psychnanovg:Handle",
                 "%s: %d is not an open paint handle", cmd, idx);
    return p;
}

void pnvg_arg_xform(const mxArray *a, int i, const char *cmd, float *dst6)
{
    size_t k;
    if (!is_num(a) || mxGetNumberOfElements(a) != 6)
        pnvg_err("psychnanovg:Type",
                 "%s: argument %d must be a 1x6 transform", cmd, i + 1);
    for (k = 0; k < 6; k++)
        dst6[k] = (float)num_at(a, k);
}

const unsigned char *pnvg_arg_bytes(const mxArray *a, int i, const char *cmd,
                                    int *n)
{
    if (!a || !mxIsUint8(a) || mxIsComplex(a))
        pnvg_err("psychnanovg:Type",
                 "%s: argument %d must be a uint8 vector", cmd, i + 1);
    *n = (int)mxGetNumberOfElements(a);
    return (const unsigned char *)mxGetData(a);
}

const unsigned char *pnvg_arg_image_data(const mxArray *a, int i,
                                         const char *cmd, int *w, int *h)
{
    const mwSize *d;
    mwSize nd;
    const unsigned char *out;
    if (!a || !mxIsUint8(a) || mxIsComplex(a))
        pnvg_err("psychnanovg:Type",
                 "%s: argument %d must be an HxWx4 uint8 array", cmd, i + 1);
    nd = mxGetNumberOfDimensions(a);
    d = mxGetDimensions(a);
    if (nd != 3 || d[2] != 4)
        pnvg_err("psychnanovg:Type",
                 "%s: argument %d must be HxWx4 uint8 (RGBA)", cmd, i + 1);
    *h = (int)d[0];
    *w = (int)d[1];
    /* SPEC 7.2: MATLAB stores the array column major and NanoVG reads rows of
     * RGBA, so the pixels go through the scratch buffer. */
    out = pnvg_image_transpose((const unsigned char *)mxGetData(a), *h, *w);
    if (!out)
        pnvg_err("psychnanovg:Usage",
                 "%s: could not size the image scratch buffer", cmd);
    return out;
}

/* ------------------------------------------------------------------ */
/* Strings                                                             */
/* ------------------------------------------------------------------ */

void pnvg_str_free(pnvg_str *s)
{
    if (s->heap && s->p) {
        mxFree(s->p);
        s->p = NULL;
        s->heap = 0;
    }
}

#ifndef PNVG_OCTAVE
static size_t utf16_to_utf8(const mxChar *in, size_t nin, char *out,
                            size_t cap)
{
    size_t i, o = 0;
    for (i = 0; i < nin; i++) {
        unsigned int cp = in[i];
        if (cp >= 0xD800u && cp <= 0xDBFFu && i + 1 < nin &&
            in[i + 1] >= 0xDC00u && in[i + 1] <= 0xDFFFu) {
            cp = 0x10000u + ((cp - 0xD800u) << 10) + (in[i + 1] - 0xDC00u);
            i++;
        }
        if (cp < 0x80u) {
            if (o + 1 > cap) return (size_t)-1;
            out[o++] = (char)cp;
        } else if (cp < 0x800u) {
            if (o + 2 > cap) return (size_t)-1;
            out[o++] = (char)(0xC0u | (cp >> 6));
            out[o++] = (char)(0x80u | (cp & 0x3Fu));
        } else if (cp < 0x10000u) {
            if (o + 3 > cap) return (size_t)-1;
            out[o++] = (char)(0xE0u | (cp >> 12));
            out[o++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
            out[o++] = (char)(0x80u | (cp & 0x3Fu));
        } else {
            if (o + 4 > cap) return (size_t)-1;
            out[o++] = (char)(0xF0u | (cp >> 18));
            out[o++] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
            out[o++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
            out[o++] = (char)(0x80u | (cp & 0x3Fu));
        }
    }
    return o;
}
#endif

void pnvg_arg_string(const mxArray *a, int i, const char *cmd, pnvg_str *out)
{
    size_t nin;
    const mxChar *in;

    out->p = out->stack;
    out->n = 0;
    out->heap = 0;
    out->stack[0] = '\0';

    if (!a || !mxIsChar(a))
        pnvg_err("psychnanovg:Type",
                 "%s: argument %d must be a char string", cmd, i + 1);
    nin = mxGetNumberOfElements(a);
    in = (const mxChar *)mxGetData(a);

#ifdef PNVG_OCTAVE
    /* SPEC 6.3: Octave already hands over UTF-8 bytes, one per mxChar. */
    {
        size_t k;
        char *dst = out->stack;
        if (nin + 1 > PNVG_STR_STACK) {
            dst = (char *)mxMalloc(nin + 1);
            out->p = dst;
            out->heap = 1;
        }
        for (k = 0; k < nin; k++)
            dst[k] = (char)(in[k] & 0xFFu);
        dst[nin] = '\0';
        out->n = nin;
    }
#else
    {
        size_t n = utf16_to_utf8(in, nin, out->stack, PNVG_STR_STACK - 1);
        if (n == (size_t)-1) {
            /* Worst case four UTF-8 bytes per UTF-16 unit. mxMalloc memory is
             * released by MATLAB even if a later check raises. */
            size_t cap = nin * 4 + 1;
            char *heap = (char *)mxMalloc(cap);
            n = utf16_to_utf8(in, nin, heap, cap - 1);
            if (n == (size_t)-1)
                pnvg_err("psychnanovg:Usage", "%s: string conversion failed",
                         cmd);
            heap[n] = '\0';
            out->p = heap;
            out->heap = 1;
        } else {
            out->stack[n] = '\0';
        }
        out->n = n;
    }
#endif
}

/* ------------------------------------------------------------------ */
/* Output builders                                                     */
/* ------------------------------------------------------------------ */

mxArray *pnvg_make_row(const float *v, int n)
{
    mxArray *a = mxCreateDoubleMatrix(1, (mwSize)n, mxREAL);
    double *d = (double *)mxGetData(a);
    int i;
    for (i = 0; i < n; i++)
        d[i] = (double)v[i];
    return a;
}

mxArray *pnvg_make_color(NVGcolor c)
{
    return pnvg_make_row(c.rgba, 4);
}

mxArray *pnvg_make_glyphs(const NVGglyphPosition *p, int n, const char *base)
{
    static const char *fields[] = {"x", "minx", "maxx", "index"};
    mxArray *s = mxCreateStructMatrix(1, (mwSize)(n < 0 ? 0 : n), 4, fields);
    int i;
    for (i = 0; i < n; i++) {
        mxSetFieldByNumber(s, i, 0, mxCreateDoubleScalar(p[i].x));
        mxSetFieldByNumber(s, i, 1, mxCreateDoubleScalar(p[i].minx));
        mxSetFieldByNumber(s, i, 2, mxCreateDoubleScalar(p[i].maxx));
        /* 1-based byte offset into the UTF-8 string that was passed in. */
        mxSetFieldByNumber(s, i, 3,
                           mxCreateDoubleScalar((double)(p[i].str - base) + 1.0));
    }
    return s;
}

mxArray *pnvg_make_rows(const NVGtextRow *r, int n, const char *base)
{
    static const char *fields[] = {"start", "end", "next", "width", "minx",
                                   "maxx", "text"};
    mxArray *s = mxCreateStructMatrix(1, (mwSize)(n < 0 ? 0 : n), 7, fields);
    int i;
    for (i = 0; i < n; i++) {
        size_t len = (size_t)(r[i].end - r[i].start);
        char small[256];
        char *txt = small;
        if (len + 1 > sizeof(small))
            txt = (char *)mxMalloc(len + 1);
        memcpy(txt, r[i].start, len);
        txt[len] = '\0';
        mxSetFieldByNumber(s, i, 0,
                           mxCreateDoubleScalar((double)(r[i].start - base) + 1.0));
        mxSetFieldByNumber(s, i, 1,
                           mxCreateDoubleScalar((double)(r[i].end - base) + 1.0));
        mxSetFieldByNumber(s, i, 2,
                           mxCreateDoubleScalar((double)(r[i].next - base) + 1.0));
        mxSetFieldByNumber(s, i, 3, mxCreateDoubleScalar(r[i].width));
        mxSetFieldByNumber(s, i, 4, mxCreateDoubleScalar(r[i].minx));
        mxSetFieldByNumber(s, i, 5, mxCreateDoubleScalar(r[i].maxx));
        mxSetFieldByNumber(s, i, 6, mxCreateString(txt));
        if (txt != small)
            mxFree(txt);
    }
    return s;
}

/* ------------------------------------------------------------------ */
/* Per-subcommand statistics                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    double calls;
    double totalNs;
    double maxNs;
} pnvg_cmdstat;

static pnvg_cmdstat g_cmdstats[PNVG_MAX_CMDS];

/* ------------------------------------------------------------------ */
/* Lifecycle handlers                                                  */
/* ------------------------------------------------------------------ */

static int g_locked;

static int opt_flag(const mxArray *opts, const char *name, int dflt)
{
    const mxArray *f;
    if (!opts || !mxIsStruct(opts))
        return dflt;
    f = mxGetField(opts, 0, name);
    if (!f)
        return dflt;
    if (!is_num(f) || mxGetNumberOfElements(f) != 1)
        pnvg_err("psychnanovg:Type", "Init: opts.%s must be a scalar", name);
    return num_at(f, 0) != 0.0;
}

void h_Init(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    const mxArray *opts = (nrhs > 0) ? prhs[0] : NULL;
    int flags = 0, backend = PNVG_BACKEND_BUILT, st;
    char renderer[16];

    (void)nlhs;
    (void)plhs;
    if (opts && !mxIsStruct(opts))
        pnvg_err("psychnanovg:Type", "Init: opts must be a struct");

    if (opt_flag(opts, "antialias", 1))
        flags |= PNVG_ANTIALIAS;
    if (opt_flag(opts, "stencilStrokes", 1))
        flags |= PNVG_STENCIL_STROKES;
    if (opt_flag(opts, "debug", 0))
        flags |= PNVG_DEBUG;

    snprintf(renderer, sizeof(renderer), "%s", "auto");
    if (opts && mxIsStruct(opts)) {
        const mxArray *r = mxGetField(opts, 0, "renderer");
        if (r) {
            if (!mxIsChar(r) || mxGetString(r, renderer, sizeof(renderer)) != 0)
                pnvg_err("psychnanovg:Type",
                         "Init: opts.renderer must be '" PNVG_BACKEND_NAME
                         "', 'auto', or 'null'");
        }
    }

    if (strcmp(renderer, "null") == 0)
        backend = PNVG_BACKEND_NULL;
    else if (strcmp(renderer, "auto") == 0)
        backend = PNVG_BACKEND_BUILT;
    else if (strcmp(renderer, PNVG_BACKEND_NAME) == 0)
        backend = PNVG_BACKEND_BUILT;
    else if (strcmp(renderer, "gl2") == 0 || strcmp(renderer, "gl3") == 0)
        pnvg_err("psychnanovg:Usage",
                 "Init: this build has the %s backend, not %s. The backend is "
                 "chosen when the library is compiled.",
                 PNVG_BACKEND_NAME, renderer);
    else
        pnvg_err("psychnanovg:Usage",
                 "Init: renderer '%s' is not known", renderer);

    st = pnvg_init(backend, flags);
    if (st != PNVG_OK)
        pnvg_raise(st, "Init");

    if (backend != PNVG_BACKEND_NULL && pnvg_state_get()->stencilBits == 0)
        mexWarnMsgIdAndTxt("psychnanovg:NoStencil",
                           "The drawing target has no stencil buffer. Concave "
                           "fills and NVG_STENCIL_STROKES will be wrong.");
    memset(g_cmdstats, 0, sizeof(g_cmdstats));
    if (!g_locked) {
        mexLock();
        g_locked = 1;
    }
}

static void pnvg_at_exit(void)
{
    /* R7: without a current GL context the GL objects cannot be deleted, and
     * the driver reclaims them with the context anyway. */
    pnvg_state *s = pnvg_state_get();
    if (s->vg && s->backend != PNVG_BACKEND_NULL && !pnvg_gl_have_context())
        return;
    if (s->vg)
        pnvg_shutdown();
}

void h_Shutdown(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    pnvg_state *s = pnvg_state_get();
    (void)nlhs; (void)plhs; (void)nrhs; (void)prhs;
    if (!s->vg)
        pnvg_err("psychnanovg:NotInit", "Shutdown: no context to shut down");
    if (s->backend != PNVG_BACKEND_NULL && !pnvg_gl_have_context()) {
        mexWarnMsgIdAndTxt("psychnanovg:NoGLContext",
                           "Shutdown ran without a current GL context. The GL "
                           "objects were left to the driver.");
        memset(s, 0, sizeof(*s));
    } else {
        pnvg_shutdown();
    }
    if (g_locked) {
        mexUnlock();
        g_locked = 0;
    }
}

void h_BeginFrame(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    int w = pnvg_arg_int(prhs[0], 0, "BeginFrame");
    int h = pnvg_arg_int(prhs[1], 1, "BeginFrame");
    float r = (nrhs > 2) ? pnvg_arg_float(prhs[2], 2, "BeginFrame") : 1.0f;
    int st;
    (void)nlhs; (void)plhs;
    st = pnvg_begin_frame(w, h, r);
    if (st != PNVG_OK)
        pnvg_raise(st, "BeginFrame");
}

void h_EndFrame(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    int st;
    (void)nlhs; (void)plhs; (void)nrhs; (void)prhs;
    st = pnvg_end_frame();
    if (st != PNVG_OK)
        pnvg_raise(st, "EndFrame");
}

void h_CancelFrame(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    int st;
    (void)nlhs; (void)plhs; (void)nrhs; (void)prhs;
    st = pnvg_cancel_frame();
    if (st != PNVG_OK)
        pnvg_raise(st, "CancelFrame");
}

void h_PaintDelete(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    int idx = pnvg_arg_int(prhs[0], 0, "PaintDelete");
    (void)nlhs; (void)plhs; (void)nrhs;
    if (pnvg_paint_release(idx) != PNVG_OK)
        pnvg_err("psychnanovg:Handle",
                 "PaintDelete: %d is not an open paint handle", idx);
}

void h_ResetFallbackFonts(int nlhs, mxArray *plhs[], int nrhs,
                          const mxArray *prhs[])
{
    /* nvgResetFallbackFonts hands nvgFindFont's result to fontstash, which
     * indexes its font array with it and does not check it. An unknown name
     * gives -1 there, so resolve the name here and refuse it instead. */
    pnvg_str name;
    int id;
    (void)nlhs; (void)plhs; (void)nrhs;
    pnvg_arg_string(prhs[0], 0, "ResetFallbackFonts", &name);
    id = nvgFindFont(PNVG_VG, name.p);
    if (!pnvg_font_is_live(id)) {
        char copy[128];
        snprintf(copy, sizeof(copy), "%s", name.p);
        pnvg_str_free(&name);
        pnvg_err("psychnanovg:Handle",
                 "ResetFallbackFonts: no font is named %s", copy);
    }
    pnvg_str_free(&name);
    nvgResetFallbackFontsId(PNVG_VG, id);
}

void h_FindSystemFont(int nlhs, mxArray *plhs[], int nrhs,
                      const mxArray *prhs[])
{
    /* The search is a directory walk, which MATLAB does better than C and
     * which a site can extend without a rebuild. SPEC 5.1 keeps it in
     * PsychNanoVGFonts.m; this subcommand only forwards to it. */
    mxArray *args[2];
    mxArray *out = NULL;
    int status;
    (void)nlhs; (void)nrhs;
    if (!mxIsChar(prhs[0]))
        pnvg_err("psychnanovg:Type",
                 "FindSystemFont: the family must be a char string");
    /* Both arguments are ours to destroy. Handing prhs[0] to mexCallMATLAB
     * would give the interpreter a second owner of an array it already owns,
     * which is undefined and which Octave turns into a crash some calls
     * later. mxDuplicateArray is the documented way to pass an input on. */
    args[0] = mxCreateString("FindSystemFont");
    args[1] = mxDuplicateArray(prhs[0]);
    status = mexCallMATLAB(1, &out, 2, args, "PsychNanoVGFonts");
    mxDestroyArray(args[0]);
    mxDestroyArray(args[1]);
    if (status != 0 || !out)
        pnvg_err("psychnanovg:Font",
                 "FindSystemFont: PsychNanoVGFonts.m is not on the path");
    plhs[0] = out;
}

void h_Enum(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    char buf[256];
    (void)nlhs; (void)nrhs;
    if (!mxIsChar(prhs[0]) || mxGetString(prhs[0], buf, sizeof(buf)) != 0)
        pnvg_err("psychnanovg:Type", "Enum: the name must be a char string");
    plhs[0] = mxCreateDoubleScalar((double)enum_from_string(buf, "Enum"));
}

static int find_cmd(const char *name)
{
    int lo = 0, hi = pnvg_ncmds - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int c = strcmp(pnvg_cmds[mid].name, name);
        if (c == 0)
            return mid;
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

void h_Opcode(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    char buf[PNVG_NAME_MAX];
    int idx;
    (void)nlhs; (void)nrhs;
    if (!mxIsChar(prhs[0]) || mxGetString(prhs[0], buf, sizeof(buf)) != 0)
        pnvg_err("psychnanovg:Type", "Opcode: the name must be a char string");
    idx = find_cmd(buf);
    if (idx < 0)
        pnvg_err("psychnanovg:UnknownCommand", "Opcode: no subcommand %s", buf);
    plhs[0] = mxCreateDoubleScalar((double)(idx + 1));
}

void h_Version(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    static const char *fields[] = {"nanovg", "psychnanovg", "backend",
                                   "glVersion", "glRenderer", "build"};
    pnvg_state *s = pnvg_state_get();
    mxArray *v;
    const char *backend = "none";
    char build[128];
    (void)nlhs; (void)nrhs; (void)prhs;

    switch (s->backend) {
    case PNVG_BACKEND_GL3:  backend = "GL3"; break;
    case PNVG_BACKEND_GL2:  backend = "GL2"; break;
    case PNVG_BACKEND_NULL: backend = "null"; break;
    default: break;
    }
    snprintf(build, sizeof(build), "%s %s, stats=%d, tracy=%d",
             __DATE__, __TIME__, PSYCHNANOVG_STATS,
#if defined(PSYCHNANOVG_TRACY) && PSYCHNANOVG_TRACY
             1
#else
             0
#endif
             );
    v = mxCreateStructMatrix(1, 1, 6, fields);
    mxSetFieldByNumber(v, 0, 0, mxCreateString(pnvg_version_string));
    mxSetFieldByNumber(v, 0, 1, mxCreateString(PNVG_VERSION));
    mxSetFieldByNumber(v, 0, 2, mxCreateString(backend));
    mxSetFieldByNumber(v, 0, 3, mxCreateString(s->glVersion[0] ? s->glVersion : ""));
    mxSetFieldByNumber(v, 0, 4, mxCreateString(s->glRenderer[0] ? s->glRenderer : ""));
    mxSetFieldByNumber(v, 0, 5, mxCreateString(build));
    plhs[0] = v;
}

void h_Stats(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    static const char *top[] = {"frames", "endFrameNs", "endFrameMaxNs",
                                "endFrameSumNs", "gpuNs", "drawCalls",
                                "fillCount", "strokeCount", "textCount",
                                "vertexCount", "commands"};
    static const char *cf[] = {"name", "calls", "totalNs", "maxNs"};
    pnvg_state *s = pnvg_state_get();
    mxArray *out, *cmds;
    int i, n = 0, k = 0;
    (void)nlhs;

    if (nrhs > 0) {
        char buf[32];
        if (!mxIsChar(prhs[0]) || mxGetString(prhs[0], buf, sizeof(buf)) != 0 ||
            strcmp(buf, "reset") != 0)
            pnvg_err("psychnanovg:Usage",
                     "Stats: the only option is the string 'reset'");
        memset(g_cmdstats, 0, sizeof(g_cmdstats));
        memset(&s->stats, 0, sizeof(s->stats));
        return;
    }

    for (i = 0; i < pnvg_ncmds && i < PNVG_MAX_CMDS; i++)
        if (g_cmdstats[i].calls > 0.0)
            n++;
    cmds = mxCreateStructMatrix(1, (mwSize)n, 4, cf);
    for (i = 0; i < pnvg_ncmds && i < PNVG_MAX_CMDS; i++) {
        if (g_cmdstats[i].calls <= 0.0)
            continue;
        mxSetFieldByNumber(cmds, k, 0, mxCreateString(pnvg_cmds[i].name));
        mxSetFieldByNumber(cmds, k, 1, mxCreateDoubleScalar(g_cmdstats[i].calls));
        mxSetFieldByNumber(cmds, k, 2, mxCreateDoubleScalar(g_cmdstats[i].totalNs));
        mxSetFieldByNumber(cmds, k, 3, mxCreateDoubleScalar(g_cmdstats[i].maxNs));
        k++;
    }

    out = mxCreateStructMatrix(1, 1, 11, top);
    mxSetFieldByNumber(out, 0, 0, mxCreateDoubleScalar(s->stats.frames));
    mxSetFieldByNumber(out, 0, 1, mxCreateDoubleScalar(s->stats.endFrameNs));
    mxSetFieldByNumber(out, 0, 2, mxCreateDoubleScalar(s->stats.endFrameMaxNs));
    mxSetFieldByNumber(out, 0, 3, mxCreateDoubleScalar(s->stats.endFrameSumNs));
    mxSetFieldByNumber(out, 0, 4, mxCreateDoubleScalar(s->stats.gpuNs));
    mxSetFieldByNumber(out, 0, 5, mxCreateDoubleScalar(s->stats.drawCalls));
    mxSetFieldByNumber(out, 0, 6, mxCreateDoubleScalar(s->stats.fillCount));
    mxSetFieldByNumber(out, 0, 7, mxCreateDoubleScalar(s->stats.strokeCount));
    mxSetFieldByNumber(out, 0, 8, mxCreateDoubleScalar(s->stats.textCount));
    mxSetFieldByNumber(out, 0, 9, mxCreateDoubleScalar(s->stats.vertexCount));
    mxSetFieldByNumber(out, 0, 10, cmds);
    plhs[0] = out;
}

/* ------------------------------------------------------------------ */
/* Help                                                                */
/* ------------------------------------------------------------------ */

static void print_list(void)
{
    const char *group = NULL;
    int i;
    mexPrintf("PsychNanoVG %s (NanoVG %s)\n", PNVG_VERSION, pnvg_version_string);
    mexPrintf("%d subcommands. PsychNanoVG('Name?') prints one help text.\n\n",
              pnvg_ncmds);
    /* The table is sorted by name, so walk it once per group. */
    for (;;) {
        const char *next = NULL;
        for (i = 0; i < pnvg_ncmds; i++) {
            const char *g = pnvg_cmds[i].group;
            if (group && strcmp(g, group) <= 0)
                continue;
            if (!next || strcmp(g, next) < 0)
                next = g;
        }
        if (!next)
            break;
        group = next;
        mexPrintf("%s:\n   ", group);
        for (i = 0; i < pnvg_ncmds; i++)
            if (strcmp(pnvg_cmds[i].group, group) == 0)
                mexPrintf(" %s", pnvg_cmds[i].name);
        mexPrintf("\n");
    }
}

static void print_help(int idx)
{
    mexPrintf("%s  (%s)\n\n%s\n", pnvg_cmds[idx].name, pnvg_cmds[idx].group,
              pnvg_cmds[idx].help);
    mexPrintf("\nArguments after the name: %d to %d. Outputs: %d.\n",
              pnvg_cmds[idx].minArgs, pnvg_cmds[idx].maxArgs,
              pnvg_cmds[idx].nOut);
}

/* ------------------------------------------------------------------ */
/* Dispatch                                                            */
/* ------------------------------------------------------------------ */

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    char name[PNVG_NAME_MAX];
    const pnvg_cmd *c;
    pnvg_state *s = pnvg_state_get();
    int idx = -1, nargs;
    double t0 = 0.0;
    static int atexit_set;

    if (!atexit_set) {
        mexAtExit(pnvg_at_exit);
        atexit_set = 1;
    }

    if (nrhs == 0) {
        print_list();
        return;
    }

    if (mxIsChar(prhs[0])) {
        size_t len;
        if (mxGetString(prhs[0], name, sizeof(name)) != 0)
            pnvg_err("psychnanovg:UnknownCommand",
                     "the subcommand name is longer than %d characters",
                     PNVG_NAME_MAX - 1);
        len = strlen(name);
        if (len > 1 && name[len - 1] == '?') {
            name[len - 1] = '\0';
            idx = find_cmd(name);
            if (idx < 0)
                pnvg_err("psychnanovg:UnknownCommand",
                         "no subcommand named %s", name);
            print_help(idx);
            return;
        }
        idx = find_cmd(name);
        if (idx < 0)
            pnvg_err("psychnanovg:UnknownCommand",
                     "no subcommand named %s", name);
    } else if (is_num(prhs[0]) && mxGetNumberOfElements(prhs[0]) == 1) {
        double op = num_at(prhs[0], 0);
        idx = (int)op - 1;
        if (idx < 0 || idx >= pnvg_ncmds)
            pnvg_err("psychnanovg:UnknownCommand",
                     "opcode %g is not in 1 to %d", op, pnvg_ncmds);
    } else {
        pnvg_err("psychnanovg:Usage",
                 "the first argument must be a subcommand name or an opcode");
    }

    c = &pnvg_cmds[idx];
    nargs = nrhs - 1;
    if (nargs < c->minArgs || nargs > c->maxArgs) {
        if (c->minArgs == c->maxArgs)
            pnvg_err("psychnanovg:Usage",
                     "%s takes %d argument(s) after the name, not %d",
                     c->name, c->minArgs, nargs);
        pnvg_err("psychnanovg:Usage",
                 "%s takes %d to %d arguments after the name, not %d",
                 c->name, c->minArgs, c->maxArgs, nargs);
    }

    if ((c->flags & PNVG_F_INIT) && !s->vg)
        pnvg_err("psychnanovg:NotInit",
                 "%s needs a context. Call PsychNanoVG('Init') first.", c->name);
    if ((c->flags & PNVG_F_FRAME) && !s->inFrame)
        pnvg_err("psychnanovg:FrameState",
                 "%s must run between BeginFrame and EndFrame", c->name);
    /* Init carries PNVG_F_GL too, but its renderer is not known yet, so the
     * context check for it happens in the core. */
    if ((c->flags & PNVG_F_GL) && (c->flags & PNVG_F_INIT) &&
        s->backend != PNVG_BACKEND_NULL && !pnvg_gl_have_context())
        pnvg_err("psychnanovg:NoGLContext",
                 "%s issues OpenGL calls. Call it between "
                 "Screen('BeginOpenGL') and Screen('EndOpenGL').", c->name);

#if PSYCHNANOVG_STATS
    t0 = pnvg_now_ns();
#endif
    PNVG_ZONE("dispatch");
    c->fn(nlhs, plhs, nargs, prhs + 1);
    PNVG_ZONE_END();
#if PSYCHNANOVG_STATS
    if (idx < PNVG_MAX_CMDS) {
        double dt = pnvg_now_ns() - t0;
        g_cmdstats[idx].calls += 1.0;
        g_cmdstats[idx].totalNs += dt;
        if (dt > g_cmdstats[idx].maxNs)
            g_cmdstats[idx].maxNs = dt;
    }
#endif

    /* Keep the live-image bitset in step with NanoVG's own handles so that a
     * stale id gets psychnanovg:Handle instead of reaching NanoVG. */
    if ((c->flags & PNVG_F_MKIMG) && plhs[0])
        pnvg_image_mark((int)mxGetScalar(plhs[0]));
    if ((c->flags & PNVG_F_MKFONT) && plhs[0])
        pnvg_font_mark((int)mxGetScalar(plhs[0]));
    if ((c->flags & PNVG_F_DELIMG) && nargs >= 1)
        pnvg_image_unmark((int)num_at(prhs[1], 0));
}
