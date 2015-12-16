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
#include "hkesc.h"
#include "validate.h"
#include "util.h"

using namespace Subdoc;

namespace {
struct ParseContext : public HashKey {
    ParseContext(Match *match, Path::CompInfo *jpr) : jpr(jpr), match(match){
    }

    Path::CompInfo* jpr;

    Match *match;

    // Internal pointer/length pair used to hold the pointer of the
    // unique array value (if in "unique" mode).
    const char *uniquebuf = NULL;

    void set_unique_begin(const jsonsl_state_st *, const jsonsl_char_t *at) {
        uniquebuf = at;
    }

    const char *get_unique() const { return uniquebuf; }
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

    if (st->mres == JSONSL_MATCH_COMPLETE) {
        // Popping the parent level!
        jsn->action_callback_POP = pop_callback;
        jsn->action_callback_PUSH = push_callback;

        // Prevent descent into parser again
        jsn->max_callback_level = st->level+1;
        pop_callback(jsn, action, st, at);
        return;
    }

    slen = st->pos_cur - st->pos_begin;

    SUBDOC_ASSERT(st->level == m->match_level + 1U);

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
        jsonsl_stop(jsn);
        return;
    }

    if (rv == 0) {
        m->unique_item_found = 1;
        jsonsl_stop(jsn);
    }
}

/* Make code a bit more readable */
#define M_POSSIBLE JSONSL_MATCH_POSSIBLE

static void
push_callback(jsonsl_t jsn, jsonsl_action_t action, struct jsonsl_state_st *st,
    const jsonsl_char_t *at)
{
    ParseContext *ctx = get_ctx(jsn);
    Match *m = ctx->match;
    struct jsonsl_state_st *parent = jsonsl_last_state(jsn, st);

    if (st->type == JSONSL_T_HKEY) {
        ctx->set_hk_begin(st, at);
        return;
    }

    // If the parent is a match candidate
    if (parent == NULL || parent->mres == M_POSSIBLE) {
        unsigned prtype = parent ? parent->type : JSONSL_T_UNKNOWN;
        size_t nkey;
        const char *key;
        key = ctx->get_hk(nkey);

        /* Run the match */
        st->mres = jsonsl_path_match(ctx->jpr, parent, st, key, nkey);

        if (st->mres == JSONSL_MATCH_COMPLETE) {
            m->matchres = JSONSL_MATCH_COMPLETE;
            m->type = st->type;
            m->loc_deepest.at = at;
            m->match_level = st->level;

            if (prtype == JSONSL_T_OBJECT) {
                // The beginning of the key (for "subdoc" purposes) actually
                // _includes_ the opening and closing quotes
                ctx->hk_rawloc(m->loc_key);

                // I'm not sure if it's used.
                m->position = static_cast<unsigned>((parent->nelem - 1) / 2);
            } else if (prtype == JSONSL_T_LIST) {
                // array[n]
                m->position = static_cast<unsigned>(parent->nelem - 1);
            }

            if (m->ensure_unique.at) {
                if (st->type != JSONSL_T_LIST) {
                    // Can't check "uniquness" in an array!
                    m->matchres = JSONSL_MATCH_TYPE_MISMATCH;
                    jsonsl_stop(jsn);
                    return;
                } else {
                    // Set up callbacks for string comparison. This will
                    // be invoked for each push/pop combination.
                    jsn->action_callback_POP = unique_callback;
                    jsn->action_callback_PUSH = unique_callback;
                    jsn->max_callback_level = st->level + 2;
                }
            }

        } else if (st->mres == JSONSL_MATCH_NOMATCH) {
            // Can't have a match on this tree. Ignore subsequent callbacks here.
            st->ignore_callback = 1;

        } else if (st->mres == JSONSL_MATCH_POSSIBLE) {
            // Update our depth thus far
            m->match_level = st->level;
            m->loc_deepest.at = at;
        } else if (st->mres == JSONSL_MATCH_TYPE_MISMATCH) {
            st->ignore_callback = 1;
            m->matchres = JSONSL_MATCH_TYPE_MISMATCH;
        }
    }
    (void)action; /* always push */
}

