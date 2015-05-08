#define INCLUDE_SUBDOC_STRING_SRC
#define INCLUDE_SUBDOC_NTOHLL
#define NOMINMAX // For Visual Studio

#include "operations.h"
#include <errno.h>
#include <inttypes.h>
#include <string>
#include <limits>

using Subdoc::Loc;
using Subdoc::Error;
using Subdoc::Operation;
using Subdoc::Path;
using Subdoc::Match;

static Loc loc_COMMA = { ",", 1 };
static Loc loc_QUOTE = { "\"", 1 };
static Loc loc_COMMA_QUOTE = { ",\"", 2 };
static Loc loc_QUOTE_COLON = { "\":", 2 };

Error
Operation::do_match_common()
{
    match.exec_match(doc_cur, path, jsn);
    if (match.matchres == JSONSL_MATCH_TYPE_MISMATCH) {
        return SUBDOC_STATUS_PATH_MISMATCH;
    } else if (match.status != JSONSL_ERROR_SUCCESS) {
        if (match.status == JSONSL_ERROR_LEVELS_EXCEEDED) {
            return SUBDOC_STATUS_DOC_ETOODEEP;
        } else {
            return SUBDOC_STATUS_DOC_NOTJSON;
        }
    } else {
        return SUBDOC_STATUS_SUCCESS;
    }
}

Error
Operation::do_get()
{
    if (match.matchres != JSONSL_MATCH_COMPLETE) {
        return SUBDOC_STATUS_PATH_ENOENT;
    }
    return SUBDOC_STATUS_SUCCESS;
}

/* Start at the beginning of the buffer, stripping first comma */
#define STRIP_FIRST_COMMA 1

/* Start at the end of the buffer, stripping last comma */
#define STRIP_LAST_COMMA 2

static void
strip_comma(Loc *loc, int mode)
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

Error
Operation::do_store_dict()
{
    /* we can't do a simple get here, since it's a bit more complex than that */
    /* TODO: Validate new value! */

    const jsonsl_jpr_t jpr = &path->jpr_base;

    /* Now it's time to get creative */
    if (match.matchres != JSONSL_MATCH_COMPLETE) {
        switch (optype) {
        case SUBDOC_CMD_DICT_ADD_P:
        case SUBDOC_CMD_DICT_UPSERT_P:
            break;

        case SUBDOC_CMD_DICT_ADD:
        case SUBDOC_CMD_DICT_UPSERT:
            if (!match.immediate_parent_found) {
                return SUBDOC_STATUS_PATH_ENOENT;
            }
            break;

        case SUBDOC_CMD_DELETE:
        case SUBDOC_CMD_REPLACE:
            /* etc. */
        default:
            return SUBDOC_STATUS_PATH_ENOENT;
        }
    } else if (match.matchres == JSONSL_MATCH_COMPLETE) {
        if ((optype == SUBDOC_CMD_DICT_ADD) ||
            (optype == SUBDOC_CMD_DICT_ADD_P)) {
            return SUBDOC_STATUS_DOC_EEXISTS;
        }
    }

    if (optype == SUBDOC_CMD_DELETE) {
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
        if (match.has_key) {
            doc_new[0].end_at_begin(doc_cur, match.loc_key, Loc::NO_OVERLAP);
        } else {
            doc_new[0].end_at_begin(doc_cur, match.loc_match, Loc::NO_OVERLAP);
        }

        doc_new[1].begin_at_end(doc_cur, match.loc_match, Loc::NO_OVERLAP);

        if (match.num_siblings) {
            if (match.is_last()) {
                /*
                 * NEWDOC[0] = [a,b,c, <-- Strip here
                 * MATCH     = d
                 * NEWDOC[1] = ]
                 */
                strip_comma(&doc_new[0], STRIP_LAST_COMMA);
            } else {
                /*
                 * NEWDOC[0] = [a,b,
                 * MATCH     = c
                 * NEWDOC[1] = Strip here -->, d]
                 */
                strip_comma(&doc_new[1], STRIP_FIRST_COMMA);
            }
        }

        doc_new_len = 2;

    } else if (match.matchres == JSONSL_MATCH_COMPLETE) {
        /* 1. Remove the old value from the first segment */
        doc_new[0].end_at_begin(doc_cur, match.loc_match, Loc::NO_OVERLAP);

        /* 2. Insert the new value */
        doc_new[1] = user_in;

        /* 3. Insert the rest of the document */
        doc_new[2].begin_at_end(doc_cur, match.loc_match, Loc::NO_OVERLAP);
        doc_new_len = 3;

    } else if (match.immediate_parent_found) {
        doc_new[0].end_at_end(doc_cur, match.loc_parent, Loc::NO_OVERLAP);
        /*TODO: The key might have a literal '"' in it, which has been escaped? */
        if (match.num_siblings) {
            doc_new[1] = loc_COMMA_QUOTE; /* ," */
        } else {
            doc_new[1] = loc_QUOTE /* " */;
        }

        /* Create the actual key: */
        doc_new[2].at = jpr->components[jpr->ncomponents-1].pstr;
        doc_new[2].length = jpr->components[jpr->ncomponents-1].len;

        /* Close the quote and add the dictionary key */
        doc_new[3] = loc_QUOTE_COLON; /* ": */
        /* new value */
        doc_new[4] = user_in;
        /* Closing tokens */
        doc_new[5].begin_at_end(doc_cur, match.loc_parent, Loc::OVERLAP);
        doc_new_len = 6;

    } else {
        return do_mkdir_p(MKDIR_P_DICT);
    }
    return SUBDOC_STATUS_SUCCESS;
}

