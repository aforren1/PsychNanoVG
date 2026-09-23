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

/* SPEC 1.2 had one context per process; phase 3 has one per Psychtoolbox
 * window. The states live on the heap because each holds the paint table
 * and the command statistics, about 30 KB, and most processes need one. */
static pnvg_state *g_ctx[PNVG_MAX_CONTEXTS];
static int g_nextId;

/* What pnvg_cur points at when no context is current. Nothing writes to it,
 * so its vg stays NULL and the per-call "is there a context" test is one
 * load with no NULL check on the pointer itself. */
static pnvg_state g_none;

pnvg_state *pnvg_cur = &g_none;

/* One buffer for the whole process rather than one per context, because an
 * Init that fails has no context left to hold its message. */
static char g_err[256];

pnvg_state *pnvg_state_get(void)
{
    return pnvg_cur;
}

const char *pnvg_last_error(void)
{
    return g_err[0] ? g_err : "no further information";
}

static int fail(int code, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof(g_err), fmt, ap);
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

static void paint_table_reset(pnvg_state *s)
{
    int i;
    for (i = 0; i < PNVG_MAX_PAINTS; i++) {
        s->paintNext[i] = (short)(i + 1);
        s->paintLive[i] = 0;
    }
    s->paintNext[PNVG_MAX_PAINTS - 1] = -1;
    s->paintFreeHead = 0;
}

int pnvg_init(int backend, int createFlags)
{
    pnvg_state *s;
    int slot;

    for (slot = 0; slot < PNVG_MAX_CONTEXTS; slot++)
        if (!g_ctx[slot])
            break;
    if (slot == PNVG_MAX_CONTEXTS)
        return fail(PNVG_E_RANGE,
                    "all %d contexts are in use. Shut down the ones that "
                    "belong to closed windows; PsychNanoVG('Shutdown', 'all') "
                    "shuts down every context", PNVG_MAX_CONTEXTS);
    if (backend != PNVG_BACKEND_NULL && !pnvg_gl_have_context())
        return fail(PNVG_E_NOGLCONTEXT,
                    "no OpenGL context is current on this thread");

    s = (pnvg_state *)calloc(1, sizeof(pnvg_state));
    if (!s)
        return fail(PNVG_E_GLINIT, "no memory for a new context");
    paint_table_reset(s);
    s->slot = slot;
#if PNVG_TRACY
    /* The MEX starts the profiler on its first call. A native program such
     * as smoke_gl has no such call, so Init does it too. */
    pnvg_prof_startup();
#endif

    if (backend == PNVG_BACKEND_NULL) {
        s->vg = pnvg_null_create(createFlags);
        if (!s->vg) {
            free(s);
            return fail(PNVG_E_GLINIT, "the null renderer could not start");
        }
        snprintf(s->glVersion, sizeof(s->glVersion), "none (null renderer)");
        snprintf(s->glRenderer, sizeof(s->glRenderer), "null");
        s->stencilBits = 8;
    } else {
        if (pnvg_gl_load() != 0) {
            free(s);
            return fail(PNVG_E_GLINIT,
                        "the OpenGL entry points could not be loaded");
        }
        s->glContext = pnvg_gl_current_context();
        pnvg_gl_query_info(s->glVersion, sizeof(s->glVersion),
                           s->glRenderer, sizeof(s->glRenderer),
                           &s->stencilBits);
        s->vg = pnvg_gl_create(backend, createFlags);
        if (!s->vg) {
            int code = fail(PNVG_E_GLINIT,
                            "nvgCreate failed for GL version %s",
                            s->glVersion);
            free(s);
            return code;
        }
        pnvg_gl_timer_reset(&s->timer, slot * PNVG_TIMER_SLOTS * 2);
    }
    s->backend = backend;
    s->createFlags = createFlags;
    s->id = ++g_nextId;
    g_ctx[slot] = s;
    pnvg_cur = s;
    pnvg_stats_reset();
    return PNVG_OK;
}

void pnvg_stats_reset(void)
{
    pnvg_state *s = pnvg_cur;
    if (!s->vg)
        return;
    memset(&s->stats, 0, sizeof(s->stats));
    if (s->backend == PNVG_BACKEND_NULL || !pnvg_gl_timer_available(&s->timer))
        s->stats.gpuNs = NAN;
}

pnvg_state *pnvg_context_get(int id)
{
    int i;
    /* A linear scan of sixteen slots. Only SetContext and Shutdown with a
     * handle come here, never the per-call path. */
    if (id <= 0)
        return NULL;
    for (i = 0; i < PNVG_MAX_CONTEXTS; i++)
        if (g_ctx[i] && g_ctx[i]->id == id)
            return g_ctx[i];
    return NULL;
}

