/* Shared declarations for the MEX layer of psychnanovg. */
#ifndef PNVG_INTERNAL_H
#define PNVG_INTERNAL_H

#include "mex.h"
#include "nanovg.h"
#include "core/pnvg_core.h"
#include "pnvg_marshal.h"
#include "pnvg_profiler.h"

/* The glyph and row buffers sit on the handler stack, so they are bounded.
 * A line of text longer than this is clipped, and the caller is told. */
#define PNVG_MAX_GLYPHS 2048
#define PNVG_MAX_ROWS   256

/* The GL backend is chosen when the static library is compiled, because
 * nanovg_gl.h is a single implementation unit. SPEC 4.1: Psychtoolbox makes
 * legacy GL 2.1 contexts on macOS, so that build is GL2 and every other one
 * is GL3. build.m passes PNVG_GL2 to mex to match CMake. */
#if defined(PNVG_GL2)
#  define PNVG_BACKEND_BUILT PNVG_BACKEND_GL2
#  define PNVG_BACKEND_NAME  "gl2"
#else
#  define PNVG_BACKEND_BUILT PNVG_BACKEND_GL3
#  define PNVG_BACKEND_NAME  "gl3"
#endif

/* Command flags checked by the dispatcher before the handler runs. */
#define PNVG_F_INIT  0x1u   /* needs a live context */
#define PNVG_F_FRAME 0x2u   /* needs BeginFrame */
#define PNVG_F_GL    0x4u   /* issues GL calls, so needs a current context */
#define PNVG_F_MKIMG 0x8u   /* returns a new NanoVG image id in plhs[0] */
#define PNVG_F_DELIMG 0x10u /* deletes the image id in its first argument */
#define PNVG_F_MKFONT 0x20u /* returns a new fontstash font id in plhs[0] */

typedef void (*pnvg_handler)(int nlhs, mxArray *plhs[], int nrhs,
                             const mxArray *prhs[]);

typedef struct {
    const char *name;
    pnvg_handler fn;
    int minArgs;        /* arguments after the subcommand */
    int maxArgs;
    int nOut;
    unsigned int flags;
    const char *group;
    const char *help;
} pnvg_cmd;

typedef struct {
    const char *name;
    int value;
} pnvg_enum;

/* Generated tables, sorted by name so the dispatcher can bisect. */
extern const pnvg_cmd pnvg_cmds[];
extern const int pnvg_ncmds;
extern const pnvg_enum pnvg_enums[];
extern const int pnvg_nenums;
extern const char *const pnvg_version_string;

#define PNVG_VG (pnvg_state_get()->vg)

#endif /* PNVG_INTERNAL_H */
