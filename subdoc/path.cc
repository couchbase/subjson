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

#define INCLUDE_JSONSL_SRC
#include "subdoc-api.h"
#include "path.h"

using namespace Subdoc;

const char *
Path::convert_escaped(const char *src, size_t& len)
{
    if (m_cached.empty()) {
        m_used.push_back(new std::string());
    } else {
        m_used.push_back(m_cached.back());
        m_cached.pop_back();
    }
    std::string& s = *m_used.back();

    for (size_t ii = 0; ii < len; ii++) {
        if (src[ii] != '`') {
            s += src[ii];
        } else if(src[ii] == '`' && ii+1 < len && src[ii+1] == '`') {
            s += src[ii++];
        }
    }
    len = s.size();
    return s.c_str();
}

/* Adds a numeric component */
int
Path::add_num_component(const char *component, size_t len)
{
    unsigned ii;
    size_t numval = 0;

    if (component[0] == '-') {
        if (len != 2 || component[1] != '1') {
            return JSONSL_ERROR_INVALID_NUMBER;
        } else {
            return add_array_index(-1);
        }
    }

    for (ii = 0; ii < len; ii++) {
        const char *c = &component[ii];
        if (*c < 0x30 || *c > 0x39) {
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
    return add_array_index(numval);
}

int
Path::add_str_component(const char *component, size_t len, int n_backtick)
{
    /* Allocate first component: */
    if (len > 1 && component[0] == '`' && component[len-1] == '`') {
        component++;
        n_backtick -= 2;
        len -= 2;
    }

    if (size() == Limits::MAX_COMPONENTS) {
        return JSONSL_ERROR_LEVELS_EXCEEDED;
    }
    if (len == 0) {
        return JSONSL_ERROR_JPR_BADPATH;
    }

    if (n_backtick) {
        /* OHNOEZ! Slow path */
        component = convert_escaped(component, len);
    }

    Component& jpr_comp = add(JSONSL_PATH_STRING);
    jpr_comp.pstr = const_cast<char*>(component);
    jpr_comp.len = len;
    jpr_comp.is_neg = 0;
    return 0;
}

jsonsl_error_t
Path::add_array_index(long ixnum)
{
    if (size() == Limits::MAX_COMPONENTS) {
        return JSONSL_ERROR_LEVELS_EXCEEDED;
    }

    Component& comp = add(JSONSL_PATH_NUMERIC);
    comp.len = 0;
    comp.idx = ixnum;
    comp.pstr = NULL;
    if (ixnum == -1) {
        has_negix = true;
        comp.is_neg = 1;
    } else {
        comp.is_neg = 0;
    }
    return JSONSL_ERROR_SUCCESS;
}

/* Copied over from jsonsl */
static const int allowed_json_escapes[0x100] = {
        /* 0x00 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x1f */
        /* 0x20 */ 0,0, /* 0x21 */
        /* 0x22 */ 1 /* <"> */, /* 0x22 */
        /* 0x23 */ 0,0,0,0,0,0,0,0,0,0,0,0, /* 0x2e */
        /* 0x2f */ 1 /* </> */, /* 0x2f */
        /* 0x30 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x4f */
        /* 0x50 */ 0,0,0,0,0,0,0,0,0,0,0,0, /* 0x5b */
        /* 0x5c */ 1 /* <\> */, /* 0x5c */
        /* 0x5d */ 0,0,0,0,0, /* 0x61 */
        /* 0x62 */ 1 /* <b> */, /* 0x62 */
        /* 0x63 */ 0,0,0, /* 0x65 */
        /* 0x66 */ 1 /* <f> */, /* 0x66 */
        /* 0x67 */ 0,0,0,0,0,0,0, /* 0x6d */
        /* 0x6e */ 1 /* <n> */, /* 0x6e */
        /* 0x6f */ 0,0,0, /* 0x71 */
        /* 0x72 */ 1 /* <r> */, /* 0x72 */
        /* 0x73 */ 0, /* 0x73 */
        /* 0x74 */ 1 /* <t> */, /* 0x74 */
        /* 0x75 */ 1 /* <u> */, /* 0x75 */
        /* 0x76 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0x95 */
        /* 0x96 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0xb5 */
        /* 0xb6 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0xd5 */
        /* 0xd6 */ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, /* 0xf5 */
        /* 0xf6 */ 0,0,0,0,0,0,0,0,0, /* 0xfe */
};

int
Path::parse_bracket(const char *path, size_t len, size_t *n_consumed)
{
    // Check if 0 before decreasing! */
    if (len == 0) {
        return JSONSL_ERROR_JPR_BADPATH;
    }

    // Adjust positions so we don't parse the first '[':
    len--, path++, *n_consumed = 1;

    for (size_t ii = 0; ii < len; ii++) {
        if (path[ii] == ']') {
            *n_consumed += (ii + 1);
            return add_num_component(path, ii);
        }
    }

    // Didn't find the closing ']'
    return JSONSL_ERROR_JPR_BADPATH;
}

int
Path::parse_string(const char *path, size_t len, size_t *n_consumed)
{
    bool in_n1ql_escape = false;
    bool in_json_escape = false;
    int n_backticks = 0;

    if (len == 0) {
        return JSONSL_ERROR_JPR_BADPATH;
    }

    for (size_t ii = 0; ii < len; ii++) {
        // Escape handling
        int can_jescape = allowed_json_escapes[static_cast<int>(path[ii])];
        if (in_json_escape) {
            if (!can_jescape) {
                return JSONSL_ERROR_JPR_BADPATH;
            } else if (path[ii] == 'u') {
                /* We can't handle \u-escapes in paths now! */
                return JSONSL_ERROR_JPR_BADPATH;
            }
            in_json_escape = false;
        } else if (path[ii] == '\\') {
            in_json_escape = true;
        } else if (path[ii] == '"' || path[ii] < 0x1F) {
            // Needs escape!
            return JSONSL_ERROR_JPR_BADPATH;
        }

        if (path[ii] == '`') {
            n_backticks++;
            in_n1ql_escape = !in_n1ql_escape;
        }
        if (in_n1ql_escape) {
            continue;
        }

        // Token handling
        if (path[ii] == ']') {
            return JSONSL_ERROR_JPR_BADPATH;
        } else if (path[ii] == '[' || path[ii] == '.') {
            *n_consumed = ii;
            if (path[ii] == '.') {
                *n_consumed += 1;
            }

            if (in_n1ql_escape || in_json_escape) {
                return JSONSL_ERROR_JPR_BADPATH;
            }
            return add_str_component(path, ii, n_backticks);
        }
    }

    if (in_n1ql_escape || in_json_escape) {
        return JSONSL_ERROR_JPR_BADPATH;
    }

    *n_consumed = len;
    return add_str_component(path, len, n_backticks);
}

/* So this should somehow give us a 'JPR' object.. */
int
Path::parse(const char *path, size_t len)
{
    /* Path's buffers cannot change */
    ncomponents = 0;
    has_negix = false;
    add(JSONSL_PATH_ROOT);

    size_t ii = 0;

    while (ii < len) {
        size_t to_adv = 0;
        int rv;

        if (path[ii] == '[') {
            rv = parse_bracket(path + ii, len-ii, &to_adv);
            if (rv == 0) {
                ii += to_adv;
                if (ii == len) {
                    // Last character. Will implicitly break

                } else if (path[ii] == '[') {
                    // Parse it on the next iteration

                } else if (path[ii] == '.') {
                    // Skip another character. Ignore the '.'
                    ii++;
                } else {
                    return JSONSL_ERROR_JPR_BADPATH;
                }
            }
        } else {
            rv = parse_string(path + ii, len - ii, &to_adv);
            ii += to_adv;
        }

        if (rv != 0) {
            return rv;
        }
    }
    return JSONSL_ERROR_SUCCESS;
}

Path::Path() : PathComponentInfo(components_s, 0) {
    has_negix = false;
    memset(components_s, 0, sizeof components_s);
}

Path::~Path() {
    clear();
    for (auto ii : m_cached) {
        delete ii;
    }
}

void
Path::clear() {
    unsigned ii;
    for (ii = 1; ii < size(); ii++) {
        Component& comp = get_component(ii);
        comp.pstr = NULL;
        comp.ptype = JSONSL_PATH_NONE;
        comp.is_neg = 0;
    }

    m_cached.insert(m_cached.end(), m_used.begin(), m_used.end());
    m_used.clear();
}
