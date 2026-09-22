/* The translation unit that compiles NanoVG itself.
 *
 * NanoVG keeps its per-frame counters inside the private NVGcontext struct in
 * nanovg.c and offers no accessor, and upstream has no NANOVG_STATS switch.
 * Including the .c file here puts the counters in scope for one small
 * accessor without patching the vendored source, so `Stats` can report them
 * and the clone in third_party stays pristine.
 */

#include "nanovg.c"

int pnvg_nvg_counters(NVGcontext *ctx, int *drawCalls, int *fill, int *stroke,
                      int *text)
{
    if (!ctx)
        return 0;
    *drawCalls = ctx->drawCallCount;
    *fill = ctx->fillTriCount;
    *stroke = ctx->strokeTriCount;
    *text = ctx->textTriCount;
    return 1;
}