Error
Operation::do_mkdir_p(int mode)
{
    const struct jsonsl_jpr_component_st *comp;
    jsonsl_jpr_t jpr = &path->jpr_base;
    unsigned ii;
    doc_new[0].end_at_end(doc_cur, match.loc_parent, Loc::NO_OVERLAP);

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
    if (match.num_siblings) {
        bkbuf += ',';
    }

    /* Insert the first item. This is a dictionary key without any object
     * wrapper: */
    comp = &jpr->components[match.match_level];
    bkbuf += '"';
    bkbuf.append(comp->pstr, comp->len);
    bkbuf += "\":";

    /* The next set of components must be created as entries within the
     * newly created key */
    for (ii = match.match_level + 1; ii < jpr->ncomponents; ii++) {
        comp = &jpr->components[ii];
        if (comp->ptype != JSONSL_PATH_STRING) {
            return SUBDOC_STATUS_PATH_ENOENT;
        }
        bkbuf += "{\"";
        bkbuf.append(comp->pstr, comp->len);
        bkbuf += "\":";
    }
    if (mode == MKDIR_P_ARRAY) {
        bkbuf += '[';
    }

    doc_new[1].length = bkbuf.size();

    if (mode == MKDIR_P_ARRAY) {
        bkbuf += ']';
    }

    for (ii = match.match_level+1; ii < jpr->ncomponents; ii++) {
        bkbuf += '}';
    }
    doc_new[3].length = bkbuf.size() - doc_new[1].length;

    /* Set the buffers */
    doc_new[1].at = bkbuf.data();
    doc_new[3].at = bkbuf.data() + doc_new[1].length;
    doc_new[2] = user_in;

    doc_new[4].begin_at_end(doc_cur, match.loc_parent, Loc::OVERLAP);
    doc_new_len = 5;

    return SUBDOC_STATUS_SUCCESS;
}

Error
Operation::find_first_element()
{
    jsonsl_error_t rv = path->add_array_index(0);
    if (rv != JSONSL_ERROR_SUCCESS) {
        return SUBDOC_STATUS_PATH_E2BIG;
    }

    Error status = do_match_common();
    path->pop_component();

    if (status != SUBDOC_STATUS_SUCCESS) {
        return status;
    }
    if (match.matchres != JSONSL_MATCH_COMPLETE) {
        return SUBDOC_STATUS_PATH_ENOENT;
    }
    return SUBDOC_STATUS_SUCCESS;
}

