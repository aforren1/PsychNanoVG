/* The GL implementation unit: glad, the NanoVG GL backend, the proc loader,
 * and the GL state save and restore that SPEC R4 requires.
 *
 * This is the only file that sees a GL header. Everything else in the project
 * goes through pnvg_gl.h, so the MEX and the core layer never need a GL
 * include path.
 */

#if defined(__linux__) && !defined(_GNU_SOURCE)
/* RTLD_DEFAULT, for the GLX fallback of the GLES build. */
#  define _GNU_SOURCE
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* PNVG_GLES selects a GLES backend on Linux (CMake PSYCHNANOVG_GLES). glad
 * then loads the GLES 2.0 and 3.0 entry points instead of desktop GL 3.3;
 * the two headers declare the same names, so a build has one or the other. */
#if defined(PNVG_GLES)
#  include <glad/gles2.h>
#else
#  include <glad/gl.h>
#endif

#include "nanovg.h"

#if defined(PNVG_GL2)
#  define NANOVG_GL2_IMPLEMENTATION
#  define PNVG_NVG_CREATE      nvgCreateGL2
#  define PNVG_NVG_DELETE      nvgDeleteGL2
#  define PNVG_NVG_FROM_HANDLE nvglCreateImageFromHandleGL2
#  define PNVG_NVG_HANDLE      nvglImageHandleGL2
#elif defined(PNVG_GLES) && PNVG_GLES == 2
#  define NANOVG_GLES2_IMPLEMENTATION
#  define PNVG_NVG_CREATE      nvgCreateGLES2
#  define PNVG_NVG_DELETE      nvgDeleteGLES2
#  define PNVG_NVG_FROM_HANDLE nvglCreateImageFromHandleGLES2
#  define PNVG_NVG_HANDLE      nvglImageHandleGLES2
#elif defined(PNVG_GLES) && PNVG_GLES == 3
#  define NANOVG_GLES3_IMPLEMENTATION
#  define PNVG_NVG_CREATE      nvgCreateGLES3
#  define PNVG_NVG_DELETE      nvgDeleteGLES3
#  define PNVG_NVG_FROM_HANDLE nvglCreateImageFromHandleGLES3
#  define PNVG_NVG_HANDLE      nvglImageHandleGLES3
#elif defined(PNVG_GLES)
#  error "PNVG_GLES must be 2 or 3"
#else
#  define NANOVG_GL3_IMPLEMENTATION
#  define PNVG_NVG_CREATE      nvgCreateGL3
#  define PNVG_NVG_DELETE      nvgDeleteGL3
#  define PNVG_NVG_FROM_HANDLE nvglCreateImageFromHandleGL3
#  define PNVG_NVG_HANDLE      nvglImageHandleGL3
#endif
#include "nanovg_gl.h"

#if defined(__APPLE__) && defined(PNVG_GL2)
/* nanovg_gl_utils.h includes <OpenGL/glext.h> on an Apple GL2 build, to get
 * the framebuffer object entry points. glad already declares those, and the
 * two sets of declarations do not agree, because glad turns each name into a
 * function pointer. Setting the header's own include guards first makes that
 * include expand to nothing; the NANOVG_FBO_VALID define next to it still
 * happens, and glad supplies the entry points through
 * GL_ARB_framebuffer_object. */
#  ifndef __glext_h_
#    define __glext_h_ 1
#  endif
#  ifndef __gl_glext_h_
#    define __gl_glext_h_ 1
#  endif
#endif
#include "nanovg_gl_utils.h"

#include "core/pnvg_core.h"
#include "pnvg_profiler.h"

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#elif defined(__APPLE__)
#  include <dlfcn.h>
#else
#  include <dlfcn.h>
#endif

/* ------------------------------------------------------------------ */
/* Proc address loading                                                */
/* ------------------------------------------------------------------ */

#if defined(_WIN32)

static HMODULE g_opengl32;