/*
 * This callback is only invoked if GET_FOLLOWING_SIBLINGS is the option
 * mode.
 */
static void
next_sibling_push_callback(jsonsl_t jsn, jsonsl_action_t action,
    struct jsonsl_state_st *state, const jsonsl_char_t *at)
{
    const jsonsl_state_st *parent;
    ParseContext *ctx = get_ctx(jsn);
    Match *m = ctx->match;

    if (action == JSONSL_ACTION_POP) {
        // We can get a POP callback before a PUSH callback if the match
        // was the last child in the parent. In this case, we are being
        // invoked on a child, not a sibling.
        SUBDOC_ASSERT(state->level == m->match_level - 1U);
        parent = state;

    } else {
        parent = jsonsl_last_state(jsn, state);
    }

    SUBDOC_ASSERT(ctx->match->matchres == JSONSL_MATCH_COMPLETE);
    SUBDOC_ASSERT(ctx->match->extra_options == Match::GET_FOLLOWING_SIBLINGS);

    if (parent != NULL && parent->mres == JSONSL_MATCH_POSSIBLE) {
        /* Is the immediate parent! */
        if (parent->type == JSONSL_T_OBJECT) {
            unsigned nobj_elem = static_cast<unsigned>(parent->nelem);
            if (action == JSONSL_ACTION_PUSH) {
                SUBDOC_ASSERT(state->type == JSONSL_T_HKEY);
                nobj_elem++;
            }
            m->num_siblings = static_cast<unsigned>(nobj_elem / 2);

        } else if (parent->type == JSONSL_T_LIST) {
            m->num_siblings = static_cast<unsigned>(parent->nelem);
        }
        m->num_siblings--;
    }
    jsonsl_stop(jsn);
}

