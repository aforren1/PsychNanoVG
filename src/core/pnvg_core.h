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
/* One context per Psychtoolbox window is the use case, and a lab setup has a
 * handful of windows at most. The bound also caps what a script leaks when it
 * fails before it shuts its contexts down. */
#define PNVG_MAX_CONTEXTS 16
/* Per-subcommand statistic slots. The generated table has fewer entries. */
#define PNVG_MAX_CMDS 256

enum pnvg_backend {
    PNVG_BACKEND_NONE = 0,
    PNVG_BACKEND_GL3,
    PNVG_BACKEND_GL2,
    PNVG_BACKEND_NULL,
    PNVG_BACKEND_GLES2,
    PNVG_BACKEND_GLES3
};

/* The one GL backend in this build. nanovg_gl.h is a single implementation
 * unit, so the choice is made when the library is compiled (SPEC 4.1): GL2
 * on macOS, where Psychtoolbox makes GL 2.1 contexts, GLES2 or GLES3 on a
 * Linux build configured for them, and GL3 everywhere else. */
#if defined(PNVG_GL2)
#  define PNVG_BACKEND_BUILT PNVG_BACKEND_GL2
#  define PNVG_BACKEND_NAME  "gl2"
#elif defined(PNVG_GLES) && PNVG_GLES == 2
#  define PNVG_BACKEND_BUILT PNVG_BACKEND_GLES2
#  define PNVG_BACKEND_NAME  "gles2"
#elif defined(PNVG_GLES) && PNVG_GLES == 3
#  define PNVG_BACKEND_BUILT PNVG_BACKEND_GLES3
#  define PNVG_BACKEND_NAME  "gles3"
#else
#  define PNVG_BACKEND_BUILT PNVG_BACKEND_GL3
#  define PNVG_BACKEND_NAME  "gl3"
#endif

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
    PNVG_E_USAGE,
    PNVG_E_CONTEXT      /* the GL context current now is not the context's own */
};

typedef struct {
    double frames;
    double endFrameNs;      /* last frame */
    double endFrameMaxNs;
    double endFrameSumNs;
    double gpuNs;           /* last readable GPU timer pair; NaN without timer queries */
    double drawCalls;
    double fillCount;
    double strokeCount;
    double textCount;
    double vertexCount;
} pnvg_framestats;

typedef struct {
    double calls;
    double totalNs;
    double maxNs;
} pnvg_cmdstat;

/* Everything that belongs to one NanoVG context. Psychtoolbox gives each
 * onscreen window its own userspace GL context and those contexts share no
 * objects, so the images, fonts, render targets, timer queries, and Stats of
 * one window cannot serve another. */
typedef struct {
    int id;             /* the handle a script sees; never reused */
    int slot;           /* index in the context table */
    /* The GL context that was current at Init, or NULL for the null
     * renderer. Every GL subcommand compares it with the current one. */
    void *glContext;
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
    /* Binds nest: a script can render into a target while another one is
     * already bound, and each has its own saved framebuffer. The stack holds
     * the handles, innermost last, and targetDepth is 0 when none is bound. */
    int targetStack[PNVG_MAX_TARGETS];
    int targetDepth;

    unsigned int imageLive[PNVG_MAX_IMAGES / 32];
    /* Fontstash ids count up from 0 and are never freed, so one high water
     * mark is enough to reject a handle that was never created. */
    int fontCount;

    pnvg_glstate saved;
    pnvg_gltimer timer;
    pnvg_framestats stats;
    pnvg_cmdstat cmdstats[PNVG_MAX_CMDS];

    char glVersion[128];
    char glRenderer[128];
    int stencilBits;

    /* Scratch for the column-major to row-major image transpose. Sized once
     * per image, never on the per-call path for drawing. */
    unsigned char *scratch;
    size_t scratchBytes;
} pnvg_state;

/* The current context. It is never NULL: with no context current it points
 * at an empty state whose vg is NULL, so the per-call checks read one field
 * and need no second test. Only the functions below change it. */
extern pnvg_state *pnvg_cur;

pnvg_state *pnvg_state_get(void);