int pnvg_context_set(int id)
{
    pnvg_state *s;
    if (id == 0) {
        pnvg_cur = &g_none;
        return PNVG_OK;
    }
    s = pnvg_context_get(id);
    if (!s)
        return fail(PNVG_E_HANDLE, "%d is not an open context", id);
    pnvg_cur = s;
    return PNVG_OK;
}

int pnvg_context_count(void)
{
    int i, n = 0;
    for (i = 0; i < PNVG_MAX_CONTEXTS; i++)
        if (g_ctx[i])
            n++;
    return n;
}

int pnvg_context_list(int *ids, int max)
{
    int i, j, n = 0;
    for (i = 0; i < PNVG_MAX_CONTEXTS; i++)
        if (g_ctx[i] && n < max)
            ids[n++] = g_ctx[i]->id;
    /* Slots are reused, so slot order is not age order; ids are. */
    for (i = 1; i < n; i++) {
        int t = ids[i];
        for (j = i - 1; j >= 0 && ids[j] > t; j--)
            ids[j + 1] = ids[j];
        ids[j + 1] = t;
    }
    return n;
}

int pnvg_context_check_gl(const pnvg_state *s)
{
    void *cur;
    if (s->backend == PNVG_BACKEND_NULL)
        return PNVG_OK;
    cur = pnvg_gl_current_context();
    if (!cur)
        return fail(PNVG_E_NOGLCONTEXT,
                    "no OpenGL context is current on this thread");
    if (cur != s->glContext)
        return fail(PNVG_E_CONTEXT,
                    "the OpenGL context that is current is not the one of "
                    "context %d. Call Screen('BeginOpenGL') for the window "
                    "of context %d, or PsychNanoVG('SetContext') with the "
                    "context of this window", s->id, s->id);
    return PNVG_OK;
}

int pnvg_context_destroy(pnvg_state *s, int *leftToDriver)
{
    int i, gl;
    if (leftToDriver)
        *leftToDriver = 0;
    if (!s || !s->vg)
        return fail(PNVG_E_NOTINIT, "no context to shut down");

    /* GL deletes run only in the context's own GL context. In another one
     * they would delete that context's objects of the same name. */
    gl = s->backend != PNVG_BACKEND_NULL && s->glContext &&
         pnvg_gl_current_context() == s->glContext;
    if (s->backend != PNVG_BACKEND_NULL && !gl && leftToDriver)
        *leftToDriver = 1;

    if (gl && s->targetDepth > 0)
        /* A bound target would leave the window without its framebuffer. */
        pnvg_gl_fb_bind_raw(s->targetPrevFbo[s->targetStack[0] - 1]);
    for (i = 0; i < PNVG_MAX_TARGETS; i++) {
        if (!s->targets[i])
            continue;
        if (gl)
            pnvg_gl_fb_delete(s->targets[i]);
        else
            pnvg_gl_fb_free_nogl(s->targets[i]);
        s->targets[i] = NULL;
    }
    /* NanoVG deletes its own images and fonts with the context. */
    if (s->backend == PNVG_BACKEND_NULL) {
        pnvg_null_delete(s->vg);
    } else if (gl) {
        pnvg_gl_timer_release(&s->timer);
        pnvg_gl_destroy(s->vg, s->backend);
    } else {
        pnvg_gl_destroy_nogl(s->vg);
    }

    g_ctx[s->slot] = NULL;
    if (pnvg_cur == s)
        pnvg_cur = &g_none;
    free(s->scratch);
    free(s);
    return PNVG_OK;
}

int pnvg_shutdown(void)
{
    return pnvg_context_destroy(pnvg_cur, NULL);
}

int pnvg_begin_frame(int w, int h, float pixelRatio)
{
    pnvg_state *s = pnvg_cur;
    int st;
    if (!s->vg)
        return fail(PNVG_E_NOTINIT, "call Init first");
    if (s->inFrame)
        return fail(PNVG_E_FRAMESTATE, "BeginFrame inside a frame");
    if (w <= 0 || h <= 0 || pixelRatio <= 0.0f)
        return fail(PNVG_E_RANGE, "BeginFrame needs positive w, h, pixelRatio");

    if (s->backend != PNVG_BACKEND_NULL) {
        /* The frame's GL work would otherwise land in another window's
         * context, where this context's program and buffer names mean
         * nothing or mean something else. */
        st = pnvg_context_check_gl(s);
        if (st != PNVG_OK)
            return st;
        pnvg_gl_save(&s->saved);
        pnvg_gl_viewport(0, 0, w, h);
        pnvg_gl_timer_begin(&s->timer);
    }
    nvgBeginFrame(s->vg, (float)w, (float)h, pixelRatio);
    s->inFrame = 1;
    s->frameW = w;
    s->frameH = h;
    s->pixelRatio = pixelRatio;
    return PNVG_OK;
}

