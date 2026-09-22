/* psychnanovg core: everything that touches NanoVG, with no MATLAB types.
 *
 * The MEX marshaling layer sits on top of this. A native test, such as
 * tests/smoke_gl.c, links the same core and exercises the real GL path
 * without MATLAB.
 */
#ifndef PNVG_CORE_H
#define PNVG_CORE_H

#include <stddef.h>

#include "nanovg.h"
#include "pnvg_gl.h"

#define PNVG_MAX_PAINTS  256
#define PNVG_MAX_TARGETS 16
/* NanoVG image ids count up from 1 and are never reused above this, so the
 * bitset is a cheap way to reject a stale handle before NanoVG asserts. */
#define PNVG_MAX_IMAGES  4096

enum pnvg_backend {
    PNVG_BACKEND_NONE = 0,
    PNVG_BACKEND_GL3,
    PNVG_BACKEND_GL2,
    PNVG_BACKEND_NULL
};

/* Mirrors enum NVGcreateFlags in nanovg_gl.h. That header cannot be included
 * without a GL header in scope, and these three values are part of NanoVG's
 * public contract. */
#define PNVG_ANTIALIAS       (1 << 0)
#define PNVG_STENCIL_STROKES (1 << 1)
#define PNVG_DEBUG           (1 << 2)

enum pnvg_status {
    PNVG_OK = 0,
    PNVG_E_NOTINIT,
    PNVG_E_ALREADYINIT,
    PNVG_E_NOGLCONTEXT,
    PNVG_E_GLINIT,
    PNVG_E_GLERROR,
    PNVG_E_FRAMESTATE,
    PNVG_E_HANDLE,
    PNVG_E_RANGE,
    PNVG_E_USAGE
};

typedef struct {
    double frames;
    double endFrameNs;      /* last frame */
    double endFrameMaxNs;
    double endFrameSumNs;
    double gpuNs;           /* last readable GPU timer pair */
    double drawCalls;
    double fillCount;
    double strokeCount;
    double textCount;
    double vertexCount;
} pnvg_framestats;

typedef struct {
    NVGcontext *vg;
    int backend;
    int inFrame;
    int createFlags;
    int frameW, frameH;
    float pixelRatio;

    NVGpaint paints[PNVG_MAX_PAINTS];
    short paintNext[PNVG_MAX_PAINTS];   /* free list links */
    short paintFreeHead;
    unsigned char paintLive[PNVG_MAX_PAINTS];

    void *targets[PNVG_MAX_TARGETS];
    int targetPrevFbo[PNVG_MAX_TARGETS];
    int boundTarget;                    /* -1 when none */

    unsigned int imageLive[PNVG_MAX_IMAGES / 32];
    /* Fontstash ids count up from 0 and are never freed, so one high water
     * mark is enough to reject a handle that was never created. */
    int fontCount;

    pnvg_glstate saved;
    pnvg_framestats stats;

    char glVersion[128];
    char glRenderer[128];
    int stencilBits;

    /* Scratch for the column-major to row-major image transpose. Sized once
     * per image, never on the per-call path for drawing. */
    unsigned char *scratch;
    size_t scratchBytes;

    char err[256];
} pnvg_state;

pnvg_state *pnvg_state_get(void);

/* Human-readable text for the last failure, for the caller's error message. */
const char *pnvg_last_error(void);

int pnvg_init(int backend, int createFlags);
int pnvg_shutdown(void);
int pnvg_begin_frame(int w, int h, float pixelRatio);
int pnvg_end_frame(void);
int pnvg_cancel_frame(void);

/* Paint table. pnvg_paint_alloc returns a 1-based index or -1 when full. */
int pnvg_paint_alloc(const NVGpaint *p);
int pnvg_paint_fetch(int idx, NVGpaint *out);
int pnvg_paint_release(int idx);

void pnvg_font_mark(int id);
int pnvg_font_is_live(int id);

void pnvg_image_mark(int id);
void pnvg_image_unmark(int id);
int pnvg_image_is_live(int id);

/* Image transpose. src is column-major HxWx4 uint8 as MATLAB stores it.
 * Returns a row-major RGBA buffer owned by the state, or NULL on failure. */
const unsigned char *pnvg_image_transpose(const unsigned char *src,
                                          int h, int w);

/* Batched path building. `data` is column-major, as MATLAB stores it:
 * element (i, c) is at data[c * n + i]. isSingle picks float over double. */
int pnvg_polyline(const void *data, int n, int isSingle, int close);
int pnvg_circles(const void *data, int n, int isSingle);
int pnvg_rects(const void *data, int n, int isSingle);
int pnvg_path_matrix(const void *data, int n, int isSingle);

/* Path matrix command codes, column 1 of the Nx7 matrix form. */
enum pnvg_pathcode {
    PNVG_PATH_M = 1,
    PNVG_PATH_L = 2,
    PNVG_PATH_Q = 3,
    PNVG_PATH_C = 4,
    PNVG_PATH_Z = 5
};

/* Render targets. Return the 1-based handle or -1. */
int pnvg_target_create(int w, int h, int imageFlags);
int pnvg_target_bind(int rt);
int pnvg_target_unbind(void);
int pnvg_target_image(int rt);
int pnvg_target_delete(int rt);
void *pnvg_target_ptr(int rt);

double pnvg_now_ns(void);

/* Reads NanoVG's private per-frame counters. Defined in the translation unit
 * that compiles nanovg.c, because the counters live in the private struct. */
int pnvg_nvg_counters(NVGcontext *ctx, int *drawCalls, int *fill, int *stroke,
                      int *text);

#endif /* PNVG_CORE_H */
