#define INCLUDE_SUBDOC_STRING_SRC
#define INCLUDE_SUBDOC_NTOHLL

#include "operations.h"
#include <limits.h>
#include <errno.h>
#include <inttypes.h>

static subdoc_LOC loc_COMMA = { ",", 1 };
static subdoc_LOC loc_QUOTE = { "\"", 1 };
static subdoc_LOC loc_COMMA_QUOTE = { ",\"", 2 };
static subdoc_LOC loc_QUOTE_COLON = { "\":", 2 };

static uint16_t
do_match_common(subdoc_OPERATION *op)
{
    subdoc_match_exec(op->doc_cur.at, op->doc_cur.length, op->path, op->jsn, &op->match);
    if (op->match.matchres == JSONSL_MATCH_TYPE_MISMATCH) {
        return SUBDOC_STATUS_PATH_MISMATCH;
    } else if (op->match.status != JSONSL_ERROR_SUCCESS) {
        return SUBDOC_STATUS_DOC_NOTJSON;
    } else {
        return SUBDOC_STATUS_SUCCESS;
    }
}

static uint16_t
do_get(subdoc_OPERATION *op)
{
    if (op->match.matchres != JSONSL_MATCH_COMPLETE) {
        return SUBDOC_STATUS_PATH_ENOENT;
    }
    return SUBDOC_STATUS_SUCCESS;
}

/* Define how the 'until' parameter is treated. INCLUSIVE will make the result
 * overlap with 'until' (on a single byte)
 * whereas EXCLUSIVE will make sure they don't
 */
#define LOC_INC 1
#define LOC_EXCL 2

/*Sets `result`, so that `result` ends where `until` begins */
static void
mk_end_at_begin(const subdoc_LOC *doc, const subdoc_LOC *until, subdoc_LOC *result,
    int mode)
{
    result->at = doc->at;
    result->length = until->at - doc->at;
    if (mode == LOC_INC) {
        result->length++;
    }
}

static void
mk_end_at_end(const subdoc_LOC *doc, const subdoc_LOC *until, subdoc_LOC *result,
    int mode)
{
    result->at = doc->at;
    result->length = (until->at + until->length) - doc->at;
    if (mode == LOC_EXCL) {
        result->length--;
    }
}

/*Sets `result` so that result begins where `from` ends */
static void
mk_begin_at_end(const subdoc_LOC *doc, const subdoc_LOC *from, subdoc_LOC *result,
    int mode)
{
    result->at = from->at + from->length;
    result->length = doc->length;
    result->length -= result->at - doc->at;
    if (mode == LOC_INC) {
        result->at--;
        result->length++;
    }
}

static void
mk_begin_at_begin(const subdoc_LOC *doc, const subdoc_LOC *from, subdoc_LOC *result)
{
    result->at = from->at;
    result->length = doc->length - (from->at - doc->at);
}

/* Start at the beginning of the buffer, stripping first comma */
#define STRIP_FIRST_COMMA 1

/* Start at the end of the buffer, stripping last comma */
#define STRIP_LAST_COMMA 2

static void
strip_comma(subdoc_LOC *loc, int mode)
{
    unsigned ii;
    if (mode == STRIP_FIRST_COMMA) {
        for (ii = 0; ii < loc->length; ii++) {
            if (loc->at[ii] == ',') {
                loc->at += (ii + 1);
                loc->length -= (ii + 1);
                return;
            }
        }
    } else {
        for (ii = loc->length; ii; ii--) {
            if (loc->at[ii-1] == ',') {
                loc->length = ii-1;
                return;
            }
        }
    }
}

#define MKDIR_P_ARRAY 0
#define MKDIR_P_DICT 1
static uint16_t do_mkdir_p(subdoc_OPERATION *op, int mode);