static GLADapiproc pnvg_get_proc(const char *name)
{
    PROC p = wglGetProcAddress(name);
    /* Some drivers return these sentinel values instead of NULL for the
     * GL 1.1 entry points, which live in opengl32.dll rather than the ICD. */
    if (p == NULL || p == (PROC)1 || p == (PROC)2 || p == (PROC)3 ||
        p == (PROC)-1) {
        if (!g_opengl32)
            g_opengl32 = LoadLibraryA("opengl32.dll");
        if (!g_opengl32)
            return NULL;
        p = GetProcAddress(g_opengl32, name);
    }
    return (GLADapiproc)p;
}

void *pnvg_gl_current_context(void)
{
    return (void *)wglGetCurrentContext();
}

#elif defined(__APPLE__)

static void *g_glframework;

static GLADapiproc pnvg_get_proc(const char *name)
{
    if (!g_glframework)
        g_glframework = dlopen(
            "/System/Library/Frameworks/OpenGL.framework/OpenGL",
            RTLD_LAZY | RTLD_LOCAL);
    if (!g_glframework)
        return NULL;
    return (GLADapiproc)dlsym(g_glframework, name);
}

extern void *CGLGetCurrentContext(void);

void *pnvg_gl_current_context(void)
{
    return CGLGetCurrentContext();
}

#elif defined(PNVG_GLES)

/* Psychtoolbox makes GLES contexts only through its Waffle display backends
 * (SPEC 14.8), which use EGL on Wayland and X11/EGL and GLX on X11/GLX. Both
 * libraries are opened at run time, so the build needs neither the EGL
 * development files nor a link against libEGL, and a process that already
 * has them loaded gets the same instance back. */
typedef void *(*pnvg_pfn_current)(void);
typedef GLADapiproc (*pnvg_pfn_getproc)(const char *name);

static int g_eglTried;
static void *g_libegl;
static void *g_libgles;
static pnvg_pfn_current p_eglGetCurrentContext;
static pnvg_pfn_getproc p_eglGetProcAddress;
static pnvg_pfn_current p_glXGetCurrentContext;

static void egl_open(void)
{
    if (g_eglTried)
        return;
    g_eglTried = 1;
    g_libegl = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (g_libegl) {
        p_eglGetCurrentContext =
            (pnvg_pfn_current)dlsym(g_libegl, "eglGetCurrentContext");
        p_eglGetProcAddress =
            (pnvg_pfn_getproc)dlsym(g_libegl, "eglGetProcAddress");
    }
    /* Only when libGL is already in the process, which it is under a
     * Waffle GLX backend. */
    p_glXGetCurrentContext =
        (pnvg_pfn_current)dlsym(RTLD_DEFAULT, "glXGetCurrentContext");
}

static GLADapiproc pnvg_get_proc(const char *name)
{
    GLADapiproc p = NULL;
    egl_open();
    /* EGL 1.5 and EGL_KHR_get_all_proc_addresses return core entry points
     * too. An older EGL returns only extensions, so the core ones then come
     * from the GLES library itself. */
    if (p_eglGetProcAddress)
        p = p_eglGetProcAddress(name);
    if (!p) {
        if (!g_libgles)
            g_libgles = dlopen("libGLESv2.so.2", RTLD_LAZY | RTLD_LOCAL);
        if (g_libgles)
            p = (GLADapiproc)dlsym(g_libgles, name);
    }
    return p;
}

void *pnvg_gl_current_context(void)
{
    void *c = NULL;
    egl_open();
    if (p_eglGetCurrentContext)
        c = p_eglGetCurrentContext();   /* EGL_NO_CONTEXT is 0 */
    if (!c && p_glXGetCurrentContext)
        c = p_glXGetCurrentContext();
    return c;
}

#else /* X11 and Linux */

/* Declared here rather than through GL/glx.h so that the build needs no GLX
 * development headers; libGL exports both symbols. */
extern GLADapiproc glXGetProcAddressARB(const unsigned char *name);
extern void *glXGetCurrentContext(void);

static GLADapiproc pnvg_get_proc(const char *name)
{
    return glXGetProcAddressARB((const unsigned char *)name);
}

void *pnvg_gl_current_context(void)
{
    return glXGetCurrentContext();
}

#endif

int pnvg_gl_have_context(void)
{
    return pnvg_gl_current_context() != NULL;
}

/* The entry points are loaded once per process, not once per context. Every
 * Psychtoolbox window of one process runs on one driver, and Psychtoolbox
 * itself initializes GLEW once in the same way. */