/* Finds the last element. This normalizes the match structure so that
 * the last element appears in the 'loc_match' field */
Error
Operation::find_last_element()
{
    Loc *mloc = &match.loc_match;
    Loc *ploc = &match.loc_parent;

    match.get_last_child_pos = 1;
    Error rv = find_first_element();
    if (rv != SUBDOC_STATUS_SUCCESS) {
        return rv;
    }
    if (match.num_siblings == 0) {
        /* first is last */
        return SUBDOC_STATUS_SUCCESS;
    }

    mloc->at = doc_cur.at + match.loc_key.length;
    /* Length of the last child is the difference between the child's
     * start position, and the parent's end position */
    mloc->length = (ploc->at + ploc->length) - mloc->at;
    /* Exclude the parent's token */
    mloc->length--;

    /* Finally, set the position */
    match.position = match.num_siblings;

    return SUBDOC_STATUS_SUCCESS;
}

/* Inserts a single element into an empty array */
Error
Operation::insert_singleton_element()
{
    /* First segment is ... [ */
    doc_new[0].end_at_begin(doc_cur, match.loc_parent, Loc::OVERLAP);
    /* User: */
    doc_new[1] = user_in;
    /* Last segment is ... ] */
    doc_new[2].begin_at_end(doc_cur, match.loc_parent, Loc::OVERLAP);

    doc_new_len = 3;
    return SUBDOC_STATUS_SUCCESS;
}


Error
Operation::do_list_op()
{
    #define HANDLE_LISTADD_ENOENT(rv) \
    if (rv == SUBDOC_STATUS_PATH_ENOENT) { \
        if (match.immediate_parent_found) { \
            return insert_singleton_element(); \
        } else { \
            return rv; \
        } \
    }

    #define HANDLE_LISTADD_ENOENT_P(rv) \
    if (rv == SUBDOC_STATUS_PATH_ENOENT) { \
        if (match.immediate_parent_found) { \
            return insert_singleton_element(); \
        } else { \
            return do_mkdir_p(MKDIR_P_ARRAY); \
        } \
    }

    Error rv;
    if (optype == SUBDOC_CMD_ARRAY_PREPEND) {
        /* Find the array itself. */
        rv = find_first_element();

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
        doc_new[0].end_at_begin(doc_cur, match.loc_match, Loc::NO_OVERLAP);
        /* User data */
        doc_new[1] = user_in;
        /* Comma */
        doc_new[2] = loc_COMMA;
        /* Next element */
        doc_new[3].begin_at_begin(doc_cur, match.loc_match);

        doc_new_len = 4;
        return SUBDOC_STATUS_SUCCESS;

    } else if (optype == SUBDOC_CMD_ARRAY_APPEND) {
        rv = find_last_element();

        HANDLE_LISTADD_ENOENT(rv);
        if (rv != SUBDOC_STATUS_SUCCESS) {
            return rv;
        }

        GT_APPEND_FOUND:
        /* Last element */
        doc_new[0].end_at_end(doc_cur, match.loc_match, Loc::OVERLAP);
        /* Insert comma */
        doc_new[1] = loc_COMMA;
        /* User */
        doc_new[2] = user_in;
        /* Parent end */
        doc_new[3].begin_at_end(doc_cur, match.loc_parent, Loc::OVERLAP);

        doc_new_len = 4;
        return SUBDOC_STATUS_SUCCESS;

    } else if (optype == SUBDOC_CMD_ARRAY_PREPEND_P) {
        rv = find_first_element();
        if (rv == SUBDOC_STATUS_SUCCESS) {
            goto GT_PREPEND_FOUND;
        }

        GT_ARR_P_COMMON:
        HANDLE_LISTADD_ENOENT_P(rv);
        return rv;

    } else if (optype == SUBDOC_CMD_ARRAY_APPEND_P) {
        rv = find_last_element();
        if (rv == SUBDOC_STATUS_SUCCESS) {
            goto GT_APPEND_FOUND;
        }
        goto GT_ARR_P_COMMON;

    } else if (optype == SUBDOC_CMD_ARRAY_ADD_UNIQUE) {
        match.ensure_unique = user_in;
        rv = find_first_element();
        HANDLE_LISTADD_ENOENT(rv);

        GT_ADD_UNIQUE:
        if (rv != SUBDOC_STATUS_SUCCESS) {
            /* mismatch, perhaps? */
            return rv;
        }
        if (match.unique_item_found) {
            return SUBDOC_STATUS_DOC_EEXISTS;
        }
        goto GT_PREPEND_FOUND;

    } else if (optype == SUBDOC_CMD_ARRAY_ADD_UNIQUE_P) {
        match.ensure_unique = user_in;
        rv = find_first_element();
        HANDLE_LISTADD_ENOENT_P(rv);
        goto GT_ADD_UNIQUE;
    }

    return SUBDOC_STATUS_SUCCESS;
}