int pnvg_end_frame(void)
{
    pnvg_state *s = pnvg_cur;
    double t0, dt;
    int dc = 0, fc = 0, sc = 0, tc = 0, st;

    if (!s->vg)
        return fail(PNVG_E_NOTINIT, "call Init first");
    if (!s->inFrame)
        return fail(PNVG_E_FRAMESTATE, "EndFrame without BeginFrame");
    /* The frame stays open on a mismatch, so the script can make the right
     * GL context current and end the frame there. */
    if (s->backend != PNVG_BACKEND_NULL) {
        st = pnvg_context_check_gl(s);
        if (st != PNVG_OK)
            return st;
    }

    PNVG_ZONE("nvgEndFrame");
    t0 = pnvg_now_ns();
    nvgEndFrame(s->vg);
    dt = pnvg_now_ns() - t0;
    PNVG_ZONE_END();
    s->inFrame = 0;

    pnvg_nvg_counters(s->vg, &dc, &fc, &sc, &tc);
    s->stats.frames += 1.0;
    s->stats.endFrameNs = dt;
    s->stats.endFrameSumNs += dt;
    if (dt > s->stats.endFrameMaxNs)
        s->stats.endFrameMaxNs = dt;
    s->stats.drawCalls = dc;
    s->stats.fillCount = fc;
    s->stats.strokeCount = sc;
    s->stats.textCount = tc;
    s->stats.vertexCount = (double)(fc + sc + tc) * 3.0;

    if (s->backend != PNVG_BACKEND_NULL) {
        unsigned int e;
        /* Separate zones, because the driver can do its submission work in
         * any of these calls rather than in nvgEndFrame. */
        PNVG_ZONE("GPU timer");
        pnvg_gl_timer_end(&s->timer);
        if (pnvg_gl_timer_available(&s->timer))
            s->stats.gpuNs = pnvg_gl_timer_read(&s->timer);
        PNVG_ZONE_END();
        PNVG_ZONE("GL restore and error drain");
        pnvg_gl_restore(&s->saved);
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
    pnvg_state *s = pnvg_cur;
    int st;
    if (!s->vg)
        return fail(PNVG_E_NOTINIT, "call Init first");
    if (!s->inFrame)
        return fail(PNVG_E_FRAMESTATE, "CancelFrame without BeginFrame");
    if (s->backend != PNVG_BACKEND_NULL) {
        st = pnvg_context_check_gl(s);
        if (st != PNVG_OK)
            return st;
    }
    nvgCancelFrame(s->vg);
    s->inFrame = 0;
    if (s->backend != PNVG_BACKEND_NULL) {
        /* BeginFrame wrote the first timestamp and, with Tracy, opened a GPU
         * zone. Both need their end, or the ring and Tracy disagree. */
        pnvg_gl_timer_end(&s->timer);
        if (pnvg_gl_timer_available(&s->timer))
            s->stats.gpuNs = pnvg_gl_timer_read(&s->timer);
        pnvg_gl_restore(&s->saved);
    }
    return PNVG_OK;
}

/* ------------------------------------------------------------------ */
/* Paint table                                                         */
/* ------------------------------------------------------------------ */

int pnvg_paint_alloc(const NVGpaint *p)
{
    int idx = pnvg_cur->paintFreeHead;
    if (idx < 0)
        return -1;
    pnvg_cur->paintFreeHead = pnvg_cur->paintNext[idx];
    pnvg_cur->paints[idx] = *p;
    pnvg_cur->paintLive[idx] = 1;
    return idx + 1;
}

int pnvg_paint_fetch(int idx, NVGpaint *out)
{
    if (idx < 1 || idx > PNVG_MAX_PAINTS || !pnvg_cur->paintLive[idx - 1])
        return PNVG_E_HANDLE;
    *out = pnvg_cur->paints[idx - 1];
    return PNVG_OK;
}

int pnvg_paint_release(int idx)
{
    if (idx < 1 || idx > PNVG_MAX_PAINTS || !pnvg_cur->paintLive[idx - 1])
        return PNVG_E_HANDLE;
    pnvg_cur->paintLive[idx - 1] = 0;
    pnvg_cur->paintNext[idx - 1] = pnvg_cur->paintFreeHead;
    pnvg_cur->paintFreeHead = (short)(idx - 1);
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
    if (id >= 0 && id >= pnvg_cur->fontCount)
        pnvg_cur->fontCount = id + 1;
}

int pnvg_font_is_live(int id)
{
    return id >= 0 && id < pnvg_cur->fontCount;
}

void pnvg_image_mark(int id)
{
    if (id > 0 && id < PNVG_MAX_IMAGES)
        pnvg_cur->imageLive[id >> 5] |= 1u << (id & 31);
}

void pnvg_image_unmark(int id)
{
    if (id > 0 && id < PNVG_MAX_IMAGES)
        pnvg_cur->imageLive[id >> 5] &= ~(1u << (id & 31));
}

int pnvg_image_is_live(int id)
{
    if (id <= 0 || id >= PNVG_MAX_IMAGES)
        return 0;
    return (pnvg_cur->imageLive[id >> 5] >> (id & 31)) & 1u;
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
    if (pnvg_cur->scratchBytes < need) {
        unsigned char *p = (unsigned char *)realloc(pnvg_cur->scratch, need);
        if (!p) {
            PNVG_ZONE_END();
            return NULL;
        }
        pnvg_cur->scratch = p;
        pnvg_cur->scratchBytes = need;
    }
    d = pnvg_cur->scratch;
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
    return pnvg_cur->scratch;
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
    NVGcontext *vg = pnvg_cur->vg;
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
    NVGcontext *vg = pnvg_cur->vg;
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
    NVGcontext *vg = pnvg_cur->vg;
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
    NVGcontext *vg = pnvg_cur->vg;
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
    NVGcontext *vg = pnvg_cur->vg;
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
    if (!pnvg_cur->vg) {
        fail(PNVG_E_NOTINIT, "call Init first");
        return -1;
    }
    if (pnvg_cur->backend == PNVG_BACKEND_NULL) {
        fail(PNVG_E_GLINIT, "render targets need a GL renderer");
        return -1;
    }
    for (i = 0; i < PNVG_MAX_TARGETS; i++)
        if (!pnvg_cur->targets[i])
            break;
    if (i == PNVG_MAX_TARGETS) {
        fail(PNVG_E_RANGE, "all %d render target slots are in use",
             PNVG_MAX_TARGETS);
        return -1;
    }
    fb = pnvg_gl_fb_create(pnvg_cur->vg, w, h, imageFlags);
    if (!fb) {
        fail(PNVG_E_GLERROR, "nvgluCreateFramebuffer failed for %dx%d", w, h);
        return -1;
    }
    pnvg_cur->targets[i] = fb;
    pnvg_cur->targetPrevFbo[i] = 0;
    pnvg_image_mark(pnvg_gl_fb_image(fb));
    return i + 1;
}

void *pnvg_target_ptr(int rt)
{
    if (rt < 1 || rt > PNVG_MAX_TARGETS)
        return NULL;
    return pnvg_cur->targets[rt - 1];
}

static int target_stack_find(int rt)
{
    int i;
    for (i = 0; i < pnvg_cur->targetDepth; i++)
        if (pnvg_cur->targetStack[i] == rt)
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
    if (pnvg_cur->targetDepth >= PNVG_MAX_TARGETS)
        return fail(PNVG_E_RANGE, "render target binds nest more than %d deep",
                    PNVG_MAX_TARGETS);
    pnvg_cur->targetPrevFbo[rt - 1] = pnvg_gl_current_fbo();
    pnvg_gl_fb_bind(fb);
    pnvg_cur->targetStack[pnvg_cur->targetDepth++] = rt;
    return PNVG_OK;
}

int pnvg_target_unbind(void)
{
    int rt;
    if (pnvg_cur->targetDepth < 1)
        return fail(PNVG_E_FRAMESTATE, "no render target is bound");
    rt = pnvg_cur->targetStack[--pnvg_cur->targetDepth];
    pnvg_gl_fb_bind_raw(pnvg_cur->targetPrevFbo[rt - 1]);
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
        while (pnvg_cur->targetDepth > 0) {
            int top = pnvg_cur->targetStack[pnvg_cur->targetDepth - 1];
            pnvg_target_unbind();
            if (top == rt)
                break;
        }
    }
    pnvg_image_unmark(pnvg_gl_fb_image(fb));
    pnvg_gl_fb_delete(fb);
    pnvg_cur->targets[rt - 1] = NULL;
    return PNVG_OK;
}
