/* psychnanovg core layer. No MATLAB types appear here. */

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pnvg_core.h"
#include "pnvg_profiler.h"

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
#elif defined(__APPLE__)
    /* clock_gettime(CLOCK_MONOTONIC) is rounded to microseconds on macOS, so
     * a null renderer EndFrame measured 0 there and the Stats test failed on
     * the CI runner. CLOCK_MONOTONIC_RAW through the _np variant keeps the
     * mach_absolute_time resolution, about 42 ns on Apple silicon. */
    return (double)clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
#endif
}

/* ------------------------------------------------------------------ */
/* Tracy zone stack (SPEC 9.3)                                         */
/* ------------------------------------------------------------------ */

#if PNVG_TRACY
/* Deep enough for dispatch plus the zones inside one handler. A deeper
 * nest is still counted, so begin and end stay paired, but not sent. */
#define PNVG_ZONE_DEPTH 16

static TracyCZoneCtx g_zones[PNVG_ZONE_DEPTH];
static int g_zoneDepth;
static int g_profStarted;

void pnvg_prof_startup(void)
{
    if (g_profStarted)
        return;
    ___tracy_startup_profiler();
    g_profStarted = 1;
}

void pnvg_prof_shutdown(void)
{
    if (!g_profStarted)
        return;
    pnvg_zone_unwind();
    ___tracy_shutdown_profiler();
    g_profStarted = 0;
}

int pnvg_prof_started(void)
{
    return g_profStarted;
}

void pnvg_zone_begin(const pnvg_srcloc *loc)
{
    if (g_zoneDepth < PNVG_ZONE_DEPTH) {
        /* Before startup there is no profiler to talk to. An inactive
         * context marks a zone that was not sent, so its end is not sent
         * either. */
        if (g_profStarted) {
            g_zones[g_zoneDepth] = ___tracy_emit_zone_begin(loc, 1);
        } else {
            g_zones[g_zoneDepth].id = 0;
            g_zones[g_zoneDepth].active = 0;
        }
    }
    g_zoneDepth++;
}

void pnvg_zone_end(void)
{
    if (g_zoneDepth <= 0)
        return;
    g_zoneDepth--;
    if (g_zoneDepth < PNVG_ZONE_DEPTH && g_profStarted &&
        g_zones[g_zoneDepth].active)
        ___tracy_emit_zone_end(g_zones[g_zoneDepth]);
}

void pnvg_zone_unwind(void)
{
    while (g_zoneDepth > 0)
        pnvg_zone_end();
}
#endif

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
    g_state.targetDepth = 0;
#if PNVG_TRACY
    /* The MEX starts the profiler on its first call. A native program such
     * as smoke_gl has no such call, so Init does it too. */
    pnvg_prof_startup();
#endif

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
    pnvg_stats_reset();
    return PNVG_OK;
}