static int g_loaded;

int pnvg_gl_load(void)
{
    if (g_loaded)
        return 0;
#if defined(PNVG_GLES)
    if (gladLoadGLES2(pnvg_get_proc) == 0)
        return 1;
#else
    if (gladLoadGL(pnvg_get_proc) == 0)
        return 1;
#endif
    g_loaded = 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Context                                                             */
/* ------------------------------------------------------------------ */

void pnvg_gl_query_info(char *ver, size_t nver, char *rend, size_t nrend,
                        int *stencilBits)
{
    const GLubyte *s;
    GLint fbo = 0, bits = 0;

    s = glGetString(GL_VERSION);
    snprintf(ver, nver, "%s", s ? (const char *)s : "unknown");
    s = glGetString(GL_RENDERER);
    snprintf(rend, nrend, "%s", s ? (const char *)s : "unknown");

    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
#if defined(PNVG_GLES)
    /* GLES keeps GL_STENCIL_BITS for the bound framebuffer, whether it is
     * the default one or an object, and GLES 2.0 has no attachment size
     * query. */
    (void)fbo;
    glGetIntegerv(GL_STENCIL_BITS, &bits);
#else
    if (fbo != 0) {
        glGetFramebufferAttachmentParameteriv(
            GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
            GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE, &bits);
    } else {
        glGetIntegerv(GL_STENCIL_BITS, &bits);
    }
#endif
    /* A missing stencil attachment is a legitimate query failure, so clear
     * the error rather than leaving it for Screen('EndOpenGL'). */
    while (glGetError() != GL_NO_ERROR)
        ;
    *stencilBits = (int)bits;
}

struct NVGcontext *pnvg_gl_create(int backend, int flags)
{
    (void)backend;
    return PNVG_NVG_CREATE(flags);
}

void pnvg_gl_destroy(struct NVGcontext *vg, int backend)
{
    (void)backend;
    if (!vg)
        return;
    PNVG_NVG_DELETE(vg);
}

/* The two backend callbacks that issue GL calls during nvgDeleteInternal,
 * replaced by versions that only free memory. The field list follows
 * glnvg__renderDelete in nanovg_gl.h. */
static int nogl_delete_texture(void *uptr, int image)
{
    (void)uptr;
    (void)image;
    return 1;
}

static void nogl_render_delete(void *uptr)
{
    GLNVGcontext *gl = (GLNVGcontext *)uptr;
    if (!gl)
        return;
    free(gl->textures);
    free(gl->paths);
    free(gl->verts);
    free(gl->uniforms);
    free(gl->calls);
    free(gl);
}

void pnvg_gl_destroy_nogl(struct NVGcontext *vg)
{
    NVGparams *params;
    if (!vg)
        return;
    params = nvgInternalParams(vg);
    params->renderDeleteTexture = nogl_delete_texture;
    params->renderDelete = nogl_render_delete;
    nvgDeleteInternal(vg);
}

/* ------------------------------------------------------------------ */
/* State save and restore (SPEC R4, 8.3)                               */
/* ------------------------------------------------------------------ */

void pnvg_gl_save(pnvg_glstate *s)
{
    GLint v[4];
    GLint x;
    glGetIntegerv(GL_VIEWPORT, v);
    s->viewport[0] = v[0]; s->viewport[1] = v[1];
    s->viewport[2] = v[2]; s->viewport[3] = v[3];
    s->blendEnabled = glIsEnabled(GL_BLEND) ? 1 : 0;
    glGetIntegerv(GL_BLEND_SRC_RGB, &x);   s->srcRGB = x;
    glGetIntegerv(GL_BLEND_DST_RGB, &x);   s->dstRGB = x;
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &x); s->srcAlpha = x;
    glGetIntegerv(GL_BLEND_DST_ALPHA, &x); s->dstAlpha = x;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &x);  s->activeTexture = x;
    s->valid = 1;
}

