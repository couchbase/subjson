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

/* This file does the brunt of iterating through a buffer and determining the
 * offset where a given path may begin or end.. */

#define INCLUDE_JSONSL_SRC
#include "jsonsl_header.h"
#include "subdoc-api.h"
#include "match.h"
#include "uescape.h"
#include "validate.h"

using namespace Subdoc;

namespace {
struct ParseContext {
    ParseContext(Match *match, Path::CompInfo *jpr) : jpr(jpr), match(match){
    }

    Path::CompInfo* jpr;

    // Internal pointer/length pair used to hold the pointer of either the
    // current key, or the unique array value (if in "unique" mode).
    // These are actually accessed by get_hk()
    const char *hkbuf_ = NULL;
    unsigned hklen_ = 0;

    // Internal flag set to true if the current key is actually found in
    // 'hkstr' (in which case `hkstr` contains the properly unescaped version
    // of the key).
    bool hkesc = false;

    Match *match;

    // Contains the unescaped key (if the original contained u-escapes)
    std::string hkstr;

    void set_unique_begin(const jsonsl_state_st *, const jsonsl_char_t *at) {
        hkbuf_ = at;
    }

    const char *get_unique() const { return hkbuf_; }

    void set_hk_begin(const jsonsl_state_st *, const jsonsl_char_t *at) {
        hkbuf_ = at + 1;
    }

    void set_hk_end(const jsonsl_state_st *state) {
        hklen_ = state->pos_cur - (state->pos_begin + 1);

        if (!state->nescapes) {
            hkesc = false;
        } else {
            hkstr.clear();
            hkesc = true;
            UescapeConverter::convert(hkbuf_, hklen_, hkstr);
        }
    }
    const char *get_hk(size_t& nkey) const {
        if (!hkesc) {
            nkey = hklen_;
            return hkbuf_;
        } else {
            nkey = hkstr.size();
            return hkstr.c_str();
        }
    }
    void set_hk_loc(Loc& loc) {
        loc.at = hkbuf_ - 1;
        loc.length = hklen_ + 2;
    }
};
}

static void push_callback(jsonsl_t jsn,jsonsl_action_t, struct jsonsl_state_st *, const jsonsl_char_t *);
static void pop_callback(jsonsl_t jsn,jsonsl_action_t, struct jsonsl_state_st *, const jsonsl_char_t *);

static ParseContext *get_ctx(const jsonsl_t jsn)
{
    return (ParseContext *)jsn->data;
}

static int
err_callback(jsonsl_t jsn, jsonsl_error_t err, struct jsonsl_state_st *state,
    jsonsl_char_t *at)
{
    ParseContext *ctx = get_ctx(jsn);
    ctx->match->status = err;
    (void)state; (void)at;
    return 0;
}

// Updates the state to reflect information on the parent. This is used by
// the subdoc code for insertion/deletion operations.
static void
update_possible(ParseContext *ctx, const struct jsonsl_state_st *state, const char *at)
{
    ctx->match->loc_parent.at = at;
    ctx->match->match_level = state->level;
}

static void
unique_callback(jsonsl_t jsn, jsonsl_action_t action, struct jsonsl_state_st *st,
    const jsonsl_char_t *at)
{
    ParseContext *ctx = get_ctx(jsn);
    Match *m = ctx->match;
    int rv;
    size_t slen;

    if (action == JSONSL_ACTION_PUSH) {
        /* abs. beginning of token */
        ctx->set_unique_begin(st, at);
        return;
    }

    if (st->level == jsn->max_callback_level-2) {
        /* Popping the parent state! */
        jsn->action_callback_POP = pop_callback;
        jsn->action_callback_PUSH = push_callback;
        pop_callback(jsn, action, st, at);
        return;
    }

    slen = st->pos_cur - st->pos_begin;

    if (st->type == JSONSL_T_STRING) {
        slen++;

        if (m->ensure_unique.length < 2) {
            /* not a string */
            return;
        } else if (slen != m->ensure_unique.length) {
            return; /* Length mismatch */
        }

        rv = strncmp(ctx->get_unique() + 1, m->ensure_unique.at + 1, slen-2);

    } else if (st->type == JSONSL_T_SPECIAL) {
        if (m->ensure_unique.length != slen) {
            return;
        } else if (slen != m->ensure_unique.length) {
            return;
        }

        rv = strncmp(ctx->get_unique(), m->ensure_unique.at, slen);

    } else {
        /* We can't reliably indicate uniqueness for non-primitives */
        m->matchres = JSONSL_MATCH_TYPE_MISMATCH;
        rv = 0;
    }

    if (rv == 0) {
        m->unique_item_found = 1;
        jsn->max_callback_level = 1;
    }
}

