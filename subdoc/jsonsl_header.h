/*
 *     Copyright 2015-Present Couchbase, Inc.
 *
 *   Use of this software is governed by the Business Source License included
 *   in the file licenses/BSL-Couchbase.txt.  As of the Change Date specified
 *   in that file, in accordance with the Business Source License, use of this
 *   software will be governed by the Apache License, Version 2.0, included in
 *   the file licenses/APL2.txt.
 */

/* header for jsonsl/subdoc interaction. This does the job of stabilizing the
 * fields which we add/modify JSONSL with!.
 *
 * Define INCLUDE_JSONSL_SRC in individual source files (i.e. .c files)
 * to have this header include the C file as well
 */

#pragma once

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
