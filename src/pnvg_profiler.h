/* Profiling hooks.
 *
 * Stats (SPEC 9.2) is always compiled unless PSYCHNANOVG_STATS=0, because a
 * timing question that needs a rebuild is a timing question that does not get
 * asked. Tracy zones compile to nothing unless PSYCHNANOVG_TRACY is on.
 */
#ifndef PNVG_PROFILER_H
#define PNVG_PROFILER_H

#ifndef PSYCHNANOVG_STATS
#  define PSYCHNANOVG_STATS 1
#endif

#if defined(PSYCHNANOVG_TRACY) && PSYCHNANOVG_TRACY
#  include "tracy/TracyC.h"
#  define PNVG_ZONE(name) TracyCZoneN(pnvg_zone_, name, 1)
#  define PNVG_ZONE_END() TracyCZoneEnd(pnvg_zone_)
#else
#  define PNVG_ZONE(name) ((void)0)
#  define PNVG_ZONE_END() ((void)0)
#endif

#endif /* PNVG_PROFILER_H */