static uint16_t
do_store_dict(subdoc_OPERATION *op)
{
    /* we can't do a simple get here, since it's a bit more complex than that */
    /* TODO: Validate new value! */

    const jsonsl_jpr_t jpr = &op->path->jpr_base;
    subdoc_MATCH *m = &op->match;

    /* Now it's time to get creative */
    if (op->match.matchres != JSONSL_MATCH_COMPLETE) {
        switch (op->optype) {
        case SUBDOC_CMD_DICT_ADD_P:
        case SUBDOC_CMD_DICT_UPSERT_P:
            break;

        case SUBDOC_CMD_DICT_ADD:
        case SUBDOC_CMD_DICT_UPSERT:
            if (!m->immediate_parent_found) {
                return SUBDOC_STATUS_PATH_ENOENT;
            }
            break;

        case SUBDOC_CMD_DELETE:
        case SUBDOC_CMD_REPLACE:
            /* etc. */
        default:
            return SUBDOC_STATUS_PATH_ENOENT;
        }
    } else if (m->matchres == JSONSL_MATCH_COMPLETE) {
        switch (op->optype) {
        case SUBDOC_CMD_DICT_ADD:
        case SUBDOC_CMD_DICT_ADD_P:
            return SUBDOC_STATUS_DOC_EEXISTS;
        }
    }

    if (op->optype == SUBDOC_CMD_DELETE) {
        /*
         * If match is the _last_ item, we need to strip the _last_ comma from
         * doc[0] (since doc[1] will never have a trailing comma); e.g.
         *  NEWDOC[0] = { ..., ..., ... (---> , <---)
         *  [deleted]
         *  NEWDOC[1] = } // Never any commas here!
         *
         * If match is the _first_ item, we need to strip the _first_ comma
         * from newdoc[1]:
         *
         *  NEWDOC[0] = { // never any commas here!
         *  [deleted]
         *  NEWDOC[1] = (---> , <---) ... , ... }
         *
         *
         *
         * If match is any other item, we need to strip the _last_ comma from
         * doc[0], which is the comma that preceded this item.
         */

        /* Remove the matches, starting from the beginning of the key */
        if (m->has_key) {
            mk_end_at_begin(&op->doc_cur, &m->loc_key, &op->doc_new[0], LOC_EXCL);
        } else {
            mk_end_at_begin(&op->doc_cur, &m->loc_match, &op->doc_new[0], LOC_EXCL);
        }

        mk_begin_at_end(&op->doc_cur, &m->loc_match, &op->doc_new[1], LOC_EXCL);

        if (m->num_siblings) {
            if (m->position + 1 == m->num_siblings) {
                /* Is the last item */
                strip_comma(&op->doc_new[0], STRIP_LAST_COMMA);
            } else {
                strip_comma(&op->doc_new[1], STRIP_FIRST_COMMA);
            }
        }
        op->doc_new_len = 2;

    } else if (m->matchres == JSONSL_MATCH_COMPLETE) {
        /* 1. Remove the old value from the first segment */
        mk_end_at_begin(&op->doc_cur, &m->loc_match, &op->doc_new[0], LOC_EXCL);

        /* 2. Insert the new value */
        op->doc_new[1] = op->user_in;

        /* 3. Insert the rest of the document */
        mk_begin_at_end(&op->doc_cur, &m->loc_match, &op->doc_new[2], LOC_EXCL);
        op->doc_new_len = 3;

    } else if (m->immediate_parent_found) {
        mk_end_at_end(&op->doc_cur, &m->loc_parent, &op->doc_new[0], LOC_EXCL);
        /*TODO: The key might have a literal '"' in it, which has been escaped? */
        if (m->num_siblings) {
            op->doc_new[1] = loc_COMMA_QUOTE; /* ," */
        } else {
            op->doc_new[1] = loc_QUOTE /* " */;
        }

        /* Create the actual key: */
        op->doc_new[2].at = jpr->components[jpr->ncomponents-1].pstr;
        op->doc_new[2].length = jpr->components[jpr->ncomponents-1].len;

        /* Close the quote and add the dictionary key */
        op->doc_new[3] = loc_QUOTE_COLON; /* ": */
        /* new value */
        op->doc_new[4] = op->user_in;
        /* Closing tokens */
        mk_begin_at_end(&op->doc_cur, &m->loc_parent, &op->doc_new[5], LOC_INC);

        op->doc_new_len = 6;

    } else {
        return do_mkdir_p(op, MKDIR_P_DICT);
    }
    return SUBDOC_STATUS_SUCCESS;
}