void pnvg_gl_restore(const pnvg_glstate *s)
{
    if (!s->valid)
        return;
    glViewport(s->viewport[0], s->viewport[1], s->viewport[2], s->viewport[3]);
    glBlendFuncSeparate((GLenum)s->srcRGB, (GLenum)s->dstRGB,
                        (GLenum)s->srcAlpha, (GLenum)s->dstAlpha);
    if (s->blendEnabled)
        glEnable(GL_BLEND);
    else
        glDisable(GL_BLEND);
    glActiveTexture((GLenum)s->activeTexture);
}

void pnvg_gl_viewport(int x, int y, int w, int h)
{
    glViewport(x, y, w, h);
}

unsigned int pnvg_gl_drain_error(void)
{
    GLenum first = GL_NO_ERROR, e;
    while ((e = glGetError()) != GL_NO_ERROR) {
        if (first == GL_NO_ERROR)
            first = e;
    }
    return (unsigned int)first;
}

const char *pnvg_gl_error_name(unsigned int e)
{
    switch (e) {
    case GL_INVALID_ENUM:      return "GL_INVALID_ENUM";
    case GL_INVALID_VALUE:     return "GL_INVALID_VALUE";
    case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
    case GL_OUT_OF_MEMORY:     return "GL_OUT_OF_MEMORY";
    case GL_INVALID_FRAMEBUFFER_OPERATION:
        return "GL_INVALID_FRAMEBUFFER_OPERATION";
#if defined(GL_STACK_OVERFLOW)
    case GL_STACK_OVERFLOW:    return "GL_STACK_OVERFLOW";
    case GL_STACK_UNDERFLOW:   return "GL_STACK_UNDERFLOW";
#endif
    default:                   return "an unknown error";
    }
}

/* ------------------------------------------------------------------ */
/* Images and render targets                                           */
/* ------------------------------------------------------------------ */

int pnvg_gl_image_from_handle(struct NVGcontext *vg, unsigned int tex,
                              int w, int h, int flags, int backend)
{
    (void)backend;
    return PNVG_NVG_FROM_HANDLE(vg, (GLuint)tex, w, h, flags);
}

unsigned int pnvg_gl_image_handle(struct NVGcontext *vg, int image, int backend)
{
    (void)backend;
    return (unsigned int)PNVG_NVG_HANDLE(vg, image);
}

void *pnvg_gl_fb_create(struct NVGcontext *vg, int w, int h, int imageFlags)
{
    return nvgluCreateFramebuffer(vg, w, h, imageFlags);
}

void pnvg_gl_fb_delete(void *fb)
{
    nvgluDeleteFramebuffer((NVGLUframebuffer *)fb);
}

void pnvg_gl_fb_free_nogl(void *fb)
{
    free(fb);
}

void pnvg_gl_fb_bind(void *fb)
{
    nvgluBindFramebuffer((NVGLUframebuffer *)fb);
}

void pnvg_gl_fb_bind_raw(int fbo)
{
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo);
}

int pnvg_gl_fb_image(void *fb)
{
    return fb ? ((NVGLUframebuffer *)fb)->image : -1;
}

unsigned int pnvg_gl_fb_texture(void *fb)
{
    return fb ? (unsigned int)((NVGLUframebuffer *)fb)->texture : 0u;
}

int pnvg_gl_current_fbo(void)
{
    GLint fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    return (int)fbo;
}

/* ------------------------------------------------------------------ */
/* GPU timing (SPEC 9.2) and the Tracy GPU zone (SPEC 9.3)             */
/* ------------------------------------------------------------------ */

#if !defined(PNVG_GLES)

#if PNVG_TRACY
/* One Tracy GPU context per process. A new id per Init would use up the 255
 * that Tracy has, and the GL timestamp base is the same device either way.
 * Each NanoVG context's ring takes its own range of query ids in it. */
static uint8_t g_tracyCtx;
static int g_tracyCtxReady;
static int g_tracyCtxFailed;
static const pnvg_srcloc g_gpuFrameLoc = {
    "NanoVG frame", "pnvg_gl_timer_begin", __FILE__, (uint32_t)__LINE__, 0};

unsigned char pnvg_tracy_gpu_context(void);   /* pnvg_tracy.cpp */

