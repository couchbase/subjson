/*
 *     Copyright 2015-Present Couchbase, Inc.
 *
 *   Use of this software is governed by the Business Source License included
 *   in the file licenses/BSL-Couchbase.txt.  As of the Change Date specified
 *   in that file, in accordance with the Business Source License, use of this
 *   software will be governed by the Apache License, Version 2.0, included in
 *   the file licenses/APL2.txt.
 */

#define INCLUDE_JSONSL_SRC
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
        }
        return add_array_index(-1);
    }

    for (ii = 0; ii < len; ii++) {
        const char *c = &component[ii];
        if (*c < 0x30 || *c > 0x39) {
            return JSONSL_ERROR_INVALID_NUMBER;
        }
        size_t tmpval = numval;
        tmpval *= 10;
        tmpval += *c - 0x30;

        /* check for overflow */
        if (tmpval < numval) {
            return JSONSL_ERROR_INVALID_NUMBER;
        }
        numval = tmpval;
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
    jpr_comp.is_neg = false;
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
    comp.pstr = nullptr;
    if (ixnum == -1) {
        has_negix = true;
        comp.is_neg = true;
    } else {
        comp.is_neg = false;
    }
    return JSONSL_ERROR_SUCCESS;
}

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

// Generated from the table in jsonsl.c (Allowed_Escapes).
bool isAllowedJsonEscapes(uint8_t code) {
    switch (code) {
    case 0x22:
    case 0x2f:
    case 0x5c:
    case 0x62:
    case 0x66:
    case 0x6e:
    case 0x72:
    case 0x74:
    case 0x75:
        return true;
    default:;
    }
    return false;
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
        const auto cur_c = static_cast<uint8_t>(path[ii]);
        // Escape handling
        const auto can_jescape = isAllowedJsonEscapes(cur_c);
        if (in_json_escape) {
            if (!can_jescape) {
                return JSONSL_ERROR_JPR_BADPATH;
            }
            if (cur_c == 'u') {
                /* We can't handle \u-escapes in paths now! */
                return JSONSL_ERROR_JPR_BADPATH;
            }
            in_json_escape = false;
        } else if (cur_c == '\\') {
            in_json_escape = true;
        } else if (cur_c == '"' || cur_c < 0x1F) {
            // Needs escape!
            return JSONSL_ERROR_JPR_BADPATH;
        }

        if (cur_c == '`') {
            n_backticks++;
            in_n1ql_escape = !in_n1ql_escape;
        }
        if (in_n1ql_escape) {
            continue;
        }

        // Token handling
        if (cur_c == ']') {
            return JSONSL_ERROR_JPR_BADPATH;
        }
        if (cur_c == '[' || cur_c == '.') {
            *n_consumed = ii;
            if (cur_c == '.') {
                *n_consumed += 1;
            }

            if (in_json_escape) {
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
        comp.pstr = nullptr;
        comp.ptype = JSONSL_PATH_NONE;
        comp.is_neg = false;
    }

    // Reset all used components back to default state (ready for re-use); and
    // transfer to head of cached list.
    for (auto& component : m_used) {
        component->clear();
    }
    m_cached.splice(m_cached.begin(), m_used);
}
