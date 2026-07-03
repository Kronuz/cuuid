/*
 * Default (no-op) logging hooks for the standalone `cuuid` library.
 *
 * uuid.cc instruments the condensed UUID codec through the L_* trace family. The
 * standalone library keeps those hooks no-op by default, so it has no dependency
 * on Xapiand's logger or on repr(). Arguments are dropped unevaluated by the
 * variadic no-op macro.
 *
 * A host that wants real logging can provide its own header with:
 *
 *   c++ -DCUUID_TRACE_HEADER='"my_cuuid_trace.h"' ...
 *
 * Each macro is #ifndef-guarded, so defining a subset is fine.
 */

#pragma once

#ifndef L_NOTHING
#define L_NOTHING(...)
#endif

#ifndef L_CALL
#define L_CALL L_NOTHING
#endif
