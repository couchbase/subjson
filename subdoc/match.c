/* This file does the brunt of iterating through a buffer and determining the
 * offset where a given path may begin or end.. */

#define INCLUDE_JSONSL_SRC
#include "jsonsl_header.h"
#include "subdoc-api.h"
#include "match.h"

typedef struct {
    const char *curhk;
    jsonsl_jpr_t jpr;
    size_t hklen;
    subdoc_MATCH *match;
} parse_ctx;

static void push_callback(jsonsl_t jsn,jsonsl_action_t, struct jsonsl_state_st *, const jsonsl_char_t *);
static void pop_callback(jsonsl_t jsn,jsonsl_action_t, struct jsonsl_state_st *, const jsonsl_char_t *);


static int
err_callback(jsonsl_t jsn, jsonsl_error_t err, struct jsonsl_state_st *state,
    jsonsl_char_t *at)
{
    parse_ctx *ctx = jsn->data;
    ctx->match->status = err;
    printf("PARSE ERROR!!: %s (%d)\n", at, err);
    (void)state; (void)at;
    return 0;
}

static void
update_possible(parse_ctx *ctx, const struct jsonsl_state_st *state, const char *at)
{
    ctx->match->loc_parent.at = at;
    ctx->match->match_level = state->level;
}

static void
unique_callback(jsonsl_t jsn, jsonsl_action_t action, struct jsonsl_state_st *st,
    const jsonsl_char_t *at)
{
    parse_ctx *ctx = jsn->data;
    subdoc_MATCH *m = ctx->match;
    int rv;
    size_t slen;

    if (action == JSONSL_ACTION_PUSH) {
        /* abs. beginning of token */
        ctx->curhk = at;
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

        rv = strncmp(ctx->curhk + 1, m->ensure_unique.at + 1, slen-2);

    } else if (st->type == JSONSL_T_SPECIAL) {
        if (m->ensure_unique.length != slen) {
            return;
        } else if (slen != m->ensure_unique.length) {
            return;
        }

        rv = strncmp(ctx->curhk, m->ensure_unique.at, slen);

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
    parse_ctx *ctx = jsn->data;
    subdoc_MATCH *m = ctx->match;
    struct jsonsl_state_st *parent = jsonsl_last_state(jsn, st);
    jsonsl_type_t prtype = parent->type;

    if (st->type == JSONSL_T_HKEY) {
        ctx->curhk = at+1;
        return;
    }

    if (m->matchres == M_POSSIBLE && parent->mres == M_POSSIBLE) {
        size_t nkey = (prtype == JSONSL_T_OBJECT) ? ctx->hklen : parent->nelem - 1;

        /* Run the match */
        st->mres = jsonsl_jpr_match(ctx->jpr, prtype, parent->level, ctx->curhk, nkey);

        /* We can't have a MATCH_POSSIBLE on primitive */
        if (st->mres == M_POSSIBLE && IS_CONTAINER(st) == 0) {
            st->mres = JSONSL_MATCH_TYPE_MISMATCH;
        }

        if (st->mres == JSONSL_MATCH_COMPLETE) {
            m->matchres = JSONSL_MATCH_COMPLETE;

            m->loc_match.at = at;
            m->match_level = st->level;
            m->type = st->type;

            if (prtype == JSONSL_T_OBJECT) {
                m->has_key = 1;
                m->loc_key.at = ctx->curhk-1;
                m->loc_key.length = ctx->hklen+2;
                m->position = (st->nelem - 1) / 2;
            } else {
                m->has_key = 0;
                m->position = st->nelem - 1;
            }

            if (m->ensure_unique.at) {
                /* Start matching at first element! */
                if (prtype != JSONSL_T_LIST) {
                    m->matchres = JSONSL_MATCH_TYPE_MISMATCH;
                    st->ignore_callback = 1;
                } else {
                    jsn->action_callback_POP = unique_callback;
                    jsn->action_callback_PUSH = unique_callback;
                    unique_callback(jsn, action, st, at);
                }
            }

        } else if (st->mres == JSONSL_MATCH_NOMATCH) {
            st->ignore_callback = 1;

        } else if (st->mres == JSONSL_MATCH_POSSIBLE) {
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
pop_callback(jsonsl_t jsn, jsonsl_action_t action, struct jsonsl_state_st *state,
    const jsonsl_char_t *at)
{
    parse_ctx *ctx = jsn->data;
    subdoc_MATCH *m = ctx->match;
    size_t end_pos = jsn->pos;

    if (state->type == JSONSL_T_HKEY) {
        /* Keep the hashkey! */
        ctx->hklen = state->pos_cur - (state->pos_begin + 1);
        return;
    }

    if (state->mres == JSONSL_MATCH_COMPLETE) {
        /* We have a match, so mark the end here */
        m->loc_match.length = end_pos - state->pos_begin;
        if (state->type != JSONSL_T_SPECIAL) {
            m->loc_match.length++; /* Include the terminating token */
        } else {
            m->sflags = state->special_flags;
            m->numval = state->nelem;
        }

        /* Ignore the callback level for this depth: */
        jsn->max_callback_level = state->level;
        return;
    }

    if (!JSONSL_STATE_IS_CONTAINER(state)) {
        return;
    }

    if (m->loc_parent.length) {
        return;
    }

    if (state->mres == JSONSL_MATCH_POSSIBLE) {
        m->loc_parent.length = end_pos - state->pos_begin;
        m->loc_parent.length++;
        if (state->type == JSONSL_T_OBJECT) {
            m->num_siblings = state->nelem / 2;
        } else {
            m->num_siblings = state->nelem;
            if (m->num_siblings && m->get_last_child_pos) {
                /* Set the last child begin position */
                const struct jsonsl_state_st *child = jsonsl_last_child(jsn, state);
                m->loc_key.length = child->pos_begin;
                m->loc_key.at = NULL;
            }
        }
        if (m->matchres == JSONSL_MATCH_COMPLETE) {
            /* Exclude ourselves from the 'sibling' count */
            m->num_siblings--;
        }

        /* Zero out the rest of the callbacks */
        jsn->max_callback_level = 1;
        if (state->level == ctx->jpr->ncomponents-1) {
            m->immediate_parent_found = 1;
        }
    }

    (void)action; /* always pop */
    (void)at;
}

/* Called when */
static void initial_callback(jsonsl_t jsn, jsonsl_action_t action,
    struct jsonsl_state_st *state, const jsonsl_char_t *at)
{
    parse_ctx *ctx = jsn->data;
    /* state is the parent */
    state->mres = jsonsl_jpr_match(ctx->jpr, JSONSL_T_UNKNOWN, 0, NULL, 0);
    jsn->action_callback_PUSH = push_callback;

    if (state->mres != JSONSL_MATCH_POSSIBLE) {
        state->ignore_callback = 1;
    } else {
        update_possible(ctx, state, at);
        ctx->match->matchres = JSONSL_MATCH_POSSIBLE;
    }
    (void)action; /* always push */
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
subdoc_match_exec(const char *value, size_t nvalue,
    const subdoc_PATH *nj, jsonsl_t jsn, subdoc_MATCH *result)
{
    parse_ctx ctx = { NULL };

    ctx.match = result;
    ctx.jpr = (jsonsl_jpr_t)&nj->jpr_base;
    result->status = JSONSL_ERROR_SUCCESS;

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

jsonsl_t
subdoc_jsn_alloc(void)
{
    return jsonsl_new(COMPONENTS_ALLOC);
}

void
subdoc_jsn_free(jsonsl_t jsn)
{
    jsonsl_destroy(jsn);
}

typedef struct {
    int err;
    int rootcount;
    int flags;
} validate_ctx;

static int
validate_err_callback(jsonsl_t jsn, jsonsl_error_t err,
    struct jsonsl_state_st *state, jsonsl_char_t *at)
{
    validate_ctx *ctx = jsn->data;
    ctx->err = err;
    printf("ERROR: AT=%s\n", at);
    (void)at; (void)state;
    return 0;
}

static void
validate_callback(jsonsl_t jsn, jsonsl_action_t action,
    struct jsonsl_state_st *state, const jsonsl_char_t *at)
{
    validate_ctx *ctx = jsn->data;

    if (state->level == 1) {
        ctx->rootcount++;

    } else if (action == JSONSL_ACTION_PUSH) {
        assert(state->level == 2);
        if (ctx->flags & SUBDOC_VALIDATE_F_PRIMITIVE) {
            if (JSONSL_STATE_IS_CONTAINER(state)) {
                ctx->err = SUBDOC_VALIDATE_ENOTPRIMITIVE;
                jsn->max_callback_level = 1;
            }
        }

        if (ctx->flags & SUBDOC_VALIDATE_F_SINGLE) {
            const struct jsonsl_state_st *parent = jsonsl_last_state(jsn, state);
            int has_multielem = 0;
            if (parent->type == JSONSL_T_LIST && parent->nelem > 1) {
                has_multielem = 1;
            } else if (parent->type == JSONSL_T_OBJECT && parent->nelem > 2) {
                has_multielem = 1;
            }
            if (has_multielem) {
                ctx->err = SUBDOC_VALIDATE_EMULTIELEM;
                jsn->max_callback_level = 1;
            }
        }

    }
    (void)state;(void)at;
}

static const subdoc_LOC validate_ARRAY_PRE = { "[", 1 };
static const subdoc_LOC validate_ARRAY_POST = { "]", 1 };
static const subdoc_LOC validate_DICT_PRE = { "{\"k\":", 5 };
static const subdoc_LOC validate_DICT_POST = { "}", 1 };
static const subdoc_LOC validate_NOOP = { NULL, 0 };

jsonsl_error_t
subdoc_validate(const char *s, size_t n, jsonsl_t jsn, int mode)
{
    int need_free_jsn = 0;
    int type = mode & SUBDOC_VALIDATE_MODEMASK;
    int flags = mode & SUBDOC_VALIDATE_FLAGMASK;
    const subdoc_LOC *l_pre, *l_post;

    validate_ctx ctx = { 0,0 };
    if (jsn == NULL) {
        jsn = jsonsl_new(COMPONENTS_ALLOC);
        need_free_jsn = 1;
    }

    jsn->action_callback_POP = NULL;
    jsn->action_callback_PUSH = NULL;

    jsn->action_callback = validate_callback;
    jsn->error_callback = validate_err_callback;

    if (flags) {
        ctx.flags = flags;
        jsn->max_callback_level = 3;
    } else {
        jsn->max_callback_level = 2;
    }

    jsn->call_OBJECT = 1;
    jsn->call_LIST = 1;
    jsn->call_STRING = 1;
    jsn->call_SPECIAL = 1;
    jsn->call_HKEY = 0;
    jsn->call_UESCAPE = 0;
    jsn->data = &ctx;

    if (type == SUBDOC_VALIDATE_PARENT_NONE) {
        l_pre = l_post = &validate_NOOP;
    } else if (type == SUBDOC_VALIDATE_PARENT_ARRAY) {
        l_pre = &validate_ARRAY_PRE;
        l_post = &validate_ARRAY_POST;
    } else if (type == SUBDOC_VALIDATE_PARENT_DICT) {
        l_pre = &validate_DICT_PRE;
        l_post = &validate_DICT_POST;
    } else {
        ctx.err = JSONSL_ERROR_GENERIC;
        goto GT_ERR;
    }

    jsonsl_feed(jsn, l_pre->at, l_pre->length);
    jsonsl_feed(jsn, s, n);
    jsonsl_feed(jsn, l_post->at, l_post->length);

    if (ctx.err == JSONSL_ERROR_SUCCESS) {
        if (ctx.rootcount < 2) {
            ctx.err = SUBDOC_VALIDATE_EPARTIAL;
        } else if (ctx.rootcount > 2) {
            ctx.err = SUBDOC_VALIDATE_EMULTIELEM;
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
