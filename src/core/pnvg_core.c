/* psychnanovg core layer. No MATLAB types appear here. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pnvg_core.h"

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <time.h>
#endif

NVGcontext *pnvg_null_create(int flags);
void pnvg_null_delete(NVGcontext *vg);

/* One context per process (SPEC 1.2). A second context would need a second
 * paint table and a second frame flag, which phase 3 covers. */
static pnvg_state g_state;

pnvg_state *pnvg_state_get(void)
{
    return &g_state;
}

const char *pnvg_last_error(void)
{
    return g_state.err[0] ? g_state.err : "no further information";
}

static int fail(int code, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_state.err, sizeof(g_state.err), fmt, ap);
    va_end(ap);
    return code;
}

double pnvg_now_ns(void)
{
#if defined(_WIN32)
    /* QueryPerformanceFrequency is fixed at boot, so cache it. */
    static double scale = 0.0;
    LARGE_INTEGER t;
    if (scale == 0.0) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        scale = 1e9 / (double)f.QuadPart;
    }
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * scale;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
#endif
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

static void paint_table_reset(void)
{
    int i;
    for (i = 0; i < PNVG_MAX_PAINTS; i++) {
        g_state.paintNext[i] = (short)(i + 1);
        g_state.paintLive[i] = 0;
    }
    g_state.paintNext[PNVG_MAX_PAINTS - 1] = -1;
    g_state.paintFreeHead = 0;
}

int pnvg_init(int backend, int createFlags)
{
    if (g_state.vg)
        return fail(PNVG_E_ALREADYINIT, "a context already exists");

    memset(&g_state, 0, sizeof(g_state));
    paint_table_reset();
    g_state.boundTarget = -1;

    if (backend == PNVG_BACKEND_NULL) {
        g_state.vg = pnvg_null_create(createFlags);
        if (!g_state.vg)
            return fail(PNVG_E_GLINIT, "the null renderer could not start");
        snprintf(g_state.glVersion, sizeof(g_state.glVersion), "none (null renderer)");
        snprintf(g_state.glRenderer, sizeof(g_state.glRenderer), "null");
        g_state.stencilBits = 8;
    } else {
        if (!pnvg_gl_have_context())
            return fail(PNVG_E_NOGLCONTEXT,
                        "no OpenGL context is current on this thread");
        if (pnvg_gl_load() != 0)
            return fail(PNVG_E_GLINIT, "the OpenGL entry points could not be loaded");
        pnvg_gl_query_info(g_state.glVersion, sizeof(g_state.glVersion),
                           g_state.glRenderer, sizeof(g_state.glRenderer),
                           &g_state.stencilBits);
        g_state.vg = pnvg_gl_create(backend, createFlags);
        if (!g_state.vg)
            return fail(PNVG_E_GLINIT, "nvgCreateGL failed for GL version %s",
                        g_state.glVersion);
        pnvg_gl_timer_reset();
    }
    g_state.backend = backend;
    g_state.createFlags = createFlags;
    return PNVG_OK;
}

int pnvg_shutdown(void)
{
    int i;
    if (!g_state.vg)
        return fail(PNVG_E_NOTINIT, "no context to shut down");

    for (i = 0; i < PNVG_MAX_TARGETS; i++) {
        if (g_state.targets[i]) {
            pnvg_gl_fb_delete(g_state.targets[i]);
            g_state.targets[i] = NULL;
        }
    }
    /* NanoVG deletes its own images and fonts with the context. */
    if (g_state.backend == PNVG_BACKEND_NULL)
        pnvg_null_delete(g_state.vg);
    else
        pnvg_gl_destroy(g_state.vg, g_state.backend);

    free(g_state.scratch);
    memset(&g_state, 0, sizeof(g_state));
    g_state.boundTarget = -1;
    return PNVG_OK;
}

int pnvg_begin_frame(int w, int h, float pixelRatio)
{
    if (!g_state.vg)
        return fail(PNVG_E_NOTINIT, "call Init first");
    if (g_state.inFrame)
        return fail(PNVG_E_FRAMESTATE, "BeginFrame inside a frame");
    if (w <= 0 || h <= 0 || pixelRatio <= 0.0f)
        return fail(PNVG_E_RANGE, "BeginFrame needs positive w, h, pixelRatio");

    if (g_state.backend != PNVG_BACKEND_NULL) {
        pnvg_gl_save(&g_state.saved);
        pnvg_gl_viewport(0, 0, w, h);
        pnvg_gl_timer_begin();
    }
    nvgBeginFrame(g_state.vg, (float)w, (float)h, pixelRatio);
    g_state.inFrame = 1;
    g_state.frameW = w;
    g_state.frameH = h;
    g_state.pixelRatio = pixelRatio;
    return PNVG_OK;
}

