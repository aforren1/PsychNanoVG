/* Null render backend (SPEC 11.1).
 *
 * NanoVG splits the vector work from the drawing work: path flattening,
 * tessellation, text shaping, the state stack, and every handle live in
 * nanovg.c, and the backend only turns the result into GL calls. A backend
 * whose callbacks do nothing therefore exercises all of the marshaling and
 * all of the argument checking with no GPU and no window, which is what
 * run_tests needs on a machine without Psychtoolbox.
 */

#include <stdlib.h>
#include <string.h>

#include "nanovg.h"
#include "pnvg_core.h"

#define PNVG_NULL_MAX_TEX 256

typedef struct {
    int w, h, type;
    int used;
} pnvg_null_tex;

typedef struct {
    pnvg_null_tex tex[PNVG_NULL_MAX_TEX];
    int next;
} pnvg_null_ctx;

static int null_renderCreate(void *uptr)
{
    (void)uptr;
    return 1;
}

static int null_renderCreateTexture(void *uptr, int type, int w, int h,
                                    int imageFlags, const unsigned char *data)
{
    pnvg_null_ctx *c = (pnvg_null_ctx *)uptr;
    int i;
    (void)imageFlags;
    (void)data;
    for (i = 1; i < PNVG_NULL_MAX_TEX; i++) {
        if (!c->tex[i].used) {
            c->tex[i].used = 1;
            c->tex[i].w = w;
            c->tex[i].h = h;
            c->tex[i].type = type;
            return i;   /* 0 means failure to NanoVG, so ids start at 1 */
        }
    }
    return 0;
}

static int null_renderDeleteTexture(void *uptr, int image)
{
    pnvg_null_ctx *c = (pnvg_null_ctx *)uptr;
    if (image < 1 || image >= PNVG_NULL_MAX_TEX || !c->tex[image].used)
        return 0;
    c->tex[image].used = 0;
    return 1;
}

static int null_renderUpdateTexture(void *uptr, int image, int x, int y,
                                    int w, int h, const unsigned char *data)
{
    pnvg_null_ctx *c = (pnvg_null_ctx *)uptr;
    (void)x; (void)y; (void)w; (void)h; (void)data;
    if (image < 1 || image >= PNVG_NULL_MAX_TEX || !c->tex[image].used)
        return 0;
    return 1;
}

static int null_renderGetTextureSize(void *uptr, int image, int *w, int *h)
{
    pnvg_null_ctx *c = (pnvg_null_ctx *)uptr;
    if (image < 1 || image >= PNVG_NULL_MAX_TEX || !c->tex[image].used)
        return 0;
    *w = c->tex[image].w;
    *h = c->tex[image].h;
    return 1;
}

static void null_renderViewport(void *uptr, float w, float h, float ratio)
{
    (void)uptr; (void)w; (void)h; (void)ratio;
}

static void null_renderCancel(void *uptr) { (void)uptr; }
static void null_renderFlush(void *uptr) { (void)uptr; }

static void null_renderFill(void *uptr, NVGpaint *paint,
                            NVGcompositeOperationState op, NVGscissor *scissor,
                            float fringe, const float *bounds,
                            const NVGpath *paths, int npaths)
{
    (void)uptr; (void)paint; (void)op; (void)scissor; (void)fringe;
    (void)bounds; (void)paths; (void)npaths;
}

static void null_renderStroke(void *uptr, NVGpaint *paint,
                              NVGcompositeOperationState op,
                              NVGscissor *scissor, float fringe,
                              float strokeWidth, const NVGpath *paths,
                              int npaths)
{
    (void)uptr; (void)paint; (void)op; (void)scissor; (void)fringe;
    (void)strokeWidth; (void)paths; (void)npaths;
}

static void null_renderTriangles(void *uptr, NVGpaint *paint,
                                 NVGcompositeOperationState op,
                                 NVGscissor *scissor, const NVGvertex *verts,
                                 int nverts, float fringe)
{
    (void)uptr; (void)paint; (void)op; (void)scissor; (void)verts;
    (void)nverts; (void)fringe;
}

static void null_renderDelete(void *uptr)
{
    free(uptr);
}

NVGcontext *pnvg_null_create(int flags)
{
    NVGparams params;
    pnvg_null_ctx *c = (pnvg_null_ctx *)calloc(1, sizeof(pnvg_null_ctx));
    if (!c)
        return NULL;

    memset(&params, 0, sizeof(params));
    params.userPtr = c;
    params.edgeAntiAlias = (flags & PNVG_ANTIALIAS) ? 1 : 0;
    params.renderCreate = null_renderCreate;
    params.renderCreateTexture = null_renderCreateTexture;
    params.renderDeleteTexture = null_renderDeleteTexture;
    params.renderUpdateTexture = null_renderUpdateTexture;
    params.renderGetTextureSize = null_renderGetTextureSize;
    params.renderViewport = null_renderViewport;
    params.renderCancel = null_renderCancel;
    params.renderFlush = null_renderFlush;
    params.renderFill = null_renderFill;
    params.renderStroke = null_renderStroke;
    params.renderTriangles = null_renderTriangles;
    params.renderDelete = null_renderDelete;

    return nvgCreateInternal(&params);
}

void pnvg_null_delete(NVGcontext *vg)
{
    /* nvgDeleteInternal calls renderDelete, which frees the user pointer. */
    nvgDeleteInternal(vg);
}
