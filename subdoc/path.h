#ifndef SUBDOC_PATH_H
#define SUBDOC_PATH_H

#include "subdoc-api.h"
#include "jsonsl_header.h"

#ifdef __cplusplus
#ifdef _MSC_VER
// There is no ssize_t in Visual Studio 2013, but size_t is signed
#define ssize_t size_t
#endif
extern "C" {
#endif

// Maximum number of components in a path. Set to 33 to allow 32 'actual'
// components plus the implicit root element.
#define COMPONENTS_ALLOC 33

typedef struct subdoc_PATH_st {
    struct jsonsl_jpr_st jpr_base;
    struct jsonsl_jpr_component_st components_s[COMPONENTS_ALLOC];
    int has_negix; /* True if there is a negative array index in the path */
} subdoc_PATH;

struct subdoc_PATH_st *subdoc_path_alloc(void);
void subdoc_path_free(struct subdoc_PATH_st*);
void subdoc_path_clear(struct subdoc_PATH_st*);
int subdoc_path_parse(struct subdoc_PATH_st *nj, const char *path, size_t len);
jsonsl_error_t subdoc_path_add_arrindex(subdoc_PATH *pth, ssize_t ixnum);
#define subdoc_path_pop_component(pth) do { \
    (pth)->jpr_base.ncomponents--; \
} while (0);

#ifdef __cplusplus
}
#endif
#endif
