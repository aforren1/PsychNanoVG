/* Native GL smoke test for the psychnanovg core layer.
 *
 * tests/gl/ needs Psychtoolbox for a window and a GL context. When PTB is not
 * installed, this executable supplies both itself, so the GL path is still
 * exercised: the same proc loader, the same NanoVG GL3 backend, the same
 * BeginFrame and EndFrame bookkeeping, and a pixel readback that proves
 * something was actually drawn.
 *
 * Build with `build.m smoke`, or with
 *   cmake -DPSYCHNANOVG_SMOKE_GL=ON ..
 *
 * With -DPSYCHNANOVG_GLES=2 or 3 on Linux, the same test runs the GLES
 * backend in an EGL pbuffer context instead of GLX.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The platform GL header, not glad. Everything this file calls itself is in
 * GL 1.1: glViewport, glClear, glReadPixels, glGetError. The modern GL that
 * NanoVG needs is loaded through glad inside the static library, and the two
 * headers cannot both be included in one translation unit. */
#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <GL/gl.h>
#elif defined(PNVG_GLES)
#  include <EGL/egl.h>
#  include <GLES2/gl2.h>
#elif defined(__APPLE__)
#  include <OpenGL/gl.h>
#  include <OpenGL/OpenGL.h>
#  include <OpenGL/CGLRenderers.h>
#else
#  include <GL/glx.h>
#endif

/* A CGL context has no drawable unless it is attached to a window, so the
 * macOS run draws into a NanoVG render target instead of a default
 * framebuffer. The target carries its own stencil renderbuffer, which is what
 * the fills and NVG_STENCIL_STROKES need. */
#if defined(__APPLE__)
#  define SMOKE_OFFSCREEN 1
#else
#  define SMOKE_OFFSCREEN 0
#endif

#include "nanovg.h"
#include "core/pnvg_core.h"

#define SMOKE_W 320
#define SMOKE_H 240
#define SMOKE_FRAMES 60
#define SMOKE_POLYLINE_POINTS 10000

static int g_fail;