/* Make code a bit more readable */
#define M_POSSIBLE JSONSL_MATCH_POSSIBLE
#define M_NOMATCH JSONSL_MATCH_NOMATCH
#define M_COMPLETE JSONSL_MATCH_COMPLETE
#define IS_CONTAINER JSONSL_STATE_IS_CONTAINER

static void
push_callback(jsonsl_t jsn, jsonsl_action_t action, struct jsonsl_state_st *st,
    const jsonsl_char_t *at)
{
    ParseContext *ctx = get_ctx(jsn);
    Match *m = ctx->match;
    struct jsonsl_state_st *parent = jsonsl_last_state(jsn, st);
    unsigned prtype = parent->type;

    if (st->type == JSONSL_T_HKEY) {
        ctx->set_hk_begin(st, at);
        return;
    }

    // If the parent is a match candidate
    if (parent->mres == M_POSSIBLE) {
        // Key is either the array offset or the dictionary key.
        size_t nkey;
        const char *key;
        if (prtype == JSONSL_T_OBJECT) {
            key = ctx->get_hk(nkey);
        } else {
            nkey = static_cast<size_t>(parent->nelem - 1);
            key = NULL;
        }

        /* Run the match */
        st->mres = jsonsl_jpr_match(ctx->jpr, prtype, parent->level, key, nkey);

        // jsonsl's jpr is a bit laxer here. but in our case
        // it's impossible for a primitive to result in a "possible" result,
        // since a partial result should always mean a container
        if (st->mres == M_POSSIBLE && IS_CONTAINER(st) == 0) {
            st->mres = JSONSL_MATCH_TYPE_MISMATCH;
        }

        if (st->mres == JSONSL_MATCH_COMPLETE) {
            m->matchres = JSONSL_MATCH_COMPLETE;

            m->loc_match.at = at;
            m->match_level = st->level;
            m->type = st->type;

            if (prtype == JSONSL_T_OBJECT) {
                m->has_key = true;

                // The beginning of the key (for "subdoc" purposes) actually
                // _includes_ the opening and closing quotes
                ctx->set_hk_loc(m->loc_key);

                // I'm not sure if it's used.
                m->position = static_cast<unsigned>((parent->nelem - 1) / 2);
            } else {

                // Array doesn't have a key
                m->has_key = 0;
                // array[n]
                m->position = static_cast<unsigned>(parent->nelem - 1);
            }

            if (m->ensure_unique.at) {
                if (prtype != JSONSL_T_LIST) {
                    // Can't check "uniquness" in an array!
                    m->matchres = JSONSL_MATCH_TYPE_MISMATCH;
                    st->ignore_callback = 1;
                } else {
                    jsn->action_callback_POP = unique_callback;
                    jsn->action_callback_PUSH = unique_callback;
                    unique_callback(jsn, action, st, at);
                }
            }

        } else if (st->mres == JSONSL_MATCH_NOMATCH) {
            // Can't have a match on this tree. Ignore subsequent callbacks here.
            st->ignore_callback = 1;

        } else if (st->mres == JSONSL_MATCH_POSSIBLE) {
            // Update our depth thus far
            update_possible(ctx, st, at);
        } else if (st->mres == JSONSL_MATCH_TYPE_MISMATCH) {
            st->ignore_callback = 1;
            m->matchres = JSONSL_MATCH_TYPE_MISMATCH;
        }
    }
    (void)action; /* always push */
}