static uint16_t
do_mkdir_p(subdoc_OPERATION *op, int mode)
{
    const struct jsonsl_jpr_component_st *comp;
    jsonsl_jpr_t jpr = &op->path->jpr_base;
    unsigned ii;
    subdoc_MATCH *m = &op->match;

    mk_end_at_end(&op->doc_cur, &m->loc_parent, &op->doc_new[0], LOC_EXCL);

    #define DO_APPEND(s, n) if (subdoc_string_append(&op->bkbuf_extra, s, n) != 0) { return SUBDOC_STATUS_GLOBAL_ENOMEM; }
    #define DO_APPENDZ(s) DO_APPEND(s, sizeof(s)-1)

    /* doc_new LAYOUT:
     *
     * [0] = HEADER
     * [1] = _P header (bkbuf_extra)
     * [2] = USER TEXT
     * [3] = _P trailer (bkbuf_extra)
     * [4] = TRAILER
     */

    /* figure out the components missing */
    /* THIS IS RESERVED FOR doc_new[1]! */
    if (m->num_siblings) {
        DO_APPENDZ(",")
    }

    /* Insert the first item. This is a dictionary key without any object
     * wrapper: */
    comp = &jpr->components[m->match_level];

    DO_APPENDZ("\"");
    DO_APPEND(comp->pstr, comp->len);
    DO_APPENDZ("\":")

    /* The next set of components must be created as entries within the
     * newly created key */
    for (ii = m->match_level + 1; ii < jpr->ncomponents; ii++) {
        comp = &jpr->components[ii];
        if (comp->ptype != JSONSL_PATH_STRING) {
            return SUBDOC_STATUS_PATH_ENOENT;
        }
        DO_APPENDZ("{\"");
        DO_APPEND(comp->pstr, comp->len);
        DO_APPENDZ("\":");
    }
    if (mode == MKDIR_P_ARRAY) {
        DO_APPENDZ("[");
    }

    op->doc_new[1].length = op->bkbuf_extra.nused;

    if (mode == MKDIR_P_ARRAY) {
        DO_APPENDZ("]");
    }

    for (ii = m->match_level+1; ii < jpr->ncomponents; ii++) {
        DO_APPENDZ("}");
    }
    op->doc_new[3].length = op->bkbuf_extra.nused - op->doc_new[1].length;

    /* Set the buffers */
    op->doc_new[1].at = op->bkbuf_extra.base;
    op->doc_new[3].at = op->bkbuf_extra.base + op->doc_new[1].length;
    op->doc_new[2] = op->user_in;

    mk_begin_at_end(&op->doc_cur, &m->loc_parent, &op->doc_new[4], LOC_INC);
    op->doc_new_len = 5;

    return SUBDOC_STATUS_SUCCESS;
}

static uint16_t
find_first_element(subdoc_OPERATION *op)
{
    int rv;
    rv = subdoc_path_add_arrindex(op->path, 0);
    if (rv != JSONSL_ERROR_SUCCESS) {
        return SUBDOC_STATUS_PATH_E2BIG;
    }

    rv = do_match_common(op);
    subdoc_path_pop_component(op->path);

    if (rv != SUBDOC_STATUS_SUCCESS) {
        return rv;
    }
    if (op->match.matchres != JSONSL_MATCH_COMPLETE) {
        return SUBDOC_STATUS_PATH_ENOENT;
    }
    return SUBDOC_STATUS_SUCCESS;
}

/* Finds the last element. This normalizes the match structure so that
 * the last element appears in the 'loc_match' field */
static uint16_t
find_last_element(subdoc_OPERATION *op)
{
    subdoc_LOC *mloc = &op->match.loc_match;
    subdoc_LOC *ploc = &op->match.loc_parent;

    op->match.get_last_child_pos = 1;
    int rv = find_first_element(op);
    if (rv != SUBDOC_STATUS_SUCCESS) {
        return rv;
    }
    if (op->match.num_siblings == 0) {
        /* first is last */
        return SUBDOC_STATUS_SUCCESS;
    }

    mloc->at = op->doc_cur.at + op->match.loc_key.length;
    /* Length of the last child is the difference between the child's
     * start position, and the parent's end position */
    mloc->length = (ploc->at + ploc->length) - mloc->at;
    /* Exclude the parent's token */
    mloc->length--;

    /* Finally, set the position */
    op->match.position = op->match.num_siblings-1;

    return SUBDOC_STATUS_SUCCESS;
}

/* Inserts a single element into an empty array */
static uint16_t
insert_singleton_element(subdoc_OPERATION *op)
{
    /* First segment is ... [ */
    mk_end_at_begin(&op->doc_cur, &op->match.loc_parent, &op->doc_new[0], LOC_INC);
    /* User: */
    op->doc_new[1] = op->user_in;
    /* Last segment is ... ] */
    mk_begin_at_end(&op->doc_cur, &op->match.loc_parent, &op->doc_new[2], LOC_INC);

    op->doc_new_len = 3;
    return SUBDOC_STATUS_SUCCESS;
}