static void tracy_gpu_context(void)
{
    GLint64 t = 0;
    struct ___tracy_gpu_new_context_data nc;
    struct ___tracy_gpu_context_name_data nm;
    static const char name[] = "PsychNanoVG";

    if (g_tracyCtxReady || g_tracyCtxFailed || !pnvg_prof_started())
        return;
    /* The context needs a GPU timestamp to align with the CPU clock, and
     * GL_ARB_timer_query alone does not bring glGetInteger64v. */
    if (!glad_glGetInteger64v) {
        g_tracyCtxFailed = 1;
        return;
    }
    glGetInteger64v(GL_TIMESTAMP, &t);
    g_tracyCtx = pnvg_tracy_gpu_context();
    nc.gpuTime = (int64_t)t;
    nc.period = 1.0f;
    nc.context = g_tracyCtx;
    nc.flags = 0;
    nc.type = 1;   /* tracy::GpuContextType::OpenGl */
    ___tracy_emit_gpu_new_context_serial(nc);
    nm.context = g_tracyCtx;
    nm.name = name;
    nm.len = (uint16_t)(sizeof(name) - 1);
    ___tracy_emit_gpu_context_name_serial(nm);
    g_tracyCtxReady = 1;
}

static void tracy_gpu_times(pnvg_gltimer *t, int slot, GLuint64 t0,
                            GLuint64 t1)
{
    struct ___tracy_gpu_time_data d;
    if (!t->tracySlot[slot])
        return;
    d.context = g_tracyCtx;
    d.queryId = (uint16_t)(t->qidBase + slot * 2);
    d.gpuTime = (int64_t)t0;
    ___tracy_emit_gpu_time_serial(d);
    d.queryId = (uint16_t)(t->qidBase + slot * 2 + 1);
    d.gpuTime = (int64_t)t1;
    ___tracy_emit_gpu_time_serial(d);
    t->tracySlot[slot] = 0;
}
#endif

static int has_extension(const char *name)
{
    /* GL_EXTENSIONS through glGetString is legal in the compatibility and
     * 2.1 contexts that Psychtoolbox makes. Whole tokens only, so that a
     * longer name that starts the same does not match. */
    const char *ext = (const char *)glGetString(GL_EXTENSIONS);
    size_t n = strlen(name);
    while (ext && (ext = strstr(ext, name)) != NULL) {
        if (ext[n] == ' ' || ext[n] == '\0')
            return 1;
        ext += n;
    }
    return 0;
}

static void timer_init(pnvg_gltimer *t)
{
    if (t->ready)
        return;
    /* glad loads the GL 3.3 entry points only from a 3.3 context. On an
     * older context with GL_ARB_timer_query the same unsuffixed names exist,
     * so they are loaded here by hand. Without either, the pointers stay
     * null, nothing below calls them, and Stats reports NaN. */
    if (!glad_glQueryCounter && has_extension("GL_ARB_timer_query")) {
        glad_glQueryCounter = (PFNGLQUERYCOUNTERPROC)pnvg_get_proc("glQueryCounter");
        glad_glGetQueryObjectui64v = (PFNGLGETQUERYOBJECTUI64VPROC)
            pnvg_get_proc("glGetQueryObjectui64v");
    }
    if (!glad_glQueryCounter || !glad_glGetQueryObjectui64v ||
        !glad_glGenQueries || !glad_glGetQueryObjectiv ||
        !glad_glDeleteQueries)
        return;
    glGenQueries(PNVG_TIMER_SLOTS * 2, &t->q[0][0]);
    t->ready = 1;
}

void pnvg_gl_timer_reset(pnvg_gltimer *t, int qidBase)
{
    memset(t, 0, sizeof(*t));
    t->qidBase = qidBase;
    timer_init(t);
#if PNVG_TRACY
    if (t->ready)
        tracy_gpu_context();
#endif
}

int pnvg_gl_timer_available(const pnvg_gltimer *t)
{
    return t->ready;
}

