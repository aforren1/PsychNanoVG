// The one piece of the Tracy integration that has to be C++.
//
// Tracy's C API emits GPU zones but has no call that hands out a GPU context
// id; the C++ integrations take theirs from a shared counter. Taking the id
// from the same counter keeps this context from colliding with any other
// GPU context in the process.

#include "client/TracyProfiler.hpp"

extern "C" unsigned char pnvg_tracy_gpu_context(void)
{
    return tracy::GetGpuCtxCounter().fetch_add(1, std::memory_order_relaxed);
}
