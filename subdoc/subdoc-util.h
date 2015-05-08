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