static uint16_t
do_list_op(subdoc_OPERATION *op)
{
    #define HANDLE_LISTADD_ENOENT(rv) \
    if (rv == SUBDOC_STATUS_PATH_ENOENT) { \
        if (m->immediate_parent_found) { \
            return insert_singleton_element(op); \
        } else { \
            return rv; \
        } \
    }

    #define HANDLE_LISTADD_ENOENT_P(rv) \
    if (rv == SUBDOC_STATUS_PATH_ENOENT) { \
        if (m->immediate_parent_found) { \
            return insert_singleton_element(op); \
        } else { \
            return do_mkdir_p(op, MKDIR_P_ARRAY); \
        } \
    }

    int rv;
    subdoc_MATCH *m = &op->match;
    if (op->optype == SUBDOC_CMD_ARRAY_PREPEND) {
        /* Find the array itself. */
        rv = find_first_element(op);

        HANDLE_LISTADD_ENOENT(rv);
        if (rv != SUBDOC_STATUS_SUCCESS) {
            return rv;
        }

        /* LAYOUT:
         * NEWDOC[0] = .... [
         * NEWDOC[1] = USER
         * NEWDOC[2] = ,
         * NEWDOC[3] = REST
         */

        GT_PREPEND_FOUND:
        /* Right before the first element */
        mk_end_at_begin(&op->doc_cur, &m->loc_match, &op->doc_new[0], LOC_EXCL);
        /* User data */
        op->doc_new[1] = op->user_in;
        /* Comma */
        op->doc_new[2] = loc_COMMA;
        /* Next element */
        mk_begin_at_begin(&op->doc_cur, &m->loc_match, &op->doc_new[3]);

        op->doc_new_len = 4;
        return SUBDOC_STATUS_SUCCESS;

    } else if (op->optype == SUBDOC_CMD_ARRAY_APPEND) {
        rv = find_last_element(op);

        HANDLE_LISTADD_ENOENT(rv);
        if (rv != SUBDOC_STATUS_SUCCESS) {
            return rv;
        }

        GT_APPEND_FOUND:
        /* Last element */
        mk_end_at_end(&op->doc_cur, &m->loc_match, &op->doc_new[0], LOC_INC);
        /* Insert comma */
        op->doc_new[1] = loc_COMMA;
        /* User */
        op->doc_new[2] = op->user_in;
        /* Parent end */
        mk_begin_at_end(&op->doc_cur, &m->loc_parent, &op->doc_new[3], LOC_INC);

        op->doc_new_len = 4;
        return SUBDOC_STATUS_SUCCESS;

    } else if (op->optype == SUBDOC_CMD_ARRAY_PREPEND_P) {
        rv = find_first_element(op);
        if (rv == SUBDOC_STATUS_SUCCESS) {
            goto GT_PREPEND_FOUND;
        }

        GT_ARR_P_COMMON:
        HANDLE_LISTADD_ENOENT_P(rv);
        return rv;

    } else if (op->optype == SUBDOC_CMD_ARRAY_APPEND_P) {
        rv = find_last_element(op);
        if (rv == SUBDOC_STATUS_SUCCESS) {
            goto GT_APPEND_FOUND;
        }
        goto GT_ARR_P_COMMON;

    } else if (op->optype == SUBDOC_CMD_ARRAY_ADD_UNIQUE) {
        m->ensure_unique = op->user_in;
        rv = find_first_element(op);
        HANDLE_LISTADD_ENOENT(rv);

        GT_ADD_UNIQUE:
        if (rv != SUBDOC_STATUS_SUCCESS) {
            /* mismatch, perhaps? */
            return rv;
        }
        if (m->unique_item_found) {
            return SUBDOC_STATUS_DOC_EEXISTS;
        }
        goto GT_PREPEND_FOUND;

    } else if (op->optype == SUBDOC_CMD_ARRAY_ADD_UNIQUE_P) {
        m->ensure_unique = op->user_in;
        rv = find_first_element(op);
        HANDLE_LISTADD_ENOENT_P(rv);
        goto GT_ADD_UNIQUE;
    }

    return SUBDOC_STATUS_SUCCESS;
}

