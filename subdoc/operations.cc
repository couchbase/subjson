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

#define INCLUDE_SUBDOC_STRING_SRC
#define INCLUDE_SUBDOC_NTOHLL
#define NOMINMAX // For Visual Studio

#include "operations.h"
#include "validate.h"
#include <errno.h>
#include <inttypes.h>
#include <string>
#include <limits>

using Subdoc::Loc;
using Subdoc::Error;
using Subdoc::Operation;
using Subdoc::Path;
using Subdoc::Match;
using Subdoc::Command;

static Loc loc_COMMA = { ",", 1 };
static Loc loc_QUOTE = { "\"", 1 };
static Loc loc_COMMA_QUOTE = { ",\"", 2 };
static Loc loc_QUOTE_COLON = { "\":", 2 };

Error
Operation::do_match_common()
{
    match.exec_match(doc_cur, path, jsn);
    if (match.matchres == JSONSL_MATCH_TYPE_MISMATCH) {
        return Error::PATH_MISMATCH;
    } else if (match.status != JSONSL_ERROR_SUCCESS) {
        if (match.status == JSONSL_ERROR_LEVELS_EXCEEDED) {
            return Error::DOC_ETOODEEP;
        } else {
            return Error::DOC_NOTJSON;
        }
    } else {
        return Error::SUCCESS;
    }
}

