#ifndef SUBDOC_STRING_H
#define SUBDOC_STRING_H

#if defined(LIBCOUCHBASE_INTERNAL)
#include "simplestring.h"
typedef lcb_string subdoc_STRING;
#define subdoc_string_init lcb_string_init
#define subdoc_string_clear lcb_string_clear
#define subdoc_string_release lcb_string_release
#define subdoc_string_appendz lcb_string_appendz
#define subdoc_string_append lcb_string_append
#else
typedef struct {
    char *base;
    size_t nalloc;
    size_t nused;
} subdoc_STRING;

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

#ifdef INCLUDE_SUBDOC_STRING_SRC
static int subdoc_string_init(subdoc_STRING *s) {
    s->base = NULL; s->nalloc = 0; s->nused = 0; return 0;
}
static void subdoc_string_release(subdoc_STRING *s) {
    if (s->base) { free(s->base); }
    s->base = NULL; s->nalloc = 0; s->nused = 0;
}
static void subdoc_string_clear(subdoc_STRING *s) {
    s->nused = 0;
}

static int subdoc_string__reserve(subdoc_STRING *str, size_t size) {
    size_t newalloc;
    char *newbuf;

    /** Set size to one greater, for the terminating NUL */
    size++;
    if (!size) {
        return -1;
    }

    if (str->nalloc - str->nused >= size) {
        return 0;
    }

    newalloc = str->nalloc;
    if (!newalloc) {
        newalloc = 1;
    }

    while (newalloc - str->nused < size) {
        if (newalloc * 2 < newalloc) {
            return -1;
        }

        newalloc *= 2;
    }

    newbuf = (char *)realloc(str->base, newalloc);
    if (newbuf == NULL) {
        return -1;
    }

    str->base = newbuf;
    str->nalloc = newalloc;
    return 0;
}
static int subdoc_string_append(subdoc_STRING *str, const void *data, size_t size) {
    if (subdoc_string__reserve(str, size)) {
        return -1;
    }
    memcpy(str->base + str->nused, data, size);
    str->nused += size;
    return 0;
}
#endif /* INCLUDE_SUBDOC_STRING_SRC */
#endif /* defined(LIBCOUCHBASE_INTERNAL) */
#endif /* SUBDOC_STRING_H */