/* This callback serves three purposes:
 * 1. It captures the hash key
 * 2. It captures the length of the match
 * 3. It captures the length of the deepest parent
 */
static void
pop_callback(jsonsl_t jsn, jsonsl_action_t, struct jsonsl_state_st *state,
    const jsonsl_char_t *)
{
    ParseContext *ctx = get_ctx(jsn);
    Match *m = ctx->match;
    size_t end_pos = jsn->pos;

    if (state->type == JSONSL_T_HKEY) {
        // All we care about is recording the length of the key. We'll use
        // this later on when matching (in the PUSH callback of a new element)
        ctx->set_hk_end(state);
        return;
    }

    if (state->mres == JSONSL_MATCH_COMPLETE) {
        // This is the matched element. Record the end location of the
        // match.
        m->loc_match.length = end_pos - state->pos_begin;
        if (state->type != JSONSL_T_SPECIAL) {
            m->loc_match.length++; /* Include the terminating token */
        } else {
            m->sflags = state->special_flags;
        }

        // Don't care about more data
        jsn->max_callback_level = state->level;
        return;
    }

    // Not a container and not a full match. We can't do anything here.
    if (!JSONSL_STATE_IS_CONTAINER(state)) {
        return;
    }

    // We already recorded the deepest-most parent.
    if (m->loc_parent.length) {
        return;
    }

    // This isn't part of the match tree, so we just ignore it
    if (state->mres != JSONSL_MATCH_POSSIBLE) {
        return;
    }

    // If we haven't bailed out yet, it means this is the deepest most parent
    // match. This can either be the actual parent of the match (if a match
    // is found), or the deepest existing parent which exists in the document.
    // The latter information is needed for MKDIR_P semantics.

    // Record the length of the parent
    m->loc_parent.length = end_pos - state->pos_begin;
    m->loc_parent.length++;

    // Record the number of "siblings" in the container. This is used by
    // insertion/removal operations to determine comma placement, if any,
    // before or after the current match.
    if (state->type == JSONSL_T_OBJECT) {
        m->num_siblings = static_cast<unsigned>(state->nelem / 2);
    } else {
        m->num_siblings = static_cast<unsigned>(state->nelem);

        // For array-based APPEND operations we need the position of the
        // last child (which may not exist if the base array is empty).
        if (m->num_siblings && m->get_last_child_pos) {
            /* Set the last child begin position */
            const struct jsonsl_state_st *child = jsonsl_last_child(jsn, state);

            // Special semantics for this field when using get_last_child_pos.
            // See field documentation in header.
            m->loc_key.length = child->pos_begin;
            m->loc_key.at = NULL;

            // Set information about the last child itself.
            m->type = child->type;
            m->sflags = child->special_flags;
        }
    }
    if (m->matchres == JSONSL_MATCH_COMPLETE) {
        /* Exclude ourselves from the 'sibling' count */
        m->num_siblings--;
    }

    // If the match was not found, we need to do some additional sanity
    // checking (usually done inside the push/pop callback in the case
    // where a match _is_ found) to verify our would-be path can actually
    // make sense.
    if (m->matchres != JSONSL_MATCH_COMPLETE) {
        m->type = state->type;

        struct jsonsl_jpr_component_st *next_comp =
                &ctx->jpr->components[state->level];
        if (next_comp->is_arridx) {
            if (state->type != JSONSL_T_LIST) {
                /* printf("Next component expected list, but we are object\n"); */
                m->matchres = JSONSL_MATCH_TYPE_MISMATCH;
            }
        } else {
            if (state->type != JSONSL_T_OBJECT) {
                /* printf("Next component expected object key, but we are list!\n"); */
                m->matchres = JSONSL_MATCH_TYPE_MISMATCH;
            }
        }
    }

    jsn->max_callback_level = 1;
    jsonsl_stop(jsn);

    // Is this the actual parent of the match?
    if (state->level == ctx->jpr->ncomponents-1) {
        m->immediate_parent_found = 1;
    }
}

