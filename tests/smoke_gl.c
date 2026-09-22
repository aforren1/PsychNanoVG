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
 */

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
    return 1;
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
    if (g_dc)
        ReleaseDC(g_wnd, g_dc);
    if (g_wnd)
        DestroyWindow(g_wnd);
}

#elif defined(__APPLE__)

static CGLContextObj g_cgl;
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
    return 1;
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

    if (pnvg_init(PNVG_BACKEND_GL3, PNVG_ANTIALIAS | PNVG_STENCIL_STROKES) != PNVG_OK) {
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