void pnvg_stats_reset(void)
{
    memset(&g_state.stats, 0, sizeof(g_state.stats));
    if (!g_state.vg || g_state.backend == PNVG_BACKEND_NULL ||
        !pnvg_gl_timer_available())
        g_state.stats.gpuNs = NAN;
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
    if (g_state.backend == PNVG_BACKEND_NULL) {
        pnvg_null_delete(g_state.vg);
    } else {
        pnvg_gl_timer_release();
        pnvg_gl_destroy(g_state.vg, g_state.backend);
    }

    free(g_state.scratch);
    memset(&g_state, 0, sizeof(g_state));
    g_state.targetDepth = 0;
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

    PNVG_ZONE("nvgEndFrame");
    t0 = pnvg_now_ns();
    nvgEndFrame(g_state.vg);
    dt = pnvg_now_ns() - t0;
    PNVG_ZONE_END();
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
        /* Separate zones, because the driver can do its submission work in
         * any of these calls rather than in nvgEndFrame. */
        PNVG_ZONE("GPU timer");
        pnvg_gl_timer_end();
        if (pnvg_gl_timer_available())
            g_state.stats.gpuNs = pnvg_gl_timer_read();
        PNVG_ZONE_END();
        PNVG_ZONE("GL restore and error drain");
        pnvg_gl_restore(&g_state.saved);
        /* R3: Screen('EndOpenGL') aborts on a pending error, so drain here
         * and report it against EndFrame rather than against PTB. */
        e = pnvg_gl_drain_error();
        PNVG_ZONE_END();
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
    if (g_state.backend != PNVG_BACKEND_NULL) {
        /* BeginFrame wrote the first timestamp and, with Tracy, opened a GPU
         * zone. Both need their end, or the ring and Tracy disagree. */
        pnvg_gl_timer_end();
        if (pnvg_gl_timer_available())
            g_state.stats.gpuNs = pnvg_gl_timer_read();
        pnvg_gl_restore(&g_state.saved);
    }
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
    PNVG_ZONE("ImageTranspose");
    if (g_state.scratchBytes < need) {
        unsigned char *p = (unsigned char *)realloc(g_state.scratch, need);
        if (!p) {
            PNVG_ZONE_END();
            return NULL;
        }
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
    PNVG_ZONE_END();
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
    PNVG_ZONE("Polyline");
    if (isSingle)
        PNVG_POLYLINE(float);
    else
        PNVG_POLYLINE(double);
    if (close)
        nvgClosePath(vg);
    PNVG_ZONE_END();
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
    PNVG_ZONE("Circles");
    if (isSingle)
        PNVG_CIRCLES(float);
    else
        PNVG_CIRCLES(double);
    PNVG_ZONE_END();
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
    PNVG_ZONE("Rects");
    if (isSingle)
        PNVG_RECTS(float);
    else
        PNVG_RECTS(double);
    PNVG_ZONE_END();
    return PNVG_OK;
}

/* One pass over column 1 before anything reaches NanoVG. A bad row then
 * leaves the path untouched, and the drawing loop below needs no error
 * branch. The directions are the only arguments that NanoVG takes without
 * complaint and then misreads, so they are the only ones checked. */
#define PNVG_PATHV(T)                                                        \
    do {                                                                     \
        const T *d_ = (const T *)data;                                       \
        int i;                                                               \
        for (i = 0; i < n; i++) {                                            \
            double v = (double)d_[i];                                        \
            int code = (v >= 1.0 && v <= (double)PNVG_PATH_MAXCODE)          \
                           ? (int)v : 0;                                     \
            if (code == 0 || (double)code != v)                              \
                return fail(PNVG_E_RANGE,                                    \
                            "row %d has command code %g, not an integer "    \
                            "from 1 to %d", i + 1, v, PNVG_PATH_MAXCODE);    \
            if (code == PNVG_PATH_ARC || code == PNVG_PATH_WINDING) {        \
                int dc = (code == PNVG_PATH_ARC) ? 6 : 1;                    \
                double dir = (double)d_[(size_t)dc * (size_t)n + (size_t)i]; \
                if (dir != 1.0 && dir != 2.0)                                \
                    return fail(PNVG_E_RANGE,                                \
                                "row %d: the direction in column %d must "   \
                                "be 1 (NVG_CCW, NVG_SOLID) or 2 (NVG_CW, "   \
                                "NVG_HOLE), not %g", i + 1, dc + 1, dir);    \
            }                                                                \
        }                                                                    \
    } while (0)

#define PNVG_A(T, k) PNVG_COL(T, data, n, k, i)

/* The default branch is PNVG_PATH_WINDING: PNVG_PATHV has already refused
 * every code outside 1 to PNVG_PATH_MAXCODE. */
#define PNVG_PATHM(T)                                                        \
    do {                                                                     \
        int i;                                                               \
        for (i = 0; i < n; i++) {                                            \
            switch ((int)PNVG_A(T, 0)) {                                     \
            case PNVG_PATH_M:                                                \
                nvgMoveTo(vg, PNVG_A(T, 1), PNVG_A(T, 2));                   \
                break;                                                       \
            case PNVG_PATH_L:                                                \
                nvgLineTo(vg, PNVG_A(T, 1), PNVG_A(T, 2));                   \
                break;                                                       \
            case PNVG_PATH_Q:                                                \
                nvgQuadTo(vg, PNVG_A(T, 1), PNVG_A(T, 2), PNVG_A(T, 3),      \
                          PNVG_A(T, 4));                                     \
                break;                                                       \
            case PNVG_PATH_C:                                                \
                nvgBezierTo(vg, PNVG_A(T, 1), PNVG_A(T, 2), PNVG_A(T, 3),    \
                            PNVG_A(T, 4), PNVG_A(T, 5), PNVG_A(T, 6));       \
                break;                                                       \
            case PNVG_PATH_Z:                                                \
                nvgClosePath(vg);                                            \
                break;                                                       \
            case PNVG_PATH_ARC:                                              \
                nvgArc(vg, PNVG_A(T, 1), PNVG_A(T, 2), PNVG_A(T, 3),         \
                       PNVG_A(T, 4), PNVG_A(T, 5), (int)PNVG_A(T, 6));       \
                break;                                                       \
            case PNVG_PATH_ARCTO:                                            \
                nvgArcTo(vg, PNVG_A(T, 1), PNVG_A(T, 2), PNVG_A(T, 3),       \
                         PNVG_A(T, 4), PNVG_A(T, 5));                        \
                break;                                                       \
            case PNVG_PATH_ELLIPSE:                                          \
                nvgEllipse(vg, PNVG_A(T, 1), PNVG_A(T, 2), PNVG_A(T, 3),     \
                           PNVG_A(T, 4));                                    \
                break;                                                       \
            case PNVG_PATH_CIRCLE:                                           \
                nvgCircle(vg, PNVG_A(T, 1), PNVG_A(T, 2), PNVG_A(T, 3));     \
                break;                                                       \
            case PNVG_PATH_RECT:                                             \
                nvgRect(vg, PNVG_A(T, 1), PNVG_A(T, 2), PNVG_A(T, 3),        \
                        PNVG_A(T, 4));                                       \
                break;                                                       \
            case PNVG_PATH_ROUNDEDRECT:                                      \
                nvgRoundedRect(vg, PNVG_A(T, 1), PNVG_A(T, 2), PNVG_A(T, 3), \
                               PNVG_A(T, 4), PNVG_A(T, 5));                  \
                break;                                                       \
            default:                                                         \
                nvgPathWinding(vg, (int)PNVG_A(T, 1));                       \
                break;                                                       \
            }                                                                \
        }                                                                    \
    } while (0)

int pnvg_path_matrix(const void *data, int n, int isSingle)
{
    NVGcontext *vg = g_state.vg;
    if (!vg)
        return fail(PNVG_E_NOTINIT, "call Init first");
    if (isSingle)
        PNVG_PATHV(float);
    else
        PNVG_PATHV(double);
    PNVG_ZONE("PathMatrix");
    if (isSingle)
        PNVG_PATHM(float);
    else
        PNVG_PATHM(double);
    PNVG_ZONE_END();
    return PNVG_OK;
}

/* ------------------------------------------------------------------ */
/* Gradient segments                                                   */
/* ------------------------------------------------------------------ */

/* The two matrices can differ in class. The class test is a predictable
 * branch and small next to one nvgStroke, so one reader serves all four
 * combinations instead of four copies of the loop. */
static float seg_at(const void *d, int isSingle, int n, int col, int row)
{
    size_t k = (size_t)col * (size_t)n + (size_t)row;
    return isSingle ? ((const float *)d)[k] : (float)((const double *)d)[k];
}

static NVGcolor seg_color(const void *d, int isSingle, int n, int col0,
                          int row)
{
    return nvgRGBAf(seg_at(d, isSingle, n, col0, row),
                    seg_at(d, isSingle, n, col0 + 1, row),
                    seg_at(d, isSingle, n, col0 + 2, row),
                    seg_at(d, isSingle, n, col0 + 3, row));
}

static int color_eq(NVGcolor a, NVGcolor b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

int pnvg_stroke_segments(const void *seg, int segSingle, const void *col,
                         int colSingle, int n)
{
    NVGcontext *vg = g_state.vg;
    NVGpaint saved;
    int i = 0;

    if (!vg)
        return fail(PNVG_E_NOTINIT, "call Init first");
    if (n <= 0)
        return PNVG_OK;
    PNVG_ZONE("StrokeSegments");
    /* The caller's stroke paint survives the call. nvgSave would do that
     * too, but it does nothing once the state stack is full, and the
     * matching nvgRestore would then pop the caller's own state. */
    pnvg_nvg_stroke_paint(vg, &saved, NULL);
    while (i < n) {
        float x0 = seg_at(seg, segSingle, n, 0, i);
        float y0 = seg_at(seg, segSingle, n, 1, i);
        float x1 = seg_at(seg, segSingle, n, 2, i);
        float y1 = seg_at(seg, segSingle, n, 3, i);
        NVGcolor c0 = seg_color(col, colSingle, n, 0, i);
        NVGcolor c1 = seg_color(col, colSingle, n, 4, i);

        nvgBeginPath(vg);
        nvgMoveTo(vg, x0, y0);
        nvgLineTo(vg, x1, y1);
        if (color_eq(c0, c1)) {
            /* A gradient buys nothing along a run of one color, and one
             * path gives the run real joins and one draw call. */
            while (i + 1 < n &&
                   seg_at(seg, segSingle, n, 0, i + 1) == x1 &&
                   seg_at(seg, segSingle, n, 1, i + 1) == y1 &&
                   color_eq(seg_color(col, colSingle, n, 0, i + 1), c0) &&
                   color_eq(seg_color(col, colSingle, n, 4, i + 1), c0)) {
                i++;
                x1 = seg_at(seg, segSingle, n, 2, i);
                y1 = seg_at(seg, segSingle, n, 3, i);
                nvgLineTo(vg, x1, y1);
            }
            nvgStrokeColor(vg, c0);
        } else {
            nvgStrokePaint(vg, nvgLinearGradient(vg, x0, y0, x1, y1, c0, c1));
        }
        nvgStroke(vg);
        i++;
    }
    pnvg_nvg_stroke_paint(vg, NULL, &saved);
    PNVG_ZONE_END();
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

static int target_stack_find(int rt)
{
    int i;
    for (i = 0; i < g_state.targetDepth; i++)
        if (g_state.targetStack[i] == rt)
            return i;
    return -1;
}

int pnvg_target_bind(int rt)
{
    void *fb = pnvg_target_ptr(rt);
    if (!fb)
        return fail(PNVG_E_HANDLE, "render target %d is not open", rt);
    if (target_stack_find(rt) >= 0)
        return fail(PNVG_E_FRAMESTATE,
                    "render target %d is already bound", rt);
    if (g_state.targetDepth >= PNVG_MAX_TARGETS)
        return fail(PNVG_E_RANGE, "render target binds nest more than %d deep",
                    PNVG_MAX_TARGETS);
    g_state.targetPrevFbo[rt - 1] = pnvg_gl_current_fbo();
    pnvg_gl_fb_bind(fb);
    g_state.targetStack[g_state.targetDepth++] = rt;
    return PNVG_OK;
}

int pnvg_target_unbind(void)
{
    int rt;
    if (g_state.targetDepth < 1)
        return fail(PNVG_E_FRAMESTATE, "no render target is bound");
    rt = g_state.targetStack[--g_state.targetDepth];
    pnvg_gl_fb_bind_raw(g_state.targetPrevFbo[rt - 1]);
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
    /* Deleting a bound target unwinds the stack down to it, so the
     * framebuffer that was current before it was bound comes back. */
    if (target_stack_find(rt) >= 0) {
        while (g_state.targetDepth > 0) {
            int top = g_state.targetStack[g_state.targetDepth - 1];
            pnvg_target_unbind();
            if (top == rt)
                break;
        }
    }
    pnvg_image_unmark(pnvg_gl_fb_image(fb));
    pnvg_gl_fb_delete(fb);
    g_state.targets[rt - 1] = NULL;
    return PNVG_OK;
}