/* Called when */
static void initial_callback(jsonsl_t jsn, jsonsl_action_t action,
    struct jsonsl_state_st *state, const jsonsl_char_t *at)
{
    ParseContext *ctx = get_ctx(jsn);
    /* state is the parent */
    state->mres = jsonsl_jpr_match(ctx->jpr, JSONSL_T_UNKNOWN, 0, NULL, 0);
    jsn->action_callback_PUSH = push_callback;

    if (state->mres == JSONSL_MATCH_POSSIBLE) {
        update_possible(ctx, state, at);
        ctx->match->matchres = JSONSL_MATCH_POSSIBLE;
    } else if (state->mres == JSONSL_MATCH_COMPLETE) {
        /* Match the root element. Simple */
        ctx->match->match_level = state->level;
        ctx->match->has_key = 0;
        ctx->match->loc_match.at = at;
        ctx->match->loc_parent.at = at;
        ctx->match->matchres = JSONSL_MATCH_COMPLETE;
    } else {
        state->ignore_callback = 1;
    }

    (void)action; /* always push */
}

int
Match::exec_match_simple(const char *value, size_t nvalue,
    const Path::CompInfo *jpr, jsonsl_t jsn)
{
    ParseContext ctx(this, const_cast<Path::CompInfo*>(jpr));
    status = JSONSL_ERROR_SUCCESS;

    jsonsl_enable_all_callbacks(jsn);
    jsn->action_callback_PUSH = initial_callback;
    jsn->action_callback_POP = pop_callback;
    jsn->error_callback = err_callback;
    jsn->max_callback_level = ctx.jpr->ncomponents + 1;
    jsn->data = &ctx;

    jsonsl_feed(jsn, value, nvalue);
    jsonsl_reset(jsn);
    return 0;
}

int
Match::exec_match_negix(const char *value, size_t nvalue, const Path *pth,
    jsonsl_t jsn)
{
    /* First component to scan in next iteration */
    size_t cur_start = 1;

    /* For levels, compensate this much for the match level */
    size_t level_offset = 0;

    const Path::CompInfo& orig = *pth;
    Path::Component comp_s[Limits::PATH_COMPONENTS_ALLOC];

    /* Last length of the subdocument */
    size_t last_len = nvalue;
    /* Pointer to the beginning of the current subdocument */
    const char *last_start = value;
    std::copy(orig.begin(), orig.end(), comp_s);

    while (cur_start < orig.size()) {
        size_t ii;
        int rv, is_last_neg = 0;

        /* If the last match was not a list or an object,
         * but we still need to descend, then throw an error now. */
        if (last_start != value && type != JSONSL_T_LIST && type != JSONSL_T_OBJECT) {
            matchres = JSONSL_MATCH_TYPE_MISMATCH;
            return 0;
        }

        for (ii = cur_start; ii < orig.size(); ii++) {
            /* Seek to the next negative index */
            if (orig[ii].is_neg) {
                /* Convert this to the first array index; then switch it
                 * around to extract parent information */
                is_last_neg = 1;
                break;
            }
        }
        Path::CompInfo tmp(
            /* Assign the new list. Use one before for the ROOT element */
            &comp_s[cur_start-1],
            /* Adjust for root again */
            (ii - cur_start) + 1);

        level_offset += (ii - cur_start);

        tmp[0].ptype = JSONSL_PATH_ROOT;

        /* Clear the match. There's no good way to preserve info here,
         * unfortunately. */
        clear();

        /* Always set this */
        get_last_child_pos = 1;


        /* If we need to find the _last_ item, make use of the get_last_child_pos
         * field. To use this effectively, search for the _first_ element (to
         * guarantee a valid parent). Then, once the match is done, the last
         * item can be derived (see below). */
        if (is_last_neg) {
            Path::Component& comp = tmp.add(JSONSL_PATH_NUMERIC);
            comp.is_arridx = 1;
            comp.idx = 0;
        }

        rv = exec_match_simple(last_start, last_len, &tmp, jsn);
        match_level += level_offset;

        if (rv != 0) { /* error */
            return rv;
        } else if (status != JSONSL_ERROR_SUCCESS) {
            return 0;
        } else if (matchres != JSONSL_MATCH_COMPLETE) {
            return 0;
        }

        /* No errors so far. In this case, set last_start */
        if (is_last_neg) {
            /* Offset into last child position */
            loc_match.at = last_start + loc_key.length;

            /* Set the length. The length is at least as long as the parent */
            loc_match.length = (loc_parent.at + loc_parent.length) - loc_match.at;
            loc_match.length--;

            /* Strip trailing whitespace. Leading whitespace is stripped
             * within jsonsl itself */
            while (loc_match.at[loc_match.length-1] == ' ' && loc_match.length) {
                loc_match.length--;
            }

            /* Set the position */
            position = num_siblings;

            last_start = loc_match.at;
            last_len = loc_match.length;
        }

        /* Chomp off the current component */
        cur_start = ii + 1;
    }

    return 0;
}

