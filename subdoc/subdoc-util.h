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

#ifndef SUBDOC_STRING_H
#define SUBDOC_STRING_H

#ifdef COUCHBASE_BUILD
#include <platform/platform.h>
#else

#ifdef INCLUDE_SUBDOC_NTOHLL
    #ifndef ntohll
        #if defined(__linux__)
            #ifndef _BSD_SOURCE
            #define _BSD_SOURCE
            #endif
            #include <endian.h>
            #define ntohll be64toh
            #define htonll htobe64
        #elif defined(_WIN32)
            #include <winsock2.h>
        #else
            #error "Missing ntohll implementation!"
        #endif
    #endif
#endif

#endif

#endif /* SUBDOC_STRING_H */