Error
Operation::do_arith_op()
{
    Error status;
    int64_t num_i;
    int64_t delta;
    uint64_t tmp;

    /* Scan the match first */
    if (user_in.length != 8) {
        return SUBDOC_STATUS_GLOBAL_EINVAL;
    }

    memcpy(&tmp, user_in.at, 8);
    tmp = ntohll(tmp);
    if (tmp > std::numeric_limits<int64_t>::max()) {
        return SUBDOC_STATUS_DELTA_E2BIG;
    }

    delta = tmp;

    if ((optype == SUBDOC_CMD_DECREMENT) ||
        (optype == SUBDOC_CMD_DECREMENT_P)) {
        delta *= -1;
    }

    /* Find the number first */
    status = do_match_common();
    if (status != SUBDOC_STATUS_SUCCESS) {
        return status;
    }

    if (match.matchres == JSONSL_MATCH_COMPLETE) {
        if (match.type != JSONSL_T_SPECIAL) {
            return SUBDOC_STATUS_PATH_MISMATCH;
        } else if (match.sflags & ~(JSONSL_SPECIALf_NUMERIC)) {
            return SUBDOC_STATUS_PATH_MISMATCH;
        } else  {
            num_i = strtoll(match.loc_match.at, NULL, 10);
            if (num_i == std::numeric_limits<int64_t>::max() && errno == ERANGE) {
                return SUBDOC_STATUS_NUM_E2BIG;
            }

            /* Calculate what to place inside the buffer. We want to be gentle here
             * and not force 64 bit C arithmetic to confuse users, so use proper
             * integer overflow/underflow with a 64 (or rather, 63) bit limit. */
            if (delta >= 0 && num_i >= 0) {
                if (std::numeric_limits<int64_t>::max() - delta <= num_i) {
                    return SUBDOC_STATUS_DELTA_E2BIG;
                }
            } else if (delta < 0 && num_i < 0) {
                if (delta <= std::numeric_limits<int64_t>::min() - num_i) {
                    return SUBDOC_STATUS_DELTA_E2BIG;
                }
            }

            num_i += delta;
            numbuf = std::to_string(num_i);
        }
    } else {
        if ((optype == SUBDOC_CMD_INCREMENT) ||
            (optype == SUBDOC_CMD_DECREMENT)) {
            if (!match.immediate_parent_found) {
                return SUBDOC_STATUS_PATH_ENOENT;
            }
        }

        if (match.type != JSONSL_T_OBJECT) {
            return SUBDOC_STATUS_PATH_ENOENT;
        }

        numbuf = std::to_string(delta);
        user_in.at = numbuf.data();
        user_in.length = numbuf.size();
        optype = SUBDOC_CMD_DICT_ADD_P;
        if ((status = do_store_dict()) != SUBDOC_STATUS_SUCCESS) {
            return status;
        }
        match.loc_match = user_in;
        return SUBDOC_STATUS_SUCCESS;
    }


    /* Preamble */
    doc_new[0].end_at_begin(doc_cur, match.loc_match, Loc::NO_OVERLAP);

    /* New number */
    doc_new[1].at = numbuf.data();
    doc_new[1].length = numbuf.size();

    /* Postamble */
    doc_new[2].begin_at_end(doc_cur, match.loc_match, Loc::NO_OVERLAP);
    doc_new_len = 3;

    match.loc_match.at = numbuf.data();
    match.loc_match.length = numbuf.size();
    return SUBDOC_STATUS_SUCCESS;
}