int pnvg_end_frame(void)
{
    double t0, dt;
    int dc = 0, fc = 0, sc = 0, tc = 0;

    if (!g_state.vg)
        return fail(PNVG_E_NOTINIT, "call Init first");
    if (!g_state.inFrame)
        return fail(PNVG_E_FRAMESTATE, "EndFrame without BeginFrame");

    t0 = pnvg_now_ns();
    nvgEndFrame(g_state.vg);
    dt = pnvg_now_ns() - t0;
    g_state.inFrame = 0;

    pnvg_nvg_counters(g_state.vg, &dc, &fc, &sc, &tc);
    g_state.stats.frames += 1.0;
    g_state.stats.endFrameNs = dt;
    g_state.stats.endFrameSumNs += dt;
    if (dt > g_state.stats.endFrameMaxNs)
        g_state.stats.endFrameMaxNs = dt;
    g_state.stats.drawCalls = dc;
    g_state.stats.fillCount = fc;
    g_state.stats.strokeCount = sc;
    g_state.stats.textCount = tc;
    g_state.stats.vertexCount = (double)(fc + sc + tc) * 3.0;

    if (g_state.backend != PNVG_BACKEND_NULL) {
        unsigned int e;
        pnvg_gl_timer_end();
        g_state.stats.gpuNs = pnvg_gl_timer_read();
        pnvg_gl_restore(&g_state.saved);
        /* R3: Screen('EndOpenGL') aborts on a pending error, so drain here
         * and report it against EndFrame rather than against PTB. */
        e = pnvg_gl_drain_error();
        if (e)
            return fail(PNVG_E_GLERROR, "OpenGL reported %s (0x%04X) during EndFrame",
                        pnvg_gl_error_name(e), e);
    }
    return PNVG_OK;
}

int pnvg_cancel_frame(void)
{
    if (!g_state.vg)
        return fail(PNVG_E_NOTINIT, "call Init first");
    if (!g_state.inFrame)
        return fail(PNVG_E_FRAMESTATE, "CancelFrame without BeginFrame");
    nvgCancelFrame(g_state.vg);
    g_state.inFrame = 0;
    if (g_state.backend != PNVG_BACKEND_NULL)
        pnvg_gl_restore(&g_state.saved);
    return PNVG_OK;
}

/* ------------------------------------------------------------------ */
/* Paint table                                                         */
/* ------------------------------------------------------------------ */

int pnvg_paint_alloc(const NVGpaint *p)
{
    int idx = g_state.paintFreeHead;
    if (idx < 0)
        return -1;
    g_state.paintFreeHead = g_state.paintNext[idx];
    g_state.paints[idx] = *p;
    g_state.paintLive[idx] = 1;
    return idx + 1;
}

int pnvg_paint_fetch(int idx, NVGpaint *out)
{
    if (idx < 1 || idx > PNVG_MAX_PAINTS || !g_state.paintLive[idx - 1])
        return PNVG_E_HANDLE;
    *out = g_state.paints[idx - 1];
    return PNVG_OK;
}

int pnvg_paint_release(int idx)
{
    if (idx < 1 || idx > PNVG_MAX_PAINTS || !g_state.paintLive[idx - 1])
        return PNVG_E_HANDLE;
    g_state.paintLive[idx - 1] = 0;
    g_state.paintNext[idx - 1] = g_state.paintFreeHead;
    g_state.paintFreeHead = (short)(idx - 1);
    return PNVG_OK;
}

/* ------------------------------------------------------------------ */
/* Image handles                                                       */
/* ------------------------------------------------------------------ */

/* fonsAddFallbackFont and fonsResetFallbackFont dereference stash->fonts[id]
 * with no check of their own, and an unused slot holds NULL, so an unchecked
 * font handle from a script is a crash rather than an error. SPEC 8.4 puts
 * that check here. */
void pnvg_font_mark(int id)
{
    if (id >= 0 && id >= g_state.fontCount)
        g_state.fontCount = id + 1;
}

int pnvg_font_is_live(int id)
{
    return id >= 0 && id < g_state.fontCount;
}

void pnvg_image_mark(int id)
{
    if (id > 0 && id < PNVG_MAX_IMAGES)
        g_state.imageLive[id >> 5] |= 1u << (id & 31);
}

void pnvg_image_unmark(int id)
{
    if (id > 0 && id < PNVG_MAX_IMAGES)
        g_state.imageLive[id >> 5] &= ~(1u << (id & 31));
}