/**
 * Retrieves a buffer for the designated _path_ in the JSON document. The
 * actual length of the remaining components may be determined by using
 * pointer arithmetic.
 *
 * @param value The value buffer to search
 * @param nvalue Length of the value buffer
 * @param nj The path component to search for
 * @param[out] a result structure for the path
 * @return 0 if found, otherwise an error code
 */
int
Match::exec_match(const char *value, size_t nvalue, const Path *pth, jsonsl_t jsn)
{
    if (!pth->has_negix) {
        return exec_match_simple(value, nvalue, pth, jsn);
    } else {
        return exec_match_negix(value, nvalue, pth, jsn);
    }
}

Match::Match()
{
    clear();
}

Match::~Match()
{
}

void
Match::clear()
{
    // TODO: Memberwise reset
    memset(this, 0, sizeof(*this));
}

jsonsl_t
Match::jsn_alloc()
{
    return jsonsl_new(Limits::PARSER_DEPTH);
}

void
Match::jsn_free(jsonsl_t jsn)
{
    jsonsl_destroy(jsn);
}

typedef struct {
    int err;
    int rootcount;
    int flags;
    int maxdepth;
} validate_ctx;

static int
validate_err_callback(jsonsl_t jsn,
    jsonsl_error_t err, jsonsl_state_st *, jsonsl_char_t *)
{
    validate_ctx *ctx = (validate_ctx *)jsn->data;
    ctx->err = err;
    // printf("ERROR: AT=%s\n", at);
    return 0;
}

static void
validate_callback(jsonsl_t jsn, jsonsl_action_t action,
    jsonsl_state_st *state, const jsonsl_char_t *)
{
    validate_ctx *ctx = reinterpret_cast<validate_ctx*>(jsn->data);

    if (ctx->maxdepth > -1 &&
            state->level - 1 == static_cast<unsigned>(ctx->maxdepth)) {
        ctx->err = Validator::ETOODEEP;
        jsonsl_stop(jsn);
        return;
    }

    if (state->level == 1) {
        // Root element
        ctx->rootcount++;

    } else if (state->level == 2 && action == JSONSL_ACTION_PUSH) {
        if (action != JSONSL_ACTION_PUSH) {
            return;
        }
        if (ctx->flags & Validator::VALUE_PRIMITIVE) {
            if (JSONSL_STATE_IS_CONTAINER(state)) {
                ctx->err = Validator::ENOTPRIMITIVE;
                jsonsl_stop(jsn);
            }
        }

        if (ctx->flags & Validator::VALUE_SINGLE) {
            auto *parent = jsonsl_last_state(jsn, state);
            int has_multielem = 0;
            if (parent->type == JSONSL_T_LIST && parent->nelem > 1) {
                has_multielem = 1;
            } else if (parent->type == JSONSL_T_OBJECT && parent->nelem > 2) {
                has_multielem = 1;
            }

            if (has_multielem) {
                ctx->err = Validator::EMULTIELEM;
                jsonsl_stop(jsn);
            }
        }
    }
}