/* Human-readable text for the last failure, for the caller's error message. */
const char *pnvg_last_error(void);

/* Creates a context and makes it current. Its handle is pnvg_cur->id. */
int pnvg_init(int backend, int createFlags);
/* Shuts the current context down. */
int pnvg_shutdown(void);

/* The live context with this handle, or NULL. */
pnvg_state *pnvg_context_get(int id);
/* Makes a context current. 0 leaves no context current. */
int pnvg_context_set(int id);
int pnvg_context_count(void);
/* Fills ids with the handles of the live contexts, oldest first, and
 * returns how many there are. */
int pnvg_context_list(int *ids, int max);
/* Deletes a context. When its GL context is not the current one, no GL call
 * is made: the memory is freed and the GL objects are left to the driver,
 * which deletes them with their context. *leftToDriver reports that case.
 * When the context was current, no context is current afterwards. */
int pnvg_context_destroy(pnvg_state *s, int *leftToDriver);
/* PNVG_OK when s may issue GL calls now: the null renderer, or its own GL
 * context is current. PNVG_E_NOGLCONTEXT or PNVG_E_CONTEXT otherwise. */
int pnvg_context_check_gl(const pnvg_state *s);

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
/* Validates every row before it emits any, so a bad row leaves the current
 * path as it was. */
int pnvg_path_matrix(const void *data, int n, int isSingle);

/* Path matrix command codes, column 1 of the Nx7 matrix form (SPEC 5.3).
 * The values are part of the MATLAB API, so they never change. */
enum pnvg_pathcode {
    PNVG_PATH_M = 1,            /* x, y */
    PNVG_PATH_L = 2,            /* x, y */
    PNVG_PATH_Q = 3,            /* cx, cy, x, y */
    PNVG_PATH_C = 4,            /* c1x, c1y, c2x, c2y, x, y */
    PNVG_PATH_Z = 5,            /* none */
    PNVG_PATH_ARC = 6,          /* cx, cy, r, a0, a1, dir */
    PNVG_PATH_ARCTO = 7,        /* x1, y1, x2, y2, r */
    PNVG_PATH_ELLIPSE = 8,      /* cx, cy, rx, ry */
    PNVG_PATH_CIRCLE = 9,       /* cx, cy, r */
    PNVG_PATH_RECT = 10,        /* x, y, w, h */
    PNVG_PATH_ROUNDEDRECT = 11, /* x, y, w, h, r */
    PNVG_PATH_WINDING = 12,     /* dir */
    PNVG_PATH_MAXCODE = 12
};

/* One stroke per segment, each with a linear gradient from its first color
 * to its second. seg is Nx4 [x0 y0 x1 y1], col is Nx8 [rgba0 rgba1], both
 * column-major. A run of contiguous segments that all have one color is
 * stroked as one path with a solid color. The current path is replaced and
 * the stroke paint is put back afterwards. */
int pnvg_stroke_segments(const void *seg, int segSingle, const void *col,
                         int colSingle, int n);

/* Reads or writes the stroke paint of the current NanoVG state, with no
 * transform applied. nvgStrokePaint multiplies the paint by the current
 * transform, so it cannot put a saved paint back unchanged. */
void pnvg_nvg_stroke_paint(NVGcontext *ctx, NVGpaint *get, const NVGpaint *set);

/* Render targets. Return the 1-based handle or -1. */
int pnvg_target_create(int w, int h, int imageFlags);
int pnvg_target_bind(int rt);
int pnvg_target_unbind(void);
int pnvg_target_image(int rt);
int pnvg_target_delete(int rt);
void *pnvg_target_ptr(int rt);

double pnvg_now_ns(void);

/* Clears the frame statistics. gpuNs starts as NaN when this context cannot
 * measure GPU time, so a script can tell "not measured" from "0 ns". */
void pnvg_stats_reset(void);

/* Reads NanoVG's private per-frame counters. Defined in the translation unit
 * that compiles nanovg.c, because the counters live in the private struct. */
int pnvg_nvg_counters(NVGcontext *ctx, int *drawCalls, int *fill, int *stroke,
                      int *text);

#endif /* PNVG_CORE_H */
