#ifndef SUBDOC_PATH_H
#define SUBDOC_PATH_H

#include "subdoc-api.h"
#include "jsonsl_header.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COMPONENTS_ALLOC 32
typedef struct subdoc_PATH_st {
    struct jsonsl_jpr_st jpr_base;
    struct jsonsl_jpr_component_st components_s[COMPONENTS_ALLOC];
} subdoc_PATH;

struct subdoc_PATH_st *subdoc_path_alloc(void);
void subdoc_path_free(struct subdoc_PATH_st*);
void subdoc_path_clear(struct subdoc_PATH_st*);
int subdoc_path_parse(struct subdoc_PATH_st *nj, const char *path, size_t len);
int subdoc_path_add_arrindex(subdoc_PATH *pth, size_t ixnum);
#define subdoc_path_pop_component(pth) do { \
    (pth)->jpr_base.ncomponents--; \
} while (0);

#ifdef __cplusplus
}
#endif
#endif