static void
pop_callback(jsonsl_t jsn, jsonsl_action_t, struct jsonsl_state_st *state,
    const jsonsl_char_t *)
{
    ParseContext *ctx = get_ctx(jsn);
    Match *m = ctx->match;

    if (state->type == JSONSL_T_HKEY) {
        // All we care about is recording the length of the key. We'll use
        // this later on when matching (in the PUSH callback of a new element)
        ctx->set_hk_end(state);
        return;
    }

    if (state->mres == JSONSL_MATCH_COMPLETE) {
        // This is the matched element. Record the end location of the
        // match.
        m->loc_deepest.length = jsn->pos - state->pos_begin;
        m->immediate_parent_found = 1;
        m->num_children = state->nelem;

        if (state->type != JSONSL_T_SPECIAL) {
            m->loc_deepest.length++; /* Include the terminating token */
        } else {
            m->sflags = state->special_flags;
        }

        if (m->get_last) {
            if (state->type != JSONSL_T_LIST) {
                // Not a list!
                m->matchres = JSONSL_MATCH_TYPE_MISMATCH;
                jsonsl_stop(jsn);
                return;

            } else if (!state->nelem) {
                // List is empty
                m->matchres = JSONSL_MATCH_UNKNOWN;
                jsonsl_stop(jsn);
                return;
            }

            // Transpose the match
            const jsonsl_state_st *child = jsonsl_last_child(jsn, state);

            // For containers, pos does not include the terminating token
            size_t child_endpos = jsn->pos;

            m->loc_deepest.length = child_endpos - child->pos_begin;
            m->loc_deepest.at = jsn->base + child->pos_begin;

            // Remove trailing whitespace. This is because the child is
            // deemed to end one character before the parent does, however
            // there may be whitespace. This is usually OK but confuses tests.
            while (isspace(m->loc_deepest.at[m->loc_deepest.length-1])) {
                --m->loc_deepest.length;
            }

            m->match_level = child->level;
            m->sflags = child->special_flags;
            m->type = child->type;
            m->num_children = child->nelem;

            // Parent is an array, get pos/sibling information directly from it
            m->num_siblings = state->nelem - 1;
            m->position = m->num_siblings;
            m->loc_key.clear();
            jsonsl_stop(jsn);

        } else {
            // Maybe we need another round to check for siblings?
            if (m->extra_options == Match::GET_FOLLOWING_SIBLINGS) {
                jsn->action_callback_POP = NULL;
                jsn->action_callback_PUSH = NULL;

                // We only care for this on push, but in the off chance where
                // this is the final element in the parent *anyway*, the parsing
                // itself should just stop.
                jsn->action_callback = next_sibling_push_callback;
            } else {
                jsonsl_stop(jsn);
            }
        }

        return;
    }

    // TODO: Determine if all these checks are necessary, since if everything
    // is correct, parsing should stop as soon as the deepest match is
    // terminated

    // Not a container and not a full match. We can't do anything here.
    if (!JSONSL_STATE_IS_CONTAINER(state)) {
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
    m->loc_deepest.length = jsn->pos - state->pos_begin;
    m->loc_deepest.length++; // Always a container. Use trailing token.

    // Record the number of "siblings" in the container. This is used by
    // insertion/removal operations to determine comma placement, if any,
    // before or after the current match.
    if (state->type == JSONSL_T_OBJECT) {
        m->num_siblings = static_cast<unsigned>(state->nelem / 2);
    } else {
        m->num_siblings = static_cast<unsigned>(state->nelem);
    }

    m->type = state->type;

    // Is this the actual parent of the match?
    if (state->level == ctx->jpr->ncomponents-1) {
        m->immediate_parent_found = 1;
    }

    // Since this is the deepest parent we've found, we exit here!
    jsonsl_stop(jsn);
}

int
Match::exec_match_simple(const char *value, size_t nvalue,
    const Path::CompInfo *jpr, jsonsl_t jsn)
{
    ParseContext ctx(this, const_cast<Path::CompInfo*>(jpr));
    status = JSONSL_ERROR_SUCCESS;

    jsonsl_enable_all_callbacks(jsn);
    jsn->action_callback_PUSH = push_callback;
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

        for (ii = cur_start; ii < orig.size(); ii++) {
            /* Seek to the next negative index */
            if (orig[ii].is_neg) {
                /* Convert this to the first array index; then switch it
                 * around to extract parent information */
                is_last_neg = 1;
                break;
            }
        }

        // XXX: The next few lines could probably be more concise, but I find
        // it better to explain what's going on.

        // Set the components to use for the current match. These are the
        // components between the termination of the *last* match and the
        // next negative index (or the end!)
        Path::Component *tmpcomps = &comp_s[cur_start];
        size_t ntmpcomp = ii - cur_start;

        // We need the root element here as well, so add one more component
        // at the beginning
        tmpcomps--;
        ntmpcomp++;

        // Declare the component info itself, this is fed as the path
        // to exec_match
        Path::CompInfo tmp(tmpcomps, ntmpcomp);

        tmp[0].ptype = JSONSL_PATH_ROOT;

        /* Clear the match. There's no good way to preserve info here,
         * unfortunately. */
        clear();

        // Transpose array's last element as the match itself
        if (is_last_neg) {
            get_last = 1;
        }

        rv = exec_match_simple(last_start, last_len, &tmp, jsn);
        match_level += level_offset;
        if (level_offset) {
            // 'nth iteration
            match_level--;
        }

        level_offset = match_level;

        if (rv != 0) { /* error */
            return rv;
        } else if (status != JSONSL_ERROR_SUCCESS) {
            return 0;
        } else if (matchres != JSONSL_MATCH_COMPLETE) {
            return 0;
        }

        last_start = loc_deepest.at;
        last_len = loc_deepest.length;

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

static const Loc validate_ARRAY_PRE("[", 1);
static const Loc validate_ARRAY_POST("]", 1);
static const Loc validate_DICT_PRE("{\"k\":", 5);
static const Loc validate_DICT_POST("}", 1);
static const Loc validate_NOOP(NULL, 0);

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