int pnvg_image_is_live(int id)
{
    if (id <= 0 || id >= PNVG_MAX_IMAGES)
        return 0;
    return (g_state.imageLive[id >> 5] >> (id & 31)) & 1u;
}

const unsigned char *pnvg_image_transpose(const unsigned char *src, int h, int w)
{
    size_t need = (size_t)h * (size_t)w * 4u;
    size_t plane = (size_t)h * (size_t)w;
    int r, c;
    unsigned char *d;

    if (need == 0)
        return NULL;
    if (g_state.scratchBytes < need) {
        unsigned char *p = (unsigned char *)realloc(g_state.scratch, need);
        if (!p)
            return NULL;
        g_state.scratch = p;
        g_state.scratchBytes = need;
    }
    d = g_state.scratch;
    /* Writes run straight through the destination; the reads stride by h.
     * One pass either way, and the sequential stores are the cheaper half. */
    for (r = 0; r < h; r++) {
        const unsigned char *s = src + r;
        for (c = 0; c < w; c++) {
            size_t o = (size_t)c * (size_t)h;
            *d++ = s[o];
            *d++ = s[o + plane];
            *d++ = s[o + plane * 2];
            *d++ = s[o + plane * 3];
        }
    }
    return g_state.scratch;
}

/* ------------------------------------------------------------------ */
/* Batched paths                                                       */
/* ------------------------------------------------------------------ */

#define PNVG_COL(T, data, n, col, i) ((float)(((const T *)(data))[(size_t)(col) * (size_t)(n) + (size_t)(i)]))

#define PNVG_POLYLINE(T)                                                     \
    do {                                                                     \
        int i;                                                               \
        nvgMoveTo(vg, PNVG_COL(T, data, n, 0, 0), PNVG_COL(T, data, n, 1, 0));\
        for (i = 1; i < n; i++)                                              \
            nvgLineTo(vg, PNVG_COL(T, data, n, 0, i),                        \
                      PNVG_COL(T, data, n, 1, i));                           \
    } while (0)

int pnvg_polyline(const void *data, int n, int isSingle, int close)
{
    NVGcontext *vg = g_state.vg;
    if (!vg)
        return fail(PNVG_E_NOTINIT, "call Init first");
    if (n <= 0)
        return PNVG_OK;
    if (isSingle)
        PNVG_POLYLINE(float);
    else
        PNVG_POLYLINE(double);
    if (close)
        nvgClosePath(vg);
    return PNVG_OK;
}

#define PNVG_CIRCLES(T)                                                      \
    do {                                                                     \
        int i;                                                               \
        for (i = 0; i < n; i++)                                              \
            nvgCircle(vg, PNVG_COL(T, data, n, 0, i),                        \
                      PNVG_COL(T, data, n, 1, i),                            \
                      PNVG_COL(T, data, n, 2, i));                           \
    } while (0)

int pnvg_circles(const void *data, int n, int isSingle)
{
    NVGcontext *vg = g_state.vg;
    if (!vg)
        return fail(PNVG_E_NOTINIT, "call Init first");
    if (isSingle)
        PNVG_CIRCLES(float);
    else
        PNVG_CIRCLES(double);
    return PNVG_OK;
}

#define PNVG_RECTS(T)                                                        \
    do {                                                                     \
        int i;                                                               \
        for (i = 0; i < n; i++)                                              \
            nvgRect(vg, PNVG_COL(T, data, n, 0, i),                          \
                    PNVG_COL(T, data, n, 1, i),                              \
                    PNVG_COL(T, data, n, 2, i),                              \
                    PNVG_COL(T, data, n, 3, i));                             \
    } while (0)

int pnvg_rects(const void *data, int n, int isSingle)
{
    NVGcontext *vg = g_state.vg;
    if (!vg)
        return fail(PNVG_E_NOTINIT, "call Init first");
    if (isSingle)
        PNVG_RECTS(float);
    else
        PNVG_RECTS(double);
    return PNVG_OK;
}

