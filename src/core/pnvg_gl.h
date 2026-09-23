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

/* Three slots: the one being written, and two frames of latency before a
 * pair is read, so the readback never waits on the GPU in the common case. */
#define PNVG_TIMER_SLOTS 3

/* The GPU timer ring of one NanoVG context. Query objects are not shared
 * between the userspace GL contexts of two Psychtoolbox windows, so each
 * NanoVG context owns its ring. */
typedef struct {
    unsigned int q[PNVG_TIMER_SLOTS][2];
    int filled[PNVG_TIMER_SLOTS];
    int slot;
    int ready;
    int open;                 /* a begin stamp is waiting for its end */
    double gpuNs;
    /* Tracy query ids are per Tracy GPU context, and every NanoVG context
     * shares one, so each ring takes its own range of ids. */
    int qidBase;
    int tracySlot[PNVG_TIMER_SLOTS];   /* the slot's zone reached Tracy */
} pnvg_gltimer;

/* The GL context that is current on this thread, or NULL. The value is the
 * platform handle (HGLRC, GLXContext, EGLContext, or CGLContextObj) and is
 * only compared, never dereferenced. */
void *pnvg_gl_current_context(void);

/* 1 when a GL context is current on this thread. */
int pnvg_gl_have_context(void);

/* Loads entry points through glad. 0 on success. Safe to call twice. */
int pnvg_gl_load(void);

void pnvg_gl_query_info(char *ver, size_t nver, char *rend, size_t nrend,
                        int *stencilBits);

/* backend is the one PNVG_BACKEND_BUILT_GL names; the library has exactly
 * one GL backend. Returns NULL on failure. */
struct NVGcontext *pnvg_gl_create(int backend, int flags);
void pnvg_gl_destroy(struct NVGcontext *vg, int backend);
/* Frees the memory of a context and issues no GL call. For a context whose
 * GL context is gone or belongs to another window: a GL delete there would
 * hit objects of the same name in the context that is current instead. */
void pnvg_gl_destroy_nogl(struct NVGcontext *vg);

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
/* Frees the render target record only. Its texture goes with the context. */
void pnvg_gl_fb_free_nogl(void *fb);
void pnvg_gl_fb_bind(void *fb);
void pnvg_gl_fb_bind_raw(int fbo);
int pnvg_gl_fb_image(void *fb);
unsigned int pnvg_gl_fb_texture(void *fb);
int pnvg_gl_current_fbo(void);

/* GPU timing. The pair is read two frames later, so the first two frames
 * report 0. The same pair carries the Tracy GPU zone when Tracy is compiled
 * in. timer_reset creates the queries, so it needs the GL context of the
 * ring to be current; so do begin, end, and release. */
void pnvg_gl_timer_begin(pnvg_gltimer *t);
void pnvg_gl_timer_end(pnvg_gltimer *t);
double pnvg_gl_timer_read(const pnvg_gltimer *t);
void pnvg_gl_timer_reset(pnvg_gltimer *t, int qidBase);
void pnvg_gl_timer_release(pnvg_gltimer *t);
/* 0 when the context has neither GL 3.3 nor GL_ARB_timer_query, for example
 * the GL 2.1 context that Psychtoolbox makes on macOS, and always 0 on the
 * GLES backends, which have no GL_TIMESTAMP query in core. */
int pnvg_gl_timer_available(const pnvg_gltimer *t);

#endif /* PNVG_GL_H */
