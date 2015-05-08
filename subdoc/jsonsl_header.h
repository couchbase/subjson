/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
*     Copyright 2015 Couchbase, Inc
*
*   Licensed under the Apache License, Version 2.0 (the "License");
*   you may not use this file except in compliance with the License.
*   You may obtain a copy of the License at
*
*       http://www.apache.org/licenses/LICENSE-2.0
*
*   Unless required by applicable law or agreed to in writing, software
*   distributed under the License is distributed on an "AS IS" BASIS,
*   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*   See the License for the specific language governing permissions and
*   limitations under the License.
*/

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
