/* Render targets and texture wrapping.
 *
 * SPEC phase 2 lists these, but nvgluCreateFramebuffer and
 * nvglCreateImageFromHandleGL3 are already in the vendored headers, so the
 * cost of shipping them now is small. They need a real GL context, which this
 * machine has only in the native smoke test, so they are compiled and
 * reachable but not yet exercised against Psychtoolbox.
 */

#include "pnvg_internal.h"

void h_CreateImageFromTexture(int nlhs, mxArray *plhs[], int nrhs,
                              const mxArray *prhs[])
{
    pnvg_state *s = pnvg_state_get();
    int tex = pnvg_arg_int(prhs[0], 0, "CreateImageFromTexture");
    int w = pnvg_arg_int(prhs[1], 1, "CreateImageFromTexture");
    int h = pnvg_arg_int(prhs[2], 2, "CreateImageFromTexture");
    int flags = (nrhs > 3) ? pnvg_arg_enum(prhs[3], 3, "CreateImageFromTexture") : 0;
    int img;
    (void)nlhs;

    if (s->backend == PNVG_BACKEND_NULL)
        pnvg_err("psychnanovg:NotImplemented",
                 "CreateImageFromTexture needs a GL renderer");
    if (tex <= 0 || w <= 0 || h <= 0)
        pnvg_err("psychnanovg:Range",
                 "CreateImageFromTexture: the texture id, w, and h must be "
                 "positive");
    img = pnvg_gl_image_from_handle(s->vg, (unsigned int)tex, w, h, flags,
                                    s->backend);
    if (img <= 0)
        pnvg_err("psychnanovg:GLError",
                 "CreateImageFromTexture: NanoVG rejected texture %d", tex);
    pnvg_image_mark(img);
    plhs[0] = mxCreateDoubleScalar((double)img);
}

void h_RenderTargetCreate(int nlhs, mxArray *plhs[], int nrhs,
                          const mxArray *prhs[])
{
    int w = pnvg_arg_int(prhs[0], 0, "RenderTargetCreate");
    int h = pnvg_arg_int(prhs[1], 1, "RenderTargetCreate");
    int flags = (nrhs > 2) ? pnvg_arg_enum(prhs[2], 2, "RenderTargetCreate") : 0;
    int rt;

    if (w <= 0 || h <= 0)
        pnvg_err("psychnanovg:Range",
                 "RenderTargetCreate: w and h must be positive");
    rt = pnvg_target_create(w, h, flags);
    if (rt < 0)
        pnvg_err("psychnanovg:GLError", "RenderTargetCreate: %s",
                 pnvg_last_error());
    plhs[0] = mxCreateDoubleScalar((double)rt);
    if (nlhs > 1)
        plhs[1] = mxCreateDoubleScalar(
            (double)pnvg_gl_fb_texture(pnvg_target_ptr(rt)));
}

void h_RenderTargetBind(int nlhs, mxArray *plhs[], int nrhs,
                        const mxArray *prhs[])
{
    int rt = pnvg_arg_int(prhs[0], 0, "RenderTargetBind");
    (void)nlhs; (void)plhs; (void)nrhs;
    if (pnvg_target_bind(rt) != PNVG_OK)
        pnvg_err("psychnanovg:Handle", "RenderTargetBind: %s",
                 pnvg_last_error());
}

void h_RenderTargetUnbind(int nlhs, mxArray *plhs[], int nrhs,
                          const mxArray *prhs[])
{
    (void)nlhs; (void)plhs; (void)nrhs; (void)prhs;
    if (pnvg_target_unbind() != PNVG_OK)
        pnvg_err("psychnanovg:FrameState", "RenderTargetUnbind: %s",
                 pnvg_last_error());
}

void h_RenderTargetImage(int nlhs, mxArray *plhs[], int nrhs,
                         const mxArray *prhs[])
{
    int rt = pnvg_arg_int(prhs[0], 0, "RenderTargetImage");
    int img;
    (void)nlhs; (void)nrhs;
    img = pnvg_target_image(rt);
    if (img < 0)
        pnvg_err("psychnanovg:Handle", "RenderTargetImage: %s",
                 pnvg_last_error());
    plhs[0] = mxCreateDoubleScalar((double)img);
}

void h_RenderTargetDelete(int nlhs, mxArray *plhs[], int nrhs,
                          const mxArray *prhs[])
{
    int rt = pnvg_arg_int(prhs[0], 0, "RenderTargetDelete");
    (void)nlhs; (void)plhs; (void)nrhs;
    if (pnvg_target_delete(rt) != PNVG_OK)
        pnvg_err("psychnanovg:Handle", "RenderTargetDelete: %s",
                 pnvg_last_error());
}