/* Reads one slot. With wait set it blocks until the GPU is done. */
static int timer_collect(pnvg_gltimer *t, int slot, int wait)
{
    GLuint64 t0 = 0, t1 = 0;
    if (!wait) {
        GLint a0 = 0, a1 = 0;
        glGetQueryObjectiv(t->q[slot][0], GL_QUERY_RESULT_AVAILABLE, &a0);
        glGetQueryObjectiv(t->q[slot][1], GL_QUERY_RESULT_AVAILABLE, &a1);
        if (!a0 || !a1)
            return 0;
    }
    glGetQueryObjectui64v(t->q[slot][0], GL_QUERY_RESULT, &t0);
    glGetQueryObjectui64v(t->q[slot][1], GL_QUERY_RESULT, &t1);
    t->gpuNs = (double)(t1 - t0);
    t->filled[slot] = 0;
#if PNVG_TRACY
    tracy_gpu_times(t, slot, t0, t1);
#endif
    return 1;
}

void pnvg_gl_timer_begin(pnvg_gltimer *t)
{
    if (!t->ready)
        return;
    glQueryCounter(t->q[t->slot][0], GL_TIMESTAMP);
    t->open = 1;
#if PNVG_TRACY
    if (g_tracyCtxReady && pnvg_prof_started()) {
        struct ___tracy_gpu_zone_begin_data d;
        d.srcloc = (uint64_t)(uintptr_t)&g_gpuFrameLoc;
        d.queryId = (uint16_t)(t->qidBase + t->slot * 2);
        d.context = g_tracyCtx;
        ___tracy_emit_gpu_zone_begin_serial(d);
        t->tracySlot[t->slot] = 1;
    }
#endif
}

void pnvg_gl_timer_end(pnvg_gltimer *t)
{
    int read;
    if (!t->ready || !t->open)
        return;
    glQueryCounter(t->q[t->slot][1], GL_TIMESTAMP);
    t->filled[t->slot] = 1;
    t->open = 0;
#if PNVG_TRACY
    if (t->tracySlot[t->slot]) {
        struct ___tracy_gpu_zone_end_data d;
        d.queryId = (uint16_t)(t->qidBase + t->slot * 2 + 1);
        d.context = g_tracyCtx;
        ___tracy_emit_gpu_zone_end_serial(d);
    }
#endif

    /* The oldest slot, two frames back, is the next one to be written. */
    read = (t->slot + 1) % PNVG_TIMER_SLOTS;
    if (t->filled[read]) {
        int wait = 0;
#if PNVG_TRACY
        /* Tracy was told that this zone began and ended, and waits for both
         * timestamps. The next BeginFrame overwrites the slot, so a late
         * pair is waited for here instead of dropped. Without Tracy it is
         * dropped, and Stats keeps the previous value. */
        wait = t->tracySlot[read];
#endif
        timer_collect(t, read, wait);
    }
    t->slot = (t->slot + 1) % PNVG_TIMER_SLOTS;
}

void pnvg_gl_timer_release(pnvg_gltimer *t)
{
    int k;
    if (!t->ready)
        return;
    if (t->open)
        pnvg_gl_timer_end(t);
    /* Tracy would otherwise keep the last frames as zones without an end. */
    for (k = 0; k < PNVG_TIMER_SLOTS; k++) {
#if PNVG_TRACY
        if (t->filled[k] && t->tracySlot[k])
            timer_collect(t, k, 1);
#endif
        t->filled[k] = 0;
    }
    glDeleteQueries(PNVG_TIMER_SLOTS * 2, &t->q[0][0]);
    t->ready = 0;
}

double pnvg_gl_timer_read(const pnvg_gltimer *t)
{
    return t->gpuNs;
}

#else /* PNVG_GLES */

/* GLES has no GL_TIMESTAMP query in core; EXT_disjoint_timer_query is
 * optional and rare on the embedded drivers that need GLES at all. Stats
 * reports gpuNs as NaN and Tracy gets no GPU zone. */
void pnvg_gl_timer_reset(pnvg_gltimer *t, int qidBase)
{
    memset(t, 0, sizeof(*t));
    t->qidBase = qidBase;
}

int pnvg_gl_timer_available(const pnvg_gltimer *t)
{
    (void)t;
    return 0;
}

void pnvg_gl_timer_begin(pnvg_gltimer *t) { (void)t; }
void pnvg_gl_timer_end(pnvg_gltimer *t) { (void)t; }
void pnvg_gl_timer_release(pnvg_gltimer *t) { (void)t; }

double pnvg_gl_timer_read(const pnvg_gltimer *t)
{
    (void)t;
    return 0.0;
}

#endif