static const Loc validate_ARRAY_PRE = { "[", 1 };
static const Loc validate_ARRAY_POST = { "]", 1 };
static const Loc validate_DICT_PRE = { "{\"k\":", 5 };
static const Loc validate_DICT_POST = { "}", 1 };
static const Loc validate_NOOP = { NULL, 0 };

int
Validator::validate(const char *s, size_t n, jsonsl_t jsn, int maxdepth, int mode)
{
    int need_free_jsn = 0;
    int type = mode & PARENT_MASK;
    int flags = mode & VALUE_MASK;
    const Loc *l_pre, *l_post;

    validate_ctx ctx = { 0,0 };
    if (jsn == NULL) {
        jsn = jsonsl_new(Limits::PARSER_DEPTH);
        need_free_jsn = 1;
    }

    jsn->action_callback_POP = NULL;
    jsn->action_callback_PUSH = NULL;

    jsn->action_callback = validate_callback;
    jsn->error_callback = validate_err_callback;

    // Set the maximum level. This name is a bit misleading as it's
    // actually the level number (inclusive) beyond which we won't get
    // called. Cutting down on the number of callbacks can significantly
    // benefit CPU, especially for larger JSON documents with many tokens.
    if (maxdepth != -1) {
        if (type != PARENT_NONE) {
            // Don't count the wrapper in the depth
            maxdepth++;
        }
        // Set the callback to be 1 more (i.e. max_level+2) so we can
        // get notified about depth violations
        jsn->max_callback_level = maxdepth + 2;
    } else if (flags) {
        // Set the callback to include the "wrapper" container and our
        // actual value
        jsn->max_callback_level = 3;
    } else {
        // only care about the actual value.
        jsn->max_callback_level = 2;
    }

    ctx.maxdepth = maxdepth;
    ctx.flags = flags;

    jsn->call_OBJECT = 1;
    jsn->call_LIST = 1;
    jsn->call_STRING = 1;
    jsn->call_SPECIAL = 1;
    jsn->call_HKEY = 0;
    jsn->call_UESCAPE = 0;
    jsn->data = &ctx;

    if (type == PARENT_NONE) {
        l_pre = l_post = &validate_NOOP;
    } else if (type == PARENT_ARRAY) {
        l_pre = &validate_ARRAY_PRE;
        l_post = &validate_ARRAY_POST;
    } else if (type == PARENT_DICT) {
        l_pre = &validate_DICT_PRE;
        l_post = &validate_DICT_POST;
    } else {
        ctx.err = JSONSL_ERROR_GENERIC;
        goto GT_ERR;
    }

    jsonsl_feed(jsn, l_pre->at, l_pre->length);
    jsonsl_feed(jsn, s, n);

    if (ctx.err == JSONSL_ERROR_SUCCESS) {
        jsonsl_feed(jsn, l_post->at, l_post->length);
    }

    if (ctx.err == JSONSL_ERROR_SUCCESS) {
        if (ctx.rootcount < 2) {
            ctx.err = Validator::EPARTIAL;
        } else if (ctx.rootcount > 2) {
            ctx.err = Validator::EMULTIELEM;
        }
    }

    GT_ERR:
    if (need_free_jsn) {
        jsonsl_destroy(jsn);
    } else {
        jsonsl_reset(jsn);
    }
    return ctx.err;
}

const char *
Validator::errstr(int rv)
{
    if (rv <= JSONSL_ERROR_GENERIC) {
        return jsonsl_strerror(static_cast<jsonsl_error_t>(rv));
    }
    switch (rv) {
    case EMULTIELEM:
        return "Found multiple elements (single expected)";
    case EPARTIAL:
        return "Found incomplete JSON";
    default:
        return "UNKNOWN";
    }
}
