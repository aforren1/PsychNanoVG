/* GL-facing operations for psychnanovg.
 *
 * Only pnvg_gl.c includes the GL headers. Everything above this line works
 * with plain integers, so the core layer, the MEX, and the native smoke test
 * can all be compiled without a GL header search path.
 */
#ifndef PNVG_GL_H
#define PNVG_GL_H

#include <stddef.h>

struct NVGcontext;

/* The GL state that nanovg_gl.h changes and does not put back (SPEC R4). */
typedef struct {
    int valid;
    int viewport[4];
    int blendEnabled;
    int srcRGB, dstRGB, srcAlpha, dstAlpha;
    int activeTexture;
} pnvg_glstate;

/* 1 when a GL context is current on this thread. */
int pnvg_gl_have_context(void);

/* Loads entry points through glad. 0 on success. Safe to call twice. */
int pnvg_gl_load(void);

void pnvg_gl_query_info(char *ver, size_t nver, char *rend, size_t nrend,
                        int *stencilBits);

/* backend is PNVG_BACKEND_GL3 or PNVG_BACKEND_GL2. Returns NULL on failure. */
struct NVGcontext *pnvg_gl_create(int backend, int flags);
void pnvg_gl_destroy(struct NVGcontext *vg, int backend);

void pnvg_gl_save(pnvg_glstate *s);
void pnvg_gl_restore(const pnvg_glstate *s);
void pnvg_gl_viewport(int x, int y, int w, int h);

/* Returns the first pending GL error and clears the queue, or 0. */
unsigned int pnvg_gl_drain_error(void);
const char *pnvg_gl_error_name(unsigned int e);

int pnvg_gl_image_from_handle(struct NVGcontext *vg, unsigned int tex,
                              int w, int h, int flags, int backend);
unsigned int pnvg_gl_image_handle(struct NVGcontext *vg, int image,
                                  int backend);

/* Render targets. The handle is an opaque NVGLUframebuffer*. */
void *pnvg_gl_fb_create(struct NVGcontext *vg, int w, int h, int imageFlags);
void pnvg_gl_fb_delete(void *fb);
void pnvg_gl_fb_bind(void *fb);
void pnvg_gl_fb_bind_raw(int fbo);
int pnvg_gl_fb_image(void *fb);
unsigned int pnvg_gl_fb_texture(void *fb);
int pnvg_gl_current_fbo(void);

/* GPU timing. The pair is read two frames later, so the first two frames
 * report 0. The same pair carries the Tracy GPU zone when Tracy is compiled
 * in. timer_reset creates the queries, so it needs a current context. */
void pnvg_gl_timer_begin(void);
void pnvg_gl_timer_end(void);
double pnvg_gl_timer_read(void);
void pnvg_gl_timer_reset(void);
void pnvg_gl_timer_release(void);
/* 0 when the context has neither GL 3.3 nor GL_ARB_timer_query, for example
 * the GL 2.1 context that Psychtoolbox makes on macOS. */
int pnvg_gl_timer_available(void);

#endif /* PNVG_GL_H */