static uint16_t
do_arith_op(subdoc_OPERATION *op)
{
    uint16_t status;
    int64_t num_i;
    int64_t delta;
    uint64_t tmp;
    size_t n_buf;

    /* Scan the match first */
    if (op->user_in.length != 8) {
        return SUBDOC_STATUS_GLOBAL_EINVAL;
    }

    memcpy(&tmp, op->user_in.at, 8);
    tmp = ntohll(tmp);
    if (tmp > INT64_MAX) {
        return SUBDOC_STATUS_DELTA_E2BIG;
    }

    delta = tmp;

    switch (op->optype) {
    case SUBDOC_CMD_DECREMENT:
    case SUBDOC_CMD_DECREMENT_P:
        delta *= -1;
        break;
    }

    /* Find the number first */
    status = do_match_common(op);
    if (status != SUBDOC_STATUS_SUCCESS) {
        return status;
    }

    if (op->match.matchres == JSONSL_MATCH_COMPLETE) {
        if (op->match.type != JSONSL_T_SPECIAL) {
            return SUBDOC_STATUS_PATH_MISMATCH;
        } else if (op->match.sflags & ~(JSONSL_SPECIALf_NUMERIC)) {
            return SUBDOC_STATUS_PATH_MISMATCH;
        } else  {
            num_i = strtoll(op->match.loc_match.at, NULL, 10);
            if (num_i == LLONG_MAX && errno == ERANGE) {
                return SUBDOC_STATUS_NUM_E2BIG;
            }

            /* Calculate what to place inside the buffer. We want to be gentle here
             * and not force 64 bit C arithmetic to confuse users, so use proper
             * integer overflow/underflow with a 64 (or rather, 63) bit limit. */
            if (delta >= 0 && num_i >= 0) {
                if (INT64_MAX - delta <= num_i) {
                    return SUBDOC_STATUS_DELTA_E2BIG;
                }
            } else if (delta < 0 && num_i < 0) {
                if (delta <= INT64_MIN - num_i) {
                    return SUBDOC_STATUS_DELTA_E2BIG;
                }
            }

            num_i += delta;
            n_buf = sprintf(op->numbufs, "%" PRId64, num_i);
        }
    } else {
        switch (op->optype) {
        case SUBDOC_CMD_INCREMENT:
        case SUBDOC_CMD_DECREMENT:
            if (!op->match.immediate_parent_found) {
                return SUBDOC_STATUS_PATH_ENOENT;
            }
            break;
        }

        if (op->match.type != JSONSL_T_OBJECT) {
            return SUBDOC_STATUS_PATH_ENOENT;
        }

        n_buf = sprintf(op->numbufs, "%" PRId64, delta);
        op->user_in.at = op->numbufs;
        op->user_in.length = n_buf;
        op->optype = SUBDOC_CMD_DICT_ADD_P;
        if ((status = do_store_dict(op)) != SUBDOC_STATUS_SUCCESS) {
            return status;
        }
        op->match.loc_match = op->user_in;
        return SUBDOC_STATUS_SUCCESS;
    }


    /* Preamble */
    mk_end_at_begin(&op->doc_cur, &op->match.loc_match, &op->doc_new[0], LOC_EXCL);

    /* New number */
    op->doc_new[1].at = op->numbufs;
    op->doc_new[1].length = n_buf;

    /* Postamble */
    mk_begin_at_end(&op->doc_cur, &op->match.loc_match, &op->doc_new[2], LOC_EXCL);
    op->doc_new_len = 3;

    op->match.loc_match.at = op->numbufs;
    op->match.loc_match.length = n_buf;
    return SUBDOC_STATUS_SUCCESS;
}