static void check(int cond, const char *what)
{
    printf("  %-44s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond)
        g_fail++;
}

static const char *find_font(void)
{
    static const char *candidates[] = {
#if defined(_WIN32)
        "C:\\Windows\\Fonts\\arial.ttf",
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\tahoma.ttf",
#elif defined(__APPLE__)
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/Library/Fonts/Arial.ttf",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
#endif
        NULL
    };
    int i;
    for (i = 0; candidates[i]; i++) {
        FILE *f = fopen(candidates[i], "rb");
        if (f) {
            fclose(f);
            return candidates[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Platform: a hidden window with a compatibility context               */
/* ------------------------------------------------------------------ */

#if defined(_WIN32)

static HWND g_wnd;
static HDC g_dc;
static HGLRC g_rc;
static HGLRC g_rc2;   /* a second GL context, as a second PTB window has */

static int platform_open(int *stencilBits)
{
    PIXELFORMATDESCRIPTOR pfd;
    WNDCLASSA wc;
    int pf;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "psychnanovg_smoke";
    if (!RegisterClassA(&wc))
        return 0;

    /* Off screen rather than merely hidden: a window the compositor never
     * maps can leave the back buffer undefined on some drivers. */
    g_wnd = CreateWindowExA(WS_EX_TOOLWINDOW, "psychnanovg_smoke", "smoke",
                            WS_POPUP, -4000, -4000, SMOKE_W, SMOKE_H,
                            NULL, NULL, wc.hInstance, NULL);
    if (!g_wnd)
        return 0;
    ShowWindow(g_wnd, SW_SHOWNA);

    g_dc = GetDC(g_wnd);
    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = 8;
    pfd.cDepthBits = 24;
    /* SPEC 4.1: Psychtoolbox asks for 8 stencil bits, and NanoVG needs them
     * for concave fills and for NVG_STENCIL_STROKES. */
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    pf = ChoosePixelFormat(g_dc, &pfd);
    if (!pf || !SetPixelFormat(g_dc, pf, &pfd))
        return 0;
    DescribePixelFormat(g_dc, pf, sizeof(pfd), &pfd);
    *stencilBits = pfd.cStencilBits;

    /* A plain wglCreateContext gives a legacy compatibility context, which is
     * exactly what Psychtoolbox creates. */
    g_rc = wglCreateContext(g_dc);
    if (!g_rc || !wglMakeCurrent(g_dc, g_rc))
        return 0;
    g_rc2 = wglCreateContext(g_dc);
    return 1;
}

static int platform_use_alt(int alt)
{
    if (alt && !g_rc2)
        return 0;
    return wglMakeCurrent(g_dc, alt ? g_rc2 : g_rc) ? 1 : 0;
}

static void platform_swap(void)
{
    SwapBuffers(g_dc);
}

static void platform_close(void)
{
    if (g_rc) {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(g_rc);
    }
    if (g_rc2)
        wglDeleteContext(g_rc2);
    if (g_dc)
        ReleaseDC(g_wnd, g_dc);
    if (g_wnd)
        DestroyWindow(g_wnd);
}

#elif defined(PNVG_GLES)

/* Psychtoolbox makes GLES contexts through Waffle on EGL, so the GLES smoke
 * test does the same: a pbuffer, which needs no window and no X server when
 * Mesa picks its surfaceless or device platform. */
static EGLDisplay g_edpy = EGL_NO_DISPLAY;
static EGLSurface g_esurf = EGL_NO_SURFACE;
static EGLContext g_ectx = EGL_NO_CONTEXT;
static EGLContext g_ectx2 = EGL_NO_CONTEXT;

static int platform_open(int *stencilBits)
{
    EGLint major = 0, minor = 0, n = 0, value = 0;
    EGLConfig cfg;
    const EGLint cfgAttr[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE,
#if PNVG_GLES == 3
        EGL_OPENGL_ES3_BIT,
#else
        EGL_OPENGL_ES2_BIT,
#endif
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        /* SPEC 4.1: NanoVG needs 8 stencil bits. */
        EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };
    const EGLint pbAttr[] = {EGL_WIDTH, SMOKE_W, EGL_HEIGHT, SMOKE_H, EGL_NONE};
    const EGLint ctxAttr[] = {EGL_CONTEXT_CLIENT_VERSION, PNVG_GLES, EGL_NONE};

    g_edpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_edpy == EGL_NO_DISPLAY || !eglInitialize(g_edpy, &major, &minor)) {
        printf("  no EGL display\n");
        return 0;
    }
    printf("  EGL %d.%d\n", (int)major, (int)minor);
    if (!eglChooseConfig(g_edpy, cfgAttr, &cfg, 1, &n) || n < 1) {
        printf("  no EGL config with GLES %d and an 8-bit stencil buffer\n",
               PNVG_GLES);
        return 0;
    }
    eglGetConfigAttrib(g_edpy, cfg, EGL_STENCIL_SIZE, &value);
    *stencilBits = (int)value;
    g_esurf = eglCreatePbufferSurface(g_edpy, cfg, pbAttr);
    if (g_esurf == EGL_NO_SURFACE) {
        printf("  eglCreatePbufferSurface failed (0x%04X)\n", eglGetError());
        return 0;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API))
        return 0;
    g_ectx = eglCreateContext(g_edpy, cfg, EGL_NO_CONTEXT, ctxAttr);
    if (g_ectx == EGL_NO_CONTEXT) {
        printf("  eglCreateContext failed (0x%04X)\n", eglGetError());
        return 0;
    }
    g_ectx2 = eglCreateContext(g_edpy, cfg, EGL_NO_CONTEXT, ctxAttr);
    if (!eglMakeCurrent(g_edpy, g_esurf, g_esurf, g_ectx)) {
        printf("  eglMakeCurrent failed (0x%04X)\n", eglGetError());
        return 0;
    }
    return 1;
}

static int platform_use_alt(int alt)
{
    if (alt && g_ectx2 == EGL_NO_CONTEXT)
        return 0;
    return eglMakeCurrent(g_edpy, g_esurf, g_esurf,
                          alt ? g_ectx2 : g_ectx) ? 1 : 0;
}

static void platform_swap(void)
{
    /* A pbuffer has nothing to present. */
    glFlush();
}

static void platform_close(void)
{
    if (g_edpy == EGL_NO_DISPLAY)
        return;
    eglMakeCurrent(g_edpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (g_ectx != EGL_NO_CONTEXT)
        eglDestroyContext(g_edpy, g_ectx);
    if (g_ectx2 != EGL_NO_CONTEXT)
        eglDestroyContext(g_edpy, g_ectx2);
    if (g_esurf != EGL_NO_SURFACE)
        eglDestroySurface(g_edpy, g_esurf);
    eglTerminate(g_edpy);
    g_edpy = EGL_NO_DISPLAY;
}

#elif defined(__APPLE__)

static CGLContextObj g_cgl;
static CGLContextObj g_cgl2;
static CGLPixelFormatObj g_pix;

static CGLError choose_pixel_format(int software, CGLPixelFormatObj *pix,
                                    GLint *npix)
{
    /* No kCGLPFAOpenGLProfile attribute, so this is the legacy profile, which
     * is OpenGL 2.1. SPEC 4.1: that is what Psychtoolbox creates on macOS,
     * and it is why the macOS build uses the NanoVG GL2 backend. */
    CGLPixelFormatAttribute hw[] = {
        kCGLPFAColorSize,   (CGLPixelFormatAttribute)24,
        kCGLPFAAlphaSize,   (CGLPixelFormatAttribute)8,
        kCGLPFADepthSize,   (CGLPixelFormatAttribute)24,
        kCGLPFAStencilSize, (CGLPixelFormatAttribute)8,
        (CGLPixelFormatAttribute)0
    };
    /* The software renderer, in case a runner has no usable GPU. */
    CGLPixelFormatAttribute sw[] = {
        kCGLPFARendererID,
        (CGLPixelFormatAttribute)kCGLRendererGenericFloatID,
        kCGLPFAColorSize,   (CGLPixelFormatAttribute)24,
        kCGLPFAAlphaSize,   (CGLPixelFormatAttribute)8,
        kCGLPFADepthSize,   (CGLPixelFormatAttribute)24,
        kCGLPFAStencilSize, (CGLPixelFormatAttribute)8,
        (CGLPixelFormatAttribute)0
    };
    return CGLChoosePixelFormat(software ? sw : hw, pix, npix);
}

static int platform_open(int *stencilBits)
{
    GLint npix = 0, value = 0;
    CGLError err;

    err = choose_pixel_format(0, &g_pix, &npix);
    if (err != kCGLNoError || g_pix == NULL || npix == 0) {
        printf("  no accelerated pixel format, trying the software renderer\n");
        err = choose_pixel_format(1, &g_pix, &npix);
    }
    if (err != kCGLNoError || g_pix == NULL || npix == 0) {
        printf("  CGLChoosePixelFormat failed (%d)\n", (int)err);
        return 0;
    }

    CGLDescribePixelFormat(g_pix, 0, kCGLPFAStencilSize, &value);
    *stencilBits = (int)value;

    err = CGLCreateContext(g_pix, NULL, &g_cgl);
    if (err != kCGLNoError || g_cgl == NULL) {
        printf("  CGLCreateContext failed (%d)\n", (int)err);
        return 0;
    }
    err = CGLSetCurrentContext(g_cgl);
    if (err != kCGLNoError) {
        printf("  CGLSetCurrentContext failed (%d)\n", (int)err);
        return 0;
    }
    if (CGLCreateContext(g_pix, NULL, &g_cgl2) != kCGLNoError)
        g_cgl2 = NULL;
    return 1;
}

static int platform_use_alt(int alt)
{
    if (alt && !g_cgl2)
        return 0;
    return CGLSetCurrentContext(alt ? g_cgl2 : g_cgl) == kCGLNoError;
}

static void platform_swap(void)
{
    /* No drawable, so there is nothing to present. glFlush keeps the frames
     * from piling up in the command queue. */
    glFlush();
}

static void platform_close(void)
{
    CGLSetCurrentContext(NULL);
    if (g_cgl)
        CGLDestroyContext(g_cgl);
    if (g_cgl2)
        CGLDestroyContext(g_cgl2);
    g_cgl2 = NULL;
    if (g_pix)
        CGLDestroyPixelFormat(g_pix);
    g_cgl = NULL;
    g_pix = NULL;
}

#else /* X11 and GLX */

#include <X11/Xlib.h>

static Display *g_dpy;
static Window g_win;
static GLXContext g_ctx;
static GLXContext g_ctx2;
static Colormap g_cmap;

static int platform_open(int *stencilBits)
{
    /* SPEC 4.1: Psychtoolbox asks for 8 stencil bits, and NanoVG needs them
     * for concave fills and for NVG_STENCIL_STROKES. */
    static int attribs[] = {
        GLX_RGBA,
        GLX_DOUBLEBUFFER,
        GLX_RED_SIZE, 8,
        GLX_GREEN_SIZE, 8,
        GLX_BLUE_SIZE, 8,
        GLX_ALPHA_SIZE, 8,
        GLX_DEPTH_SIZE, 24,
        GLX_STENCIL_SIZE, 8,
        None
    };
    XVisualInfo *vi;
    XSetWindowAttributes swa;
    int screen, value = 0;

    g_dpy = XOpenDisplay(NULL);
    if (!g_dpy) {
        printf("  no X display. Run under xvfb-run.\n");
        return 0;
    }
    screen = DefaultScreen(g_dpy);
    vi = glXChooseVisual(g_dpy, screen, attribs);
    if (!vi) {
        printf("  no visual with an 8-bit stencil buffer\n");
        return 0;
    }
    glXGetConfig(g_dpy, vi, GLX_STENCIL_SIZE, &value);
    *stencilBits = value;

    memset(&swa, 0, sizeof(swa));
    g_cmap = XCreateColormap(g_dpy, RootWindow(g_dpy, screen), vi->visual,
                             AllocNone);
    swa.colormap = g_cmap;
    swa.border_pixel = 0;
    swa.event_mask = StructureNotifyMask;
    /* override_redirect keeps any window manager out of the way. */
    swa.override_redirect = True;
    g_win = XCreateWindow(g_dpy, RootWindow(g_dpy, screen), 0, 0,
                          SMOKE_W, SMOKE_H, 0, vi->depth, InputOutput,
                          vi->visual,
                          CWBorderPixel | CWColormap | CWEventMask |
                          CWOverrideRedirect, &swa);
    if (!g_win) {
        XFree(vi);
        return 0;
    }
    /* The back buffer of a window that was never mapped is undefined, so map
     * it and wait for the server. Under Xvfb there is no screen to show it
     * on, which is what makes this a hidden window. */
    XMapWindow(g_dpy, g_win);
    XSync(g_dpy, False);

    /* A direct legacy context, which is what Psychtoolbox creates as well. */
    g_ctx = glXCreateContext(g_dpy, vi, NULL, True);
    g_ctx2 = glXCreateContext(g_dpy, vi, NULL, True);
    XFree(vi);
    if (!g_ctx) {
        printf("  glXCreateContext failed\n");
        return 0;
    }
    if (!glXMakeCurrent(g_dpy, g_win, g_ctx)) {
        printf("  glXMakeCurrent failed\n");
        return 0;
    }
    return 1;
}

static int platform_use_alt(int alt)
{
    if (alt && !g_ctx2)
        return 0;
    return glXMakeCurrent(g_dpy, g_win, alt ? g_ctx2 : g_ctx) ? 1 : 0;
}

static void platform_swap(void)
{
    if (g_dpy)
        glXSwapBuffers(g_dpy, g_win);
}

static void platform_close(void)
{
    if (!g_dpy)
        return;
    glXMakeCurrent(g_dpy, None, NULL);
    if (g_ctx)
        glXDestroyContext(g_dpy, g_ctx);
    if (g_ctx2)
        glXDestroyContext(g_dpy, g_ctx2);
    if (g_win)
        XDestroyWindow(g_dpy, g_win);
    if (g_cmap)
        XFreeColormap(g_dpy, g_cmap);
    XCloseDisplay(g_dpy);
    g_dpy = NULL;
}

#endif

/* ------------------------------------------------------------------ */

static double median(double *v, int n)
{
    int i, j;
    for (i = 1; i < n; i++) {
        double t = v[i];
        for (j = i - 1; j >= 0 && v[j] > t; j--)
            v[j + 1] = v[j];
        v[j + 1] = t;
    }
    return v[n / 2];
}

/* Reads one pixel in top-left coordinates, the NanoVG convention. */
static void read_px(int x, int y, unsigned char px[4])
{
    glReadPixels(x, SMOKE_H - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
}

static void fill_rect(NVGcontext *vg, float x, float y, NVGcolor c)
{
    nvgBeginPath(vg);
    nvgRect(vg, x, y, 60.0f, 60.0f);
    nvgFillColor(vg, c);
    nvgFill(vg);
}

/* Phase 3: several NanoVG contexts. A second PTB window is a second GL
 * context, which this test has too, but it has one window to read pixels
 * from, so the drawing half runs two NanoVG contexts in one GL context and
 * the other GL context is used for the checks that must refuse it. */
static void smoke_contexts(pnvg_state *st)
{
    pnvg_state *st2, *st3;
    int id1 = st->id, id2, id3, left = -1, rt;
    double frames1 = st->stats.frames;
    unsigned char px[4];

    printf("\n  second context\n");
    if (pnvg_init(PNVG_BACKEND_BUILT, PNVG_ANTIALIAS | PNVG_STENCIL_STROKES) != PNVG_OK) {
        printf("  %s\n", pnvg_last_error());
        check(0, "a second context in the same GL context");
        return;
    }
    st2 = pnvg_state_get();
    id2 = st2->id;
    check(st2 != st && id2 != id1, "the second context has its own handle");
    check(pnvg_context_count() == 2, "two contexts are open");
    check(pnvg_context_check_gl(st2) == PNVG_OK,
          "the second context accepts the GL context it was made in");

    /* Frames of the two contexts overlap: each keeps its own frame state,
     * saved GL state, and command buffer. */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    pnvg_context_set(id1);
    check(pnvg_begin_frame(SMOKE_W, SMOKE_H, 1.0f) == PNVG_OK,
          "BeginFrame in context 1");
    pnvg_context_set(id2);
    check(pnvg_begin_frame(SMOKE_W, SMOKE_H, 1.0f) == PNVG_OK,
          "BeginFrame in context 2 inside frame 1");
    fill_rect(st2->vg, 20.0f, 20.0f, nvgRGBAf(0.0f, 1.0f, 0.0f, 1.0f));
    pnvg_context_set(id1);
    fill_rect(st->vg, 200.0f, 20.0f, nvgRGBAf(1.0f, 0.0f, 0.0f, 1.0f));
    check(pnvg_end_frame() == PNVG_OK, "EndFrame in context 1");
    pnvg_context_set(id2);
    check(pnvg_end_frame() == PNVG_OK, "EndFrame in context 2");
    check(glGetError() == GL_NO_ERROR, "no GL error after the two frames");
    read_px(50, 50, px);
    check(px[1] > 240 && px[0] < 16, "context 2 drew its green square");
    read_px(230, 50, px);
    check(px[0] > 240 && px[1] < 16, "context 1 drew its red square");

    check(st2->stats.frames == 1.0, "Stats of context 2 counts its one frame");
    check(st->stats.frames == frames1 + 1.0,
          "Stats of context 1 counts its own frames only");
    check(pnvg_gl_timer_available(&st2->timer) ==
          pnvg_gl_timer_available(&st->timer),
          "the second context has its own GPU timer ring");

    /* The render target table is per context too. */
    rt = pnvg_target_create(64, 64, 0);
    check(rt > 0, "RenderTargetCreate in context 2");
    if (rt > 0) {
        check(pnvg_target_bind(rt) == PNVG_OK, "RenderTargetBind in context 2");
        check(pnvg_begin_frame(64, 64, 1.0f) == PNVG_OK,
              "BeginFrame on the target of context 2");
        fill_rect(st2->vg, 2.0f, 2.0f, nvgRGBAf(0.0f, 0.0f, 1.0f, 1.0f));
        check(pnvg_end_frame() == PNVG_OK, "EndFrame on the target of context 2");
        check(pnvg_target_unbind() == PNVG_OK, "RenderTargetUnbind in context 2");
    }

    /* Another GL context current: the checks that PsychNanoVG('Context')
     * reports in the MEX. */
    if (platform_use_alt(1)) {
        check(pnvg_context_check_gl(st2) == PNVG_E_CONTEXT,
              "a context refuses another GL context");
        check(pnvg_begin_frame(SMOKE_W, SMOKE_H, 1.0f) == PNVG_E_CONTEXT,
              "BeginFrame in another GL context is refused");
        check(glGetError() == GL_NO_ERROR,
              "the refusal made no GL call in the other context");
        platform_use_alt(0);
    } else {
        printf("  no second GL context on this platform, mismatch checks skipped\n");
    }

    /* Shutdown of the context that is not current, in its own GL context:
     * a full teardown, and the current context stays current. */
    pnvg_context_set(id1);
    check(pnvg_context_destroy(pnvg_context_get(id2), &left) == PNVG_OK &&
          left == 0, "Shutdown of the other context deletes its GL objects");
    check(pnvg_state_get() == st, "context 1 is still current");
    check(pnvg_context_get(id2) == NULL, "the handle of context 2 is stale");
    check(pnvg_context_set(id2) == PNVG_E_HANDLE, "a stale handle is refused");
    check(glGetError() == GL_NO_ERROR, "no GL error after that Shutdown");

    /* Shutdown with another GL context current frees the memory and makes
     * no GL call, because the names would mean other objects there. */
    if (pnvg_init(PNVG_BACKEND_BUILT, PNVG_ANTIALIAS) == PNVG_OK) {
        st3 = pnvg_state_get();
        id3 = st3->id;
        check(id3 > id2, "handles are not reused");
        pnvg_context_set(id1);
        if (platform_use_alt(1)) {
            check(pnvg_context_destroy(st3, &left) == PNVG_OK && left == 1,
                  "Shutdown from another GL context leaves GL objects to the driver");
            check(glGetError() == GL_NO_ERROR,
                  "and makes no GL call in the context that is current");
            platform_use_alt(0);
        } else {
            pnvg_context_destroy(st3, &left);
        }
    } else {
        check(0, "a third context for the teardown check");
    }
    check(pnvg_context_count() == 1, "one context is left");
    pnvg_context_set(id1);
}

int main(void)
{
    pnvg_state *st;
    int stencilBits = 0;
    int font = -1;
    const char *fontPath;
    float *poly;
    unsigned char *before, *after;
    double times[SMOKE_FRAMES];
    double polyTimes[SMOKE_FRAMES];
    int i, changed = 0;
    int drawStencil = 0;
    int offscreen = -1;
    size_t nbytes = (size_t)SMOKE_W * SMOKE_H * 4;

    printf("psychnanovg GL smoke test\n");

    if (!platform_open(&stencilBits)) {
        printf("  no window or GL context on this platform; skipping\n");
        return 77;
    }
    printf("  window stencil bits: %d\n", stencilBits);

    if (pnvg_init(PNVG_BACKEND_BUILT, PNVG_ANTIALIAS | PNVG_STENCIL_STROKES) != PNVG_OK) {
        printf("  Init failed: %s\n", pnvg_last_error());
        platform_close();
        return 1;
    }
    st = pnvg_state_get();
    printf("  GL_VERSION:  %s\n", st->glVersion);
    printf("  GL_RENDERER: %s\n", st->glRenderer);
    printf("  GL_STENCIL_BITS reported by Init: %d\n", st->stencilBits);

#if SMOKE_OFFSCREEN
    /* The context has no default framebuffer, so everything below draws into
     * a render target. Init read the stencil bits of a framebuffer that does
     * not exist, so read them again once the target is bound. */
    offscreen = pnvg_target_create(SMOKE_W, SMOKE_H, 0);
    check(offscreen > 0, "the offscreen render target was created");
    if (offscreen < 1) {
        printf("  %s\n", pnvg_last_error());
        pnvg_shutdown();
        platform_close();
        return 1;
    }
    check(pnvg_target_bind(offscreen) == PNVG_OK,
          "the offscreen render target is bound");
    {
        char v[128], r[128];
        pnvg_gl_query_info(v, sizeof(v), r, sizeof(r), &drawStencil);
    }
    printf("  GL_STENCIL_BITS of the render target: %d\n", drawStencil);
#else
    drawStencil = st->stencilBits;
#endif
    check(drawStencil >= 8, "the drawing target has 8 stencil bits");

    fontPath = find_font();
    if (fontPath) {
        font = nvgCreateFont(st->vg, "sans", fontPath);
        printf("  font: %s (id %d)\n", fontPath, font);
    } else {
        printf("  font: none found, text is skipped\n");
    }
    check(!fontPath || font >= 0, "the system font loaded");

    poly = (float *)malloc(sizeof(float) * 2 * SMOKE_POLYLINE_POINTS);
    before = (unsigned char *)malloc(nbytes);
    after = (unsigned char *)malloc(nbytes);
    if (!poly || !before || !after) {
        printf("  out of memory\n");
        return 1;
    }
    /* Column major, the same layout the MEX gets from MATLAB. */
    for (i = 0; i < SMOKE_POLYLINE_POINTS; i++) {
        float t = (float)i / (float)SMOKE_POLYLINE_POINTS;
        poly[i] = 10.0f + t * (SMOKE_W - 20.0f);
        poly[SMOKE_POLYLINE_POINTS + i] =
            SMOKE_H * 0.75f + 20.0f * (float)((i % 40) - 20) / 20.0f;
    }

    /* A clear frame first, so the readback comparison sees only the shapes. */
    glViewport(0, 0, SMOKE_W, SMOKE_H);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glReadPixels(0, 0, SMOKE_W, SMOKE_H, GL_RGBA, GL_UNSIGNED_BYTE, before);

    for (i = 0; i < SMOKE_FRAMES; i++) {
        double t0;
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        if (pnvg_begin_frame(SMOKE_W, SMOKE_H, 1.0f) != PNVG_OK) {
            printf("  BeginFrame failed: %s\n", pnvg_last_error());
            g_fail++;
            break;
        }

        nvgBeginPath(st->vg);
        nvgCircle(st->vg, SMOKE_W * 0.5f, SMOKE_H * 0.35f, 60.0f);
        nvgFillColor(st->vg, nvgRGBAf(1.0f, 1.0f, 1.0f, 1.0f));
        nvgFill(st->vg);

        nvgBeginPath(st->vg);
        nvgRect(st->vg, 20.5f, 20.5f, 100.0f, 60.0f);
        nvgStrokeWidth(st->vg, 3.0f);
        nvgStrokeColor(st->vg, nvgRGBAf(0.0f, 1.0f, 0.0f, 1.0f));
        nvgStroke(st->vg);

        t0 = pnvg_now_ns();
        nvgBeginPath(st->vg);
        pnvg_polyline(poly, SMOKE_POLYLINE_POINTS, 1, 0);
        polyTimes[i] = pnvg_now_ns() - t0;
        nvgStrokeWidth(st->vg, 1.5f);
        nvgStrokeColor(st->vg, nvgRGBAf(1.0f, 0.4f, 0.1f, 1.0f));
        nvgStroke(st->vg);

        if (font >= 0) {
            nvgFontFaceId(st->vg, font);
            nvgFontSize(st->vg, 24.0f);
            nvgFillColor(st->vg, nvgRGBAf(0.2f, 0.6f, 1.0f, 1.0f));
            nvgText(st->vg, 12.0f, SMOKE_H - 12.0f, "psychnanovg smoke", NULL);
        }

        if (pnvg_end_frame() != PNVG_OK) {
            printf("  EndFrame failed: %s\n", pnvg_last_error());
            g_fail++;
            break;
        }
        times[i] = st->stats.endFrameNs;

        if (i == SMOKE_FRAMES - 1)
            glReadPixels(0, 0, SMOKE_W, SMOKE_H, GL_RGBA, GL_UNSIGNED_BYTE, after);
        platform_swap();
    }

    check(glGetError() == GL_NO_ERROR, "no GL error is pending after the frames");

    /* The circle sits in the middle, so sample a row through its center. */
    {
        int y = SMOKE_H - 1 - (int)(SMOKE_H * 0.35f);   /* readback is bottom up */
        int x;
        int white = 0;
        for (x = 0; x < SMOKE_W; x++) {
            size_t o = ((size_t)y * SMOKE_W + x) * 4;
            if (before[o] != after[o])
                changed++;
            if (after[o] > 200 && after[o + 1] > 200 && after[o + 2] > 200)
                white++;
        }
        check(changed > 100, "the readback row through the circle changed");
        check(white > 100, "the circle covers over 100 pixels on that row");
    }

    {
        double t = median(times, SMOKE_FRAMES);
        double p = median(polyTimes, SMOKE_FRAMES);
        printf("\n  EndFrame median over %d frames: %.1f us (max %.1f us)\n",
               SMOKE_FRAMES, t / 1000.0, st->stats.endFrameMaxNs / 1000.0);
        printf("  Polyline of %d points, path build only: %.1f us (%.0f ns/point)\n",
               SMOKE_POLYLINE_POINTS, p / 1000.0, p / SMOKE_POLYLINE_POINTS);
        printf("  NanoVG counters: drawCalls %.0f, fillTri %.0f, strokeTri %.0f,"
               " textTri %.0f\n", st->stats.drawCalls, st->stats.fillCount,
               st->stats.strokeCount, st->stats.textCount);
        printf("  gpuNs from the timer query pair: %.0f\n", st->stats.gpuNs);
    }

    /* Arcs in the Path matrix and gradient segments, the other phase 2
     * paths. The matrices are column major, as the MEX gets them. */
    {
        /* 6 rows x 7 columns: a gauge from two arcs, then a ringed circle. */
        static const double gauge[6 * 7] = {
            /* code */ 6, 6, 5, 9, 9, 12,
            /* 1 */ 100, 100, 0, 250, 250, 2,
            /* 2 */ 110, 110, 0, 50, 50, 0,
            /* 3 */ 60, 40, 0, 30, 15, 0,
            /* 4 */ -3.14159265358979, 0, 0, 0, 0, 0,
            /* 5 */ 0, -3.14159265358979, 0, 0, 0, 0,
            /* 6 */ 2, 1, 0, 0, 0, 0};
        static const double seg[1 * 4] = {170, 200, 310, 200};
        static const double col[1 * 8] = {1, 0, 0, 1, 0, 0, 1, 1};
        unsigned char *px = after;
#define SMOKE_PX(x, y) (px + (((size_t)(SMOKE_H - 1 - (y)) * SMOKE_W + (x)) * 4))

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        check(pnvg_begin_frame(SMOKE_W, SMOKE_H, 1.0f) == PNVG_OK,
              "BeginFrame for the arc frame");
        nvgBeginPath(st->vg);
        check(pnvg_path_matrix(gauge, 6, 0) == PNVG_OK,
              "the Path matrix with arcs was accepted");
        nvgFillColor(st->vg, nvgRGBAf(1.0f, 1.0f, 1.0f, 1.0f));
        nvgFill(st->vg);
        nvgStrokeWidth(st->vg, 8.0f);
        check(pnvg_stroke_segments(seg, 0, col, 0, 1) == PNVG_OK,
              "StrokeSegments was accepted");
        check(pnvg_end_frame() == PNVG_OK, "EndFrame for the arc frame");
        glReadPixels(0, 0, SMOKE_W, SMOKE_H, GL_RGBA, GL_UNSIGNED_BYTE, px);

        check(SMOKE_PX(100, 60)[0] > 240, "the gauge arc is white at the top");
        check(SMOKE_PX(100, 90)[0] < 16, "the inner arc leaves a hole");
        check(SMOKE_PX(100, 160)[0] < 16, "no gauge below its center");
        check(SMOKE_PX(250 + 22, 50)[0] > 240, "the circle row fills");
        check(SMOKE_PX(250, 50)[0] < 16, "the winding row makes a hole");
        check(SMOKE_PX(174, 200)[0] > 220 && SMOKE_PX(174, 200)[2] < 35,
              "the gradient starts red");
        check(SMOKE_PX(306, 200)[2] > 220 && SMOKE_PX(306, 200)[0] < 35,
              "the gradient ends blue");
#undef SMOKE_PX
    }

    /* The per-segment cost of StrokeSegments for 1000 segments: the path
     * build in the call, and the tessellation and draw in EndFrame. */
    {
        enum { NSEG = 1000, NREP = 20 };
        double *sg = (double *)malloc(sizeof(double) * NSEG * 4);
        double *cl = (double *)malloc(sizeof(double) * NSEG * 8);
        double build[NREP], end[NREP], gpu = 0.0;
        int k, c;
        if (!sg || !cl) {
            printf("  out of memory\n");
            return 1;
        }
        for (k = 0; k < NSEG; k++) {
            double a0 = 0.02 * k, a1 = 0.02 * (k + 1);
            double r0 = 10.0 + 0.1 * k, r1 = 10.0 + 0.1 * (k + 1);
            sg[k] = SMOKE_W / 2 + r0 * cos(a0);
            sg[NSEG + k] = SMOKE_H / 2 + r0 * sin(a0);
            sg[2 * NSEG + k] = SMOKE_W / 2 + r1 * cos(a1);
            sg[3 * NSEG + k] = SMOKE_H / 2 + r1 * sin(a1);
            for (c = 0; c < 4; c++) {
                cl[c * NSEG + k] = (c == 3) ? 1.0 : (double)((k + c) % 3) / 2.0;
                cl[(c + 4) * NSEG + k] = (c == 3) ? 1.0 : (double)((k + c + 1) % 3) / 2.0;
            }
        }
        for (k = 0; k < NREP; k++) {
            double t0;
            glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
            pnvg_begin_frame(SMOKE_W, SMOKE_H, 1.0f);
            nvgStrokeWidth(st->vg, 2.0f);
            nvgLineCap(st->vg, NVG_ROUND);
            t0 = pnvg_now_ns();
            pnvg_stroke_segments(sg, 0, cl, 0, NSEG);
            build[k] = pnvg_now_ns() - t0;
            pnvg_end_frame();
            end[k] = st->stats.endFrameNs;
            gpu = st->stats.gpuNs;
            platform_swap();
        }
        check(glGetError() == GL_NO_ERROR, "no GL error after 1000 segments");
        {
            double b = median(build, NREP), e = median(end, NREP);
            printf("\n  StrokeSegments, %d gradient segments, median of %d frames:\n",
                   NSEG, NREP);
            printf("    call (path build): %.1f us, %.0f ns per segment\n",
                   b / 1000.0, b / NSEG);
            printf("    EndFrame:          %.1f us, %.0f ns per segment\n",
                   e / 1000.0, e / NSEG);
            printf("    GPU (last pair):   %.1f us, %.0f ns per segment\n",
                   gpu / 1000.0, gpu / NSEG);
        }
        free(sg);
        free(cl);
    }

    /* Render target round trip, the phase 2 path, while a context is current. */
    {
        int rt = pnvg_target_create(64, 64, 0);
        check(rt > 0, "RenderTargetCreate succeeded");
        if (rt > 0) {
            check(pnvg_target_bind(rt) == PNVG_OK, "RenderTargetBind succeeded");
            check(pnvg_begin_frame(64, 64, 1.0f) == PNVG_OK,
                  "BeginFrame on the render target succeeded");
            nvgBeginPath(st->vg);
            nvgCircle(st->vg, 32.0f, 32.0f, 20.0f);
            nvgFillColor(st->vg, nvgRGBAf(1.0f, 0.0f, 0.0f, 1.0f));
            nvgFill(st->vg);
            check(pnvg_end_frame() == PNVG_OK, "EndFrame on the render target");
            check(pnvg_target_unbind() == PNVG_OK, "RenderTargetUnbind");
            check(pnvg_target_image(rt) > 0, "the render target has an image id");
            check(pnvg_target_delete(rt) == PNVG_OK, "RenderTargetDelete");
        }
    }

    smoke_contexts(st);

#if SMOKE_OFFSCREEN
    check(pnvg_target_unbind() == PNVG_OK, "the offscreen target is released");
    check(pnvg_target_delete(offscreen) == PNVG_OK,
          "the offscreen target is deleted");
#endif
    check(pnvg_shutdown() == PNVG_OK, "Shutdown succeeded");
    check(glGetError() == GL_NO_ERROR, "no GL error is pending after Shutdown");

    free(poly);
    free(before);
    free(after);
    platform_close();

    printf("\n%s\n", g_fail ? "SMOKE TEST FAILED" : "smoke test passed");
    return g_fail ? 1 : 0;
}
