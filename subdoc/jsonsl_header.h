/* header for jsonsl/subdoc interaction. This does the job of stabilizing the
 * fields which we add/modify JSONSL with!.
 *
 * Define INCLUDE_JSONSL_SRC in individual source files (i.e. .c files)
 * to have this header include the C file as well
 */

#ifndef SUBDOC_JSONSL_H
#define SUBDOC_JSONSL_H

#define JSONSL_STATE_USER_FIELDS \
    short mres;
#define JSONSL_JPR_COMPONENT_USER_FIELDS \
    short is_neg;

#ifdef INCLUDE_JSONSL_SRC
#if defined(__GNUC__) || defined(__clang__)
#define JSONSL_API __attribute__((unused)) static
#elif defined(_MSC_VER)
#define JSONSL_API static
#else
#define JSONSL_API static
#endif
#endif

#include "contrib/jsonsl/jsonsl.h"

/* Don't include the actual source in C++. jsonsl is a bona-fide C file :) */
#ifdef INCLUDE_JSONSL_SRC
#include "contrib/jsonsl/jsonsl.c"
#endif

#endif
