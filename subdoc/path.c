#define INCLUDE_JSONSL_SRC
#include "subdoc-api.h"
#include "path.h"

static char *
convert_escaped(const char *src, size_t *len)
{
    unsigned ii, oix;

    char *ret = malloc(*len);
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

static int
add_component(subdoc_PATH *nj, const char *component, size_t len, int n_backtick)
{
    int has_numix = 0;
    uint64_t numix;
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



    /* Because an array index is always at the (unescaped) end, we don't need
     * to check if we have backticks before checking for array indices */
    if (component[len-1] == ']') {
        unsigned ii;
        size_t a_len = 0;
        has_numix = 1;
        for (; len != 0; --len, a_len++) {
            if (component[len] == '[') {
                break;
            }
        }

        if (len == 0 && *component != '[') {
            /* Mismatch! */
            return -1;
        }

        /* end is a ']' */
        a_len--;
        for (ii = 1, numix = 0; ii < a_len; ii++) {
            const char *c = component + len + ii;
            if (*c < 0x30 || *c > 0x39) {
                if (ii == 1 && c[0] == '-') {
                    has_numix = -1;
                    continue;
                }
                /* not a number */
                return JSONSL_ERROR_INVALID_NUMBER;
            }
            numix *= 10;
            numix += *c - 0x30;
        }
    }

    if (len) {
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
    }

    if (has_numix) {
        if (has_numix == -1) {
            numix = -1;
        }
        return subdoc_path_add_arrindex(nj, numix);
    }
    return 0;
}

int
subdoc_path_add_arrindex(subdoc_PATH *pth, size_t ixnum)
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

    for (last = c = path; c < path+len; c++) {
        if (*c == '`') {
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
        if (*c == '.') {
            /* Delimiter */
            rv = add_component(nj, last, c-last, n_backtick);
            if (rv != 0) {
                return rv;
            }

            last = c + 1;
            n_backtick = 0;
        }
    }
    return add_component(nj, last, c-last, n_backtick);
}

subdoc_PATH *
subdoc_path_alloc(void)
{
    return calloc(1, sizeof(subdoc_PATH));
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
