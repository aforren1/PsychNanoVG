/* The GL implementation unit: glad, the NanoVG GL backend, the proc loader,
 * and the GL state save and restore that SPEC R4 requires.
 *
 * This is the only file that sees a GL header. Everything else in the project
 * goes through pnvg_gl.h, so the MEX and the core layer never need a GL
 * include path.
 */

#include <stdio.h>
#include <string.h>

#include <glad/gl.h>

#include "nanovg.h"

#if defined(PNVG_GL2)
#  define NANOVG_GL2_IMPLEMENTATION
#else
#  define NANOVG_GL3_IMPLEMENTATION
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

int pnvg_gl_have_context(void)
{
    return wglGetCurrentContext() != NULL;
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

int pnvg_gl_have_context(void)
{
    return CGLGetCurrentContext() != NULL;
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

int pnvg_gl_have_context(void)
{
    return glXGetCurrentContext() != NULL;
}

#endif

static int g_loaded;

int pnvg_gl_load(void)
{
    if (g_loaded)
        return 0;
    if (gladLoadGL(pnvg_get_proc) == 0)
        return 1;
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
    if (fbo != 0) {
        glGetFramebufferAttachmentParameteriv(
            GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
            GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE, &bits);
    } else {
        glGetIntegerv(GL_STENCIL_BITS, &bits);
    }
    /* A missing stencil attachment is a legitimate query failure, so clear
     * the error rather than leaving it for Screen('EndOpenGL'). */
    while (glGetError() != GL_NO_ERROR)
        ;
    *stencilBits = (int)bits;
}

struct NVGcontext *pnvg_gl_create(int backend, int flags)
{
#if defined(PNVG_GL2)
    (void)backend;
    return nvgCreateGL2(flags);
#else
    (void)backend;
    return nvgCreateGL3(flags);
#endif
}

void pnvg_gl_destroy(struct NVGcontext *vg, int backend)
{
    (void)backend;
    if (!vg)
        return;
#if defined(PNVG_GL2)
    nvgDeleteGL2(vg);
#else
    nvgDeleteGL3(vg);
#endif
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
    case GL_STACK_OVERFLOW:    return "GL_STACK_OVERFLOW";
    case GL_STACK_UNDERFLOW:   return "GL_STACK_UNDERFLOW";
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
#if defined(PNVG_GL2)
    return nvglCreateImageFromHandleGL2(vg, (GLuint)tex, w, h, flags);
#else
    return nvglCreateImageFromHandleGL3(vg, (GLuint)tex, w, h, flags);
#endif
}

unsigned int pnvg_gl_image_handle(struct NVGcontext *vg, int image, int backend)
{
    (void)backend;
#if defined(PNVG_GL2)
    return (unsigned int)nvglImageHandleGL2(vg, image);
#else
    return (unsigned int)nvglImageHandleGL3(vg, image);
#endif
}

void *pnvg_gl_fb_create(struct NVGcontext *vg, int w, int h, int imageFlags)
{
    return nvgluCreateFramebuffer(vg, w, h, imageFlags);
}

void pnvg_gl_fb_delete(void *fb)
{
    nvgluDeleteFramebuffer((NVGLUframebuffer *)fb);
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
/* GPU timing (SPEC 9.2)                                               */
/* ------------------------------------------------------------------ */

#define PNVG_TIMER_SLOTS 3

static GLuint g_q[PNVG_TIMER_SLOTS][2];
static int g_qfilled[PNVG_TIMER_SLOTS];
static int g_qslot;
static int g_qready;
static double g_gpuNs;

void pnvg_gl_timer_reset(void)
{
    memset(g_qfilled, 0, sizeof(g_qfilled));
    g_qslot = 0;
    g_qready = 0;
    g_gpuNs = 0.0;
}

static void timer_init(void)
{
    if (g_qready)
        return;
    /* glQueryCounter is core in GL 3.3. When it is missing the pointer is
     * null, and Stats reports 0 instead of guessing. */
    if (!glad_glQueryCounter || !glad_glGenQueries)
        return;
    glGenQueries(PNVG_TIMER_SLOTS * 2, &g_q[0][0]);
    g_qready = 1;
}

void pnvg_gl_timer_begin(void)
{
    timer_init();
    if (!g_qready)
        return;
    glQueryCounter(g_q[g_qslot][0], GL_TIMESTAMP);
}

void pnvg_gl_timer_end(void)
{
    int read;
    if (!g_qready)
        return;
    glQueryCounter(g_q[g_qslot][1], GL_TIMESTAMP);
    g_qfilled[g_qslot] = 1;

    /* Read the oldest slot, two frames back, so the GPU is never waited on. */
    read = (g_qslot + 1) % PNVG_TIMER_SLOTS;
    if (g_qfilled[read]) {
        GLint a0 = 0, a1 = 0;
        glGetQueryObjectiv(g_q[read][0], GL_QUERY_RESULT_AVAILABLE, &a0);
        glGetQueryObjectiv(g_q[read][1], GL_QUERY_RESULT_AVAILABLE, &a1);
        if (a0 && a1) {
            GLuint64 t0 = 0, t1 = 0;
            glGetQueryObjectui64v(g_q[read][0], GL_QUERY_RESULT, &t0);
            glGetQueryObjectui64v(g_q[read][1], GL_QUERY_RESULT, &t1);
            g_gpuNs = (double)(t1 - t0);
            g_qfilled[read] = 0;
        }
    }
    g_qslot = (g_qslot + 1) % PNVG_TIMER_SLOTS;
}

double pnvg_gl_timer_read(void)
{
    return g_gpuNs;
}
