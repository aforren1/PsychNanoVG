/* Profiling hooks.
 *
 * Stats (SPEC 9.2) is always compiled unless PSYCHNANOVG_STATS=0, because a
 * timing question that needs a rebuild is a timing question that does not get
 * asked. Tracy zones compile to nothing unless PSYCHNANOVG_TRACY is on.
 *
 * The CPU zones go through a small stack in the core instead of straight to
 * TracyCZoneN. mexErrMsgIdAndTxt leaves the handler without running the code
 * after it, so a plain TracyCZoneEnd would be skipped on every error and
 * Tracy would see the zones of that thread nest deeper forever. pnvg_err
 * unwinds the stack before it raises.
 */
#ifndef PNVG_PROFILER_H
#define PNVG_PROFILER_H

#ifndef PSYCHNANOVG_STATS
#  define PSYCHNANOVG_STATS 1
#endif

#if defined(PSYCHNANOVG_TRACY) && PSYCHNANOVG_TRACY
#  define PNVG_TRACY 1
#  include "tracy/TracyC.h"

typedef struct ___tracy_source_location_data pnvg_srcloc;

/* The profiler has a manual lifetime (TRACY_MANUAL_LIFETIME), so that it
 * starts and stops with the MEX file rather than with the process. Both
 * calls are idempotent. */
void pnvg_prof_startup(void);
void pnvg_prof_shutdown(void);
int pnvg_prof_started(void);

void pnvg_zone_begin(const pnvg_srcloc *loc);
void pnvg_zone_end(void);
void pnvg_zone_unwind(void);

/* Source locations must outlive the zone, because the Tracy server asks for
 * them by address later, so each one is a static. */
#  define PNVG_ZONE(name)                                                    \
    do {                                                                     \
        static const pnvg_srcloc pnvg_loc_ = {                               \
            name, __func__, __FILE__, (uint32_t)__LINE__, 0};                \
        pnvg_zone_begin(&pnvg_loc_);                                         \
    } while (0)
#  define PNVG_ZONE_LOC(loc) pnvg_zone_begin(loc)
#  define PNVG_ZONE_END() pnvg_zone_end()
#  define PNVG_ZONE_UNWIND() pnvg_zone_unwind()
#else
#  define PNVG_TRACY 0
#  define PNVG_ZONE(name) ((void)0)
#  define PNVG_ZONE_LOC(loc) ((void)0)
#  define PNVG_ZONE_END() ((void)0)
#  define PNVG_ZONE_UNWIND() ((void)0)
#endif

#endif /* PNVG_PROFILER_H */
