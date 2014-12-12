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
#ifdef INCLUDE_JSONSL_SRC
#define JSONSL_API __attribute__((unused)) static
#endif

#include "contrib/jsonsl/jsonsl.h"

/* Don't include the actual source in C++. jsonsl is a bona-fide C file :) */
#ifdef INCLUDE_JSONSL_SRC
#include "contrib/jsonsl/jsonsl.c"
#endif

#endif