Error
Operation::op_exec(const char *pth, size_t npth)
{
    int rv = path->parse(pth, npth);
    Error status;

    if (rv != 0) {
        if (rv == JSONSL_ERROR_LEVELS_EXCEEDED) {
            return SUBDOC_STATUS_PATH_E2BIG;
        } else {
            return SUBDOC_STATUS_PATH_EINVAL;
        }
    }

    switch (optype) {
    case SUBDOC_CMD_GET:
    case SUBDOC_CMD_EXISTS:
        status = do_match_common();
        if (status != SUBDOC_STATUS_SUCCESS) {
            return status;
        }
        return do_get();

    case SUBDOC_CMD_DICT_ADD:
    case SUBDOC_CMD_DICT_ADD_P:
    case SUBDOC_CMD_DICT_UPSERT:
    case SUBDOC_CMD_DICT_UPSERT_P:
    case SUBDOC_CMD_REPLACE:
    case SUBDOC_CMD_DELETE:
        if (path->jpr_base.ncomponents == 1) {
            /* Can't perform these operations on the root element since they
             * will invalidate the JSON or are otherwise meaningless. */
            return SUBDOC_STATUS_VALUE_CANTINSERT;
        }

        if (user_in.length) {
            rv = Match::validate(user_in.at, user_in.length, jsn,
                SUBDOC_VALIDATE_PARENT_DICT);
            if (rv != JSONSL_ERROR_SUCCESS) {
                return SUBDOC_STATUS_VALUE_CANTINSERT;
            }
        }
        status = do_match_common();
        if (status != SUBDOC_STATUS_SUCCESS) {
            return status;
        }
        return do_store_dict();
    case SUBDOC_CMD_ARRAY_APPEND:
    case SUBDOC_CMD_ARRAY_PREPEND:
    case SUBDOC_CMD_ARRAY_APPEND_P:
    case SUBDOC_CMD_ARRAY_PREPEND_P:
    case SUBDOC_CMD_ARRAY_ADD_UNIQUE:
    case SUBDOC_CMD_ARRAY_ADD_UNIQUE_P:
        if (user_in.length) {
            rv = Match::validate(user_in.at, user_in.length, jsn,
                SUBDOC_VALIDATE_PARENT_ARRAY);
            if (rv != JSONSL_ERROR_SUCCESS) {
                return SUBDOC_STATUS_VALUE_CANTINSERT;
            }
        }
        return do_list_op();

    case SUBDOC_CMD_INCREMENT:
    case SUBDOC_CMD_INCREMENT_P:
    case SUBDOC_CMD_DECREMENT:
    case SUBDOC_CMD_DECREMENT_P:
        return do_arith_op();

    default:
        return SUBDOC_STATUS_GLOBAL_ENOSUPPORT;

    }
}

Operation::Operation()
: path(new Path()),
  jsn(Match::jsn_alloc()),
  optype(SUBDOC_CMD_GET),
  doc_new_len(0)
{
}

Operation::~Operation()
{
    clear();
    delete path;
    Match::jsn_free(jsn);
}

void
Operation::clear()
{
    path->clear();
    bkbuf.clear();
    match.clear();
    user_in.length = 0;
    user_in.at = NULL;
    doc_new_len = 0;
    optype = SUBDOC_CMD_GET;
}

Operation *
subdoc_op_alloc()
{
    return new Operation();
}

void
subdoc_op_free(Operation *op)
{
    delete op;
}

/* Misc */
const char *
subdoc_strerror(Error rc)
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
