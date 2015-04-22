#define INCLUDE_JSONSL_SRC
#include "subdoc-api.h"
#include "path.h"

static char *
convert_escaped(const char *src, size_t *len)
{
    unsigned ii, oix;

    char *ret = (char *)malloc(*len);
    if (!ret) {
        return NULL;
    }

    for (oix = 0, ii = 0; ii < *len; ii++) {
        if (src[ii] != '`') {
            ret[oix++] = src[ii];
        } else if(src[ii] == '`' && ii+1 < *len && src[ii+1] == '`') {
            ret[oix++] = src[ii++];
        }
    }
    *len = oix;
    return ret;
}

/* Adds a numeric component */
static int
add_num_component(subdoc_PATH *nj, const char *component, size_t len)
{
    unsigned ii;
    size_t numval = 0;

    if (component[0] == '-') {
        if (len != 2 || component[1] != '1') {
            return JSONSL_ERROR_INVALID_NUMBER;
        } else {
            return subdoc_path_add_arrindex(nj, -1);
        }
    }

    for (ii = 0; ii < len; ii++) {
        const char *c = &component[ii];
        if (*c < 0x30 && *c > 0x39) {
            return JSONSL_ERROR_INVALID_NUMBER;
        } else {
            size_t tmpval = numval;
            tmpval *= 10;
            tmpval += *c - 0x30;

            /* check for overflow */
            if (tmpval < numval) {
                return JSONSL_ERROR_INVALID_NUMBER;
            } else {
                numval = tmpval;
            }
        }
    }
    return subdoc_path_add_arrindex(nj, numval);
}

static int
add_str_component(subdoc_PATH *nj, const char *component, size_t len, int n_backtick)
{
    struct jsonsl_jpr_component_st *jpr_comp;
    jsonsl_jpr_t jpr = &nj->jpr_base;

    /* Allocate first component: */
    if (len > 1 && component[0] == '`' && component[len-1] == '`') {
        component++;
        n_backtick -= 2;
        len -= 2;
    }

    if (jpr->ncomponents == COMPONENTS_ALLOC) {
        return JSONSL_ERROR_LEVELS_EXCEEDED;
    }
    if (len == 0) {
        return JSONSL_ERROR_JPR_BADPATH;
    }

    if (n_backtick) {
        /* OHNOEZ! Slow path */
        component = convert_escaped(component, &len);
    }

    jpr_comp = &nj->components_s[jpr->ncomponents];
    jpr_comp->pstr = (char *)component;
    jpr_comp->ptype = JSONSL_PATH_STRING;
    jpr_comp->len = len;
    jpr_comp->is_arridx = 0;
    jpr_comp->is_neg = 0;
    jpr->ncomponents++;
    return 0;
}

jsonsl_error_t
subdoc_path_add_arrindex(subdoc_PATH *pth, ssize_t ixnum)
{
    jsonsl_jpr_t jpr = &pth->jpr_base;
    struct jsonsl_jpr_component_st *comp;

    if (jpr->ncomponents == COMPONENTS_ALLOC) {
        return JSONSL_ERROR_LEVELS_EXCEEDED;
    }

    comp = &jpr->components[jpr->ncomponents];
    comp->ptype = JSONSL_PATH_NUMERIC;
    comp->len = 0;
    comp->is_arridx = 1;
    comp->idx = ixnum;
    comp->pstr = NULL;
    jpr->ncomponents++;
    if (ixnum == -1) {
        pth->has_negix = 1;
        comp->is_neg = 1;
    } else {
        comp->is_neg = 0;
    }
    return JSONSL_ERROR_SUCCESS;
}

/* So this should somehow give us a 'JPR' object.. */
int subdoc_path_parse(subdoc_PATH *nj, const char *path, size_t len)
{
    /* Path's buffers cannot change */
    const char *c, *last, *path_end = path + len;
    int in_escape = 0;
    int in_bracket = 0;
    int n_backtick = 0;
    int rv;

    jsonsl_jpr_t jpr = &nj->jpr_base;
    jpr->components = nj->components_s;
    jpr->ncomponents = 0;
    jpr->orig = (char *)path;
    jpr->norig = len;

    /* Set up first component */
    jpr->components[0].ptype = JSONSL_PATH_ROOT;
    jpr->ncomponents++;
    nj->has_negix = 0;

    if (!len) {
        return 0;
    }

    for (last = c = path; c < path_end; c++) {
        if (*c == '`') {
            if (in_bracket) {
                return JSONSL_ERROR_JPR_BADPATH; /* no ` allowed in [] */
            }

            n_backtick++;
            if (c < path_end-1 && c[1] == '`') {
                n_backtick++, c++;
                continue;
            } else if (in_escape) {
                in_escape = 0;
            } else {
                in_escape = 1;
            }
            continue;
        }

        if (in_escape) {
            continue;
        }

        int comp_added = 0;

        if (*c == '[') {
            if (in_bracket) {
                return JSONSL_ERROR_JPR_BADPATH;
            }
            in_bracket = 1;

            /* There's a leading string portion, e.g. "foo[0]". Parse foo first */
            if (c-last) {
                rv = add_str_component(nj, last, c-last, n_backtick);
                comp_added = 1;
            } else {
                last = c + 1; /* Shift ahead to avoid the '[' */
            }

        } else if (*c == ']') {
            if (!in_bracket) {
                return JSONSL_ERROR_JPR_BADPATH;
            } else {
                /* Add numeric component here */
                in_bracket = 0;
                rv = add_num_component(nj, last, c-last);
                comp_added = 1;
                in_bracket = 0;
            }
        } else if (*c == '.') {
            rv = add_str_component(nj, last, c-last, n_backtick);
            comp_added = 1;
        }

        if (comp_added) {
            if (rv != 0) {
                return rv;
            } else {
                if (*c == ']' && c + 1 < path_end && c[1] == '.') {
                    c++; /* Skip over the following '.' */
                }
                last = c + 1;
                n_backtick = 0;
            }
        }
    }

    if (c-last) {
        return add_str_component(nj, last, c-last, n_backtick);
    } else {
        return 0;
    }
}

subdoc_PATH *
subdoc_path_alloc(void)
{
    return (subdoc_PATH *)calloc(1, sizeof(subdoc_PATH));
}

void
subdoc_path_clear(subdoc_PATH *nj)
{
    unsigned ii;
    jsonsl_jpr_t jpr = &nj->jpr_base;
    for (ii = 1; ii < jpr->ncomponents; ii++) {
        struct jsonsl_jpr_component_st *comp = &jpr->components[ii];
        if (comp->pstr == NULL) {
            /* nop */
        } else if (comp->pstr >= jpr->orig && comp->pstr < (jpr->orig + jpr->norig)) {
            /* nop */
        } else {
            free(comp->pstr);
        }
        comp->pstr = NULL;
        comp->is_arridx = 0;
    }
}

void
subdoc_path_free(subdoc_PATH *nj)
{
    subdoc_path_clear(nj);
    free(nj);
}