uint16_t
subdoc_op_exec(subdoc_OPERATION *op, const char *pth, size_t npth)
{
    int rv = subdoc_path_parse(op->path, pth, npth);
    uint16_t status;

    if (rv != 0) {
        return SUBDOC_STATUS_PATH_EINVAL;
    }

    switch (op->optype) {
    case SUBDOC_CMD_GET:
    case SUBDOC_CMD_EXISTS:
        status = do_match_common(op);
        if (status != SUBDOC_STATUS_SUCCESS) {
            return status;
        }
        return do_get(op);

    case SUBDOC_CMD_DICT_ADD:
    case SUBDOC_CMD_DICT_ADD_P:
    case SUBDOC_CMD_DICT_UPSERT:
    case SUBDOC_CMD_DICT_UPSERT_P:
    case SUBDOC_CMD_REPLACE:
    case SUBDOC_CMD_DELETE:
        if (op->path->jpr_base.ncomponents == 1) {
            /* Can't perform these operations on the root element since they
             * will invalidate the JSON or are otherwise meaningless. */
            return SUBDOC_STATUS_VALUE_CANTINSERT;
        }

        if (op->user_in.length) {
            rv = subdoc_validate(op->user_in.at, op->user_in.length, op->jsn,
                SUBDOC_VALIDATE_PARENT_DICT);
            if (rv != JSONSL_ERROR_SUCCESS) {
                return SUBDOC_STATUS_VALUE_CANTINSERT;
            }
        }
        status = do_match_common(op);
        if (status != SUBDOC_STATUS_SUCCESS) {
            return status;
        }
        return do_store_dict(op);
    case SUBDOC_CMD_ARRAY_APPEND:
    case SUBDOC_CMD_ARRAY_PREPEND:
    case SUBDOC_CMD_ARRAY_APPEND_P:
    case SUBDOC_CMD_ARRAY_PREPEND_P:
    case SUBDOC_CMD_ARRAY_ADD_UNIQUE:
    case SUBDOC_CMD_ARRAY_ADD_UNIQUE_P:
        if (op->user_in.length) {
            rv = subdoc_validate(op->user_in.at, op->user_in.length, op->jsn,
                SUBDOC_VALIDATE_PARENT_ARRAY);
            if (rv != JSONSL_ERROR_SUCCESS) {
                return SUBDOC_STATUS_VALUE_CANTINSERT;
            }
        }
        return do_list_op(op);

    case SUBDOC_CMD_INCREMENT:
    case SUBDOC_CMD_INCREMENT_P:
    case SUBDOC_CMD_DECREMENT:
    case SUBDOC_CMD_DECREMENT_P:
        return do_arith_op(op);

    default:
        return SUBDOC_STATUS_GLOBAL_ENOSUPPORT;

    }
}

subdoc_OPERATION *
subdoc_op_alloc(void)
{
    subdoc_OPERATION *op = (subdoc_OPERATION*) calloc(1, sizeof(*op));

    if (op == NULL) {
        return NULL;
    }

    op->path = subdoc_path_alloc();
    op->jsn = subdoc_jsn_alloc();
    subdoc_string_init(&op->bkbuf_extra);

    if (op->path == NULL || op->jsn == NULL) {
        if (op->path) {
            subdoc_path_free(op->path);
        }
        if (op->jsn) {
            subdoc_jsn_free(op->jsn);
        }
        free(op);
        return NULL;
    }
    return op;
}

void
subdoc_op_clear(subdoc_OPERATION *op)
{
    subdoc_path_clear(op->path);
    subdoc_string_clear(&op->bkbuf_extra);

    op->user_in.length = 0;
    op->user_in.at = NULL;
    op->doc_new_len = 0;
    op->optype = SUBDOC_CMD_GET;

    memset(&op->match, 0, sizeof op->match);
}

void
subdoc_op_free(subdoc_OPERATION *op)
{
    subdoc_op_clear(op);
    subdoc_path_free(op->path);
    subdoc_jsn_free(op->jsn);
    subdoc_string_release(&op->bkbuf_extra);
    free(op);
}

/* Misc */
const char *
subdoc_strerror(uint16_t rc)
{
    switch (rc) {
    case SUBDOC_STATUS_SUCCESS:
        return "Success";
    case SUBDOC_STATUS_PATH_ENOENT:
        return "Requested path does not exist in document";
    case SUBDOC_STATUS_PATH_MISMATCH:
        return "The path specified treats an existing document entry as the wrong type";
    case SUBDOC_STATUS_PATH_EINVAL:
        return "Path syntax error";
    case SUBDOC_STATUS_DOC_NOTJSON:
        return "The document is not JSON";
    case SUBDOC_STATUS_DOC_EEXISTS:
        return "The requested path already exists";
    case SUBDOC_STATUS_PATH_E2BIG:
        return "The path is too big";
    case SUBDOC_STATUS_NUM_E2BIG:
        return "The number specified by the path is too big";
    case SUBDOC_STATUS_DELTA_E2BIG:
        return "The combination of the existing number and the delta will result in an underflow or overflow";
    case SUBDOC_STATUS_VALUE_CANTINSERT:
        return "The new value cannot be inserted in the context of the path, as it would invalidate the JSON";
    case SUBDOC_STATUS_GLOBAL_ENOMEM:
        return "Couldn't allocate memory";
    case SUBDOC_STATUS_GLOBAL_ENOSUPPORT:
        return "Operation not implemented";
    case SUBDOC_STATUS_GLOBAL_UNKNOWN_COMMAND:
        return "Unrecognized command code";
    default:
        return "Unknown error code";
    }
}