Error
Operation::do_get()
{
    if (match.matchres != JSONSL_MATCH_COMPLETE) {
        return Error::PATH_ENOENT;
    }
    return Error::SUCCESS;
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

    /* Now it's time to get creative */
    if (match.matchres != JSONSL_MATCH_COMPLETE) {
        switch (optype) {
        case Command::DICT_ADD_P:
        case Command::DICT_UPSERT_P:
            break;

        case Command::DICT_ADD:
        case Command::DICT_UPSERT:
            if (!match.immediate_parent_found) {
                return Error::PATH_ENOENT;
            }
            break;

        case Command::REMOVE:
        case Command::REPLACE:
            /* etc. */
        default:
            return Error::PATH_ENOENT;
        }
    } else if (match.matchres == JSONSL_MATCH_COMPLETE) {
        if (optype.base() == Command::DICT_ADD) {
            return Error::DOC_EEXISTS;
        }
    }

    if (optype == Command::REMOVE) {
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
        auto& comp = path->back();
        doc_new[2].at = comp.pstr;
        doc_new[2].length = comp.len;

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
    return Error::SUCCESS;
}

Error
Operation::do_mkdir_p(int mode)
{
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
    const Path::Component* comp = &path->get_component(match.match_level);
    bkbuf += '"';
    bkbuf.append(comp->pstr, comp->len);
    bkbuf += "\":";

    /* The next set of components must be created as entries within the
     * newly created key */
    for (ii = match.match_level + 1; ii < path->size(); ii++) {
        comp = &path->get_component(ii);
        if (comp->ptype != JSONSL_PATH_STRING) {
            return Error::PATH_ENOENT;
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

    for (ii = match.match_level+1; ii < path->size(); ii++) {
        bkbuf += '}';
    }
    doc_new[3].length = bkbuf.size() - doc_new[1].length;

    /* Set the buffers */
    doc_new[1].at = bkbuf.data();
    doc_new[3].at = bkbuf.data() + doc_new[1].length;
    doc_new[2] = user_in;

    doc_new[4].begin_at_end(doc_cur, match.loc_parent, Loc::OVERLAP);
    doc_new_len = 5;

    return Error::SUCCESS;
}

Error
Operation::find_first_element()
{
    jsonsl_error_t rv = path->add_array_index(0);
    if (rv != JSONSL_ERROR_SUCCESS) {
        return Error::PATH_E2BIG;
    }

    Error status = do_match_common();
    path->pop();

    if (status != Error::SUCCESS) {
        return status;
    }
    if (match.matchres != JSONSL_MATCH_COMPLETE) {
        return Error::PATH_ENOENT;
    }
    return Error::SUCCESS;
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
    if (rv != Error::SUCCESS) {
        return rv;
    }
    if (match.num_siblings == 0) {
        /* first is last */
        return Error::SUCCESS;
    }

    mloc->at = doc_cur.at + match.loc_key.length;
    /* Length of the last child is the difference between the child's
     * start position, and the parent's end position */
    mloc->length = (ploc->at + ploc->length) - mloc->at;
    /* Exclude the parent's token */
    mloc->length--;

    /* Finally, set the position */
    match.position = match.num_siblings;

    return Error::SUCCESS;
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
    return Error::SUCCESS;
}


Error
Operation::do_list_op()
{
    #define HANDLE_LISTADD_ENOENT(rv) \
    if (rv == Error::PATH_ENOENT) { \
        if (match.immediate_parent_found) { \
            return insert_singleton_element(); \
        } else { \
            return rv; \
        } \
    }

    #define HANDLE_LISTADD_ENOENT_P(rv) \
    if (rv == Error::PATH_ENOENT) { \
        if (match.immediate_parent_found) { \
            return insert_singleton_element(); \
        } else { \
            return do_mkdir_p(MKDIR_P_ARRAY); \
        } \
    }

    Error rv;
    if (optype == Command::ARRAY_PREPEND) {
        /* Find the array itself. */
        rv = find_first_element();

        HANDLE_LISTADD_ENOENT(rv);
        if (rv != Error::SUCCESS) {
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
        return Error::SUCCESS;

    } else if (optype == Command::ARRAY_APPEND) {
        rv = find_last_element();

        HANDLE_LISTADD_ENOENT(rv);
        if (rv != Error::SUCCESS) {
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
        return Error::SUCCESS;

    } else if (optype == Command::ARRAY_PREPEND_P) {
        rv = find_first_element();
        if (rv == Error::SUCCESS) {
            goto GT_PREPEND_FOUND;
        }

        GT_ARR_P_COMMON:
        HANDLE_LISTADD_ENOENT_P(rv);
        return rv;

    } else if (optype == Command::ARRAY_APPEND_P) {
        rv = find_last_element();
        if (rv == Error::SUCCESS) {
            goto GT_APPEND_FOUND;
        }
        goto GT_ARR_P_COMMON;

    } else if (optype == Command::ARRAY_ADD_UNIQUE) {
        match.ensure_unique = user_in;
        rv = find_first_element();
        HANDLE_LISTADD_ENOENT(rv);

        GT_ADD_UNIQUE:
        if (rv != Error::SUCCESS) {
            /* mismatch, perhaps? */
            return rv;
        }
        if (match.unique_item_found) {
            return Error::DOC_EEXISTS;
        }
        goto GT_PREPEND_FOUND;

    } else if (optype == Command::ARRAY_ADD_UNIQUE_P) {
        match.ensure_unique = user_in;
        rv = find_first_element();
        HANDLE_LISTADD_ENOENT_P(rv);
        goto GT_ADD_UNIQUE;
    }

    return Error::SUCCESS;
}

Error
Operation::do_insert()
{
    auto& lastcomp = path->get_component(path->size()-1);
    if (!lastcomp.is_arridx) {
        return Error::PATH_MISMATCH;
    }

    Error status = do_match_common();
    if (!status.success()) {
        return status;
    }

    if (match.matchres == JSONSL_MATCH_COMPLETE) {
        /*
         * DOCNEW[0] = ... [
         * DOCNEW[1] = USER
         * DOCNEW[2] = ,
         * DOCNEW[3] = MATCH
         */
        doc_new_len = 4;
        doc_new[0].end_at_begin(doc_cur, match.loc_match, Loc::NO_OVERLAP);
        doc_new[1] = user_in;
        doc_new[2] = loc_COMMA;
        doc_new[3].begin_at_begin(doc_cur, match.loc_match);
        return Error::SUCCESS;

    } else if (match.immediate_parent_found) {
        // Get the array index.
        auto lastix = lastcomp.idx;

        if (match.num_siblings == 0 && (lastcomp.is_neg || lastix == 0)) {
            // Singleton element and requested insertion to one of the "Ends"
            // of the array
            /*
             * DOCNEW[0] = ... [
             * DOCNEW[1] = USER
             * DOCNEW[2] = ] ...
             */
            doc_new_len = 3;
            doc_new[0].end_at_begin(doc_cur, match.loc_parent, Loc::OVERLAP);
            doc_new[1] = user_in;
            doc_new[2].begin_at_end(doc_cur, match.loc_parent, Loc::OVERLAP);
            return Error::SUCCESS;

        } else if (lastix == match.num_siblings) {
            /*
             * (assume DOC = [a,b,c,d,e]
             * DOCNEW[0] = e (last char before ']')
             * DOCNEW[1] = , (since there are items in the list)
             * DOCNEW[2] = USER
             * DOCNEW[3] = ]
             */
            doc_new_len = 4;
            doc_new[0].end_at_end(doc_cur, match.loc_parent, Loc::NO_OVERLAP);
            doc_new[1] = loc_COMMA;
            doc_new[2] = user_in;
            doc_new[3].begin_at_end(doc_cur, match.loc_parent, Loc::OVERLAP);
            return Error::SUCCESS;

        } else {
            return Error::PATH_ENOENT;
        }
    } else {
        return Error::PATH_ENOENT;
    }
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
        return Error::GLOBAL_EINVAL;
    }

    memcpy(&tmp, user_in.at, 8);
    tmp = ntohll(tmp);
    if (tmp > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
        return Error::DELTA_E2BIG;
    }

    delta = tmp;
    if (optype.base() == Command::DECREMENT) {
        delta *= -1;
    }

    /* Find the number first */
    status = do_match_common();
    if (status != Error::SUCCESS) {
        return status;
    }

    if (match.matchres == JSONSL_MATCH_COMPLETE) {
        if (match.type != JSONSL_T_SPECIAL) {
            return Error::PATH_MISMATCH;
        } else if (match.sflags & ~(JSONSL_SPECIALf_NUMERIC)) {
            return Error::PATH_MISMATCH;
        } else  {
            num_i = strtoll(match.loc_match.at, NULL, 10);
            if (num_i == std::numeric_limits<int64_t>::max() && errno == ERANGE) {
                return Error::NUM_E2BIG;
            }

            /* Calculate what to place inside the buffer. We want to be gentle here
             * and not force 64 bit C arithmetic to confuse users, so use proper
             * integer overflow/underflow with a 64 (or rather, 63) bit limit. */
            if (delta >= 0 && num_i >= 0) {
                if (std::numeric_limits<int64_t>::max() - delta <= num_i) {
                    return Error::DELTA_E2BIG;
                }
            } else if (delta < 0 && num_i < 0) {
                if (delta <= std::numeric_limits<int64_t>::min() - num_i) {
                    return Error::DELTA_E2BIG;
                }
            }

            num_i += delta;
            numbuf = std::to_string(num_i);
        }
    } else {
        if (!optype.is_mkdir_p() && !match.immediate_parent_found) {
            return Error::PATH_ENOENT;
        }

        if (match.type != JSONSL_T_OBJECT) {
            return Error::PATH_ENOENT;
        }

        numbuf = std::to_string(delta);
        user_in.at = numbuf.data();
        user_in.length = numbuf.size();
        optype = Command::DICT_ADD_P;
        if ((status = do_store_dict()) != Error::SUCCESS) {
            return status;
        }
        match.loc_match = user_in;
        return Error::SUCCESS;
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
    return Error::SUCCESS;
}

Error
Operation::validate(int mode, int depth)
{
    if (!user_in.empty()) {
        int rv = Validator::validate(user_in, jsn, depth, mode);
        switch (rv) {
        case JSONSL_ERROR_SUCCESS:
            return Error::SUCCESS;
        case JSONSL_ERROR_LEVELS_EXCEEDED:
            return Error::VALUE_ETOODEEP;
        default:
            return Error::VALUE_CANTINSERT;
        }
    } else {
        return Error::VALUE_EMPTY;
    }
}

int
Operation::get_maxdepth(DepthMode mode) const
{
    if (mode == DepthMode::PATH_HAS_NEWKEY) {
        return (Limits::MAX_COMPONENTS + 1) - path->size();
    } else {
        return Limits::MAX_COMPONENTS - path->size();
    }
}

Error
Operation::op_exec(const char *pth, size_t npth)
{
    int rv = path->parse(pth, npth);
    Error status;

    if (rv != 0) {
        if (rv == JSONSL_ERROR_LEVELS_EXCEEDED) {
            return Error::PATH_E2BIG;
        } else {
            return Error::PATH_EINVAL;
        }
    }

    switch (optype) {
    case Command::GET:
    case Command::EXISTS:
        status = do_match_common();
        if (status != Error::SUCCESS) {
            return status;
        }
        return do_get();

    case Command::DICT_ADD:
    case Command::DICT_ADD_P:
    case Command::DICT_UPSERT:
    case Command::DICT_UPSERT_P:
    case Command::REPLACE:
    case Command::REMOVE:
        if (path->size() == 1) {
            /* Can't perform these operations on the root element since they
             * will invalidate the JSON or are otherwise meaningless. */
            return Error::VALUE_CANTINSERT;
        }

        if (optype != Command::REMOVE) {
            status = validate(Validator::PARENT_DICT, get_maxdepth(PATH_HAS_NEWKEY));
            if (!status.success()) {
                return status;
            }
        }
        status = do_match_common();
        if (status != Error::SUCCESS) {
            return status;
        }
        return do_store_dict();
    case Command::ARRAY_APPEND:
    case Command::ARRAY_PREPEND:
    case Command::ARRAY_APPEND_P:
    case Command::ARRAY_PREPEND_P:
    case Command::ARRAY_ADD_UNIQUE:
    case Command::ARRAY_ADD_UNIQUE_P:
        status = validate(Validator::PARENT_ARRAY, get_maxdepth(PATH_IS_PARENT));
        if (!status.success()) {
            return status;
        }
        return do_list_op();

    case Command::ARRAY_INSERT:
        status = validate(Validator::PARENT_ARRAY, get_maxdepth(PATH_HAS_NEWKEY));
        if (!status.success()) {
            return status;
        }
        return do_insert();

    case Command::INCREMENT:
    case Command::INCREMENT_P:
    case Command::DECREMENT:
    case Command::DECREMENT_P:
        // no need to check for depth here, since if the path itself is too
        // big, it will fail during path parse-time
        return do_arith_op();

    default:
        return Error::GLOBAL_ENOSUPPORT;

    }
}

Operation::Operation()
: path(new Path()),
  jsn(Match::jsn_alloc()),
  optype(Command::GET),
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
    optype = Command::GET;
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
Error::description() const
{
    switch (m_code) {
    case Error::SUCCESS:
        return "Success";
    case Error::PATH_ENOENT:
        return "Requested path does not exist in document";
    case Error::PATH_MISMATCH:
        return "The path specified treats an existing document entry as the wrong type";
    case Error::PATH_EINVAL:
        return "Path syntax error";
    case Error::DOC_NOTJSON:
        return "The document is not JSON";
    case Error::DOC_EEXISTS:
        return "The requested path already exists";
    case Error::PATH_E2BIG:
        return "The path is too big";
    case Error::NUM_E2BIG:
        return "The number specified by the path is too big";
    case Error::DELTA_E2BIG:
        return "The combination of the existing number and the delta will result in an underflow or overflow";
    case Error::VALUE_CANTINSERT:
        return "The new value cannot be inserted in the context of the path, as it would invalidate the JSON";
    case Error::VALUE_EMPTY:
        return "Expected non-empty value for command";
    case Error::VALUE_ETOODEEP:
        return "Adding this value would make the document too deep";
    case Error::GLOBAL_ENOMEM:
        return "Couldn't allocate memory";
    case Error::GLOBAL_ENOSUPPORT:
        return "Operation not implemented";
    case Error::GLOBAL_UNKNOWN_COMMAND:
        return "Unrecognized command code";
    default:
        return "Unknown error code";
    }
}