#define PNVG_PATHM(T)                                                        \
    do {                                                                     \
        int i;                                                               \
        for (i = 0; i < n; i++) {                                            \
            int code = (int)PNVG_COL(T, data, n, 0, i);                      \
            switch (code) {                                                  \
            case PNVG_PATH_M:                                                \
                nvgMoveTo(vg, PNVG_COL(T, data, n, 1, i),                    \
                          PNVG_COL(T, data, n, 2, i));                       \
                break;                                                       \
            case PNVG_PATH_L:                                                \
                nvgLineTo(vg, PNVG_COL(T, data, n, 1, i),                    \
                          PNVG_COL(T, data, n, 2, i));                       \
                break;                                                       \
            case PNVG_PATH_Q:                                                \
                nvgQuadTo(vg, PNVG_COL(T, data, n, 1, i),                    \
                          PNVG_COL(T, data, n, 2, i),                        \
                          PNVG_COL(T, data, n, 3, i),                        \
                          PNVG_COL(T, data, n, 4, i));                       \
                break;                                                       \
            case PNVG_PATH_C:                                                \
                nvgBezierTo(vg, PNVG_COL(T, data, n, 1, i),                  \
                            PNVG_COL(T, data, n, 2, i),                      \
                            PNVG_COL(T, data, n, 3, i),                      \
                            PNVG_COL(T, data, n, 4, i),                      \
                            PNVG_COL(T, data, n, 5, i),                      \
                            PNVG_COL(T, data, n, 6, i));                     \
                break;                                                       \
            case PNVG_PATH_Z:                                                \
                nvgClosePath(vg);                                            \
                break;                                                       \
            default:                                                         \
                return fail(PNVG_E_RANGE,                                    \
                            "Path: row %d has command code %d, not 1 to 5",   \
                            i + 1, code);                                    \
            }                                                                \
        }                                                                    \
    } while (0)

int pnvg_path_matrix(const void *data, int n, int isSingle)
{
    NVGcontext *vg = g_state.vg;
    if (!vg)
        return fail(PNVG_E_NOTINIT, "call Init first");
    if (isSingle)
        PNVG_PATHM(float);
    else
        PNVG_PATHM(double);
    return PNVG_OK;
}

/* ------------------------------------------------------------------ */
/* Render targets                                                      */
/* ------------------------------------------------------------------ */

int pnvg_target_create(int w, int h, int imageFlags)
{
    int i;
    void *fb;
    /* These two report a handle, so a failure is -1 rather than a status
     * code: a status code would be mistaken for a valid slot. */
    if (!g_state.vg) {
        fail(PNVG_E_NOTINIT, "call Init first");
        return -1;
    }
    if (g_state.backend == PNVG_BACKEND_NULL) {
        fail(PNVG_E_GLINIT, "render targets need a GL renderer");
        return -1;
    }
    for (i = 0; i < PNVG_MAX_TARGETS; i++)
        if (!g_state.targets[i])
            break;
    if (i == PNVG_MAX_TARGETS) {
        fail(PNVG_E_RANGE, "all %d render target slots are in use",
             PNVG_MAX_TARGETS);
        return -1;
    }
    fb = pnvg_gl_fb_create(g_state.vg, w, h, imageFlags);
    if (!fb) {
        fail(PNVG_E_GLERROR, "nvgluCreateFramebuffer failed for %dx%d", w, h);
        return -1;
    }
    g_state.targets[i] = fb;
    g_state.targetPrevFbo[i] = 0;
    pnvg_image_mark(pnvg_gl_fb_image(fb));
    return i + 1;
}

void *pnvg_target_ptr(int rt)
{
    if (rt < 1 || rt > PNVG_MAX_TARGETS)
        return NULL;
    return g_state.targets[rt - 1];
}

int pnvg_target_bind(int rt)
{
    void *fb = pnvg_target_ptr(rt);
    if (!fb)
        return fail(PNVG_E_HANDLE, "render target %d is not open", rt);
    g_state.targetPrevFbo[rt - 1] = pnvg_gl_current_fbo();
    pnvg_gl_fb_bind(fb);
    g_state.boundTarget = rt;
    return PNVG_OK;
}

int pnvg_target_unbind(void)
{
    int rt = g_state.boundTarget;
    if (rt < 1)
        return fail(PNVG_E_FRAMESTATE, "no render target is bound");
    pnvg_gl_fb_bind_raw(g_state.targetPrevFbo[rt - 1]);
    g_state.boundTarget = -1;
    return PNVG_OK;
}

int pnvg_target_image(int rt)
{
    void *fb = pnvg_target_ptr(rt);
    if (!fb) {
        fail(PNVG_E_HANDLE, "render target %d is not open", rt);
        return -1;
    }
    return pnvg_gl_fb_image(fb);
}

int pnvg_target_delete(int rt)
{
    void *fb = pnvg_target_ptr(rt);
    if (!fb)
        return fail(PNVG_E_HANDLE, "render target %d is not open", rt);
    if (g_state.boundTarget == rt)
        pnvg_target_unbind();
    pnvg_image_unmark(pnvg_gl_fb_image(fb));
    pnvg_gl_fb_delete(fb);
    g_state.targets[rt - 1] = NULL;
    return PNVG_OK;
}
