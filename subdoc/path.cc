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

Path::Component&
Path::alloc_component(jsonsl_jpr_type_t type)
{
    Component& ret = components_s[jpr_base.ncomponents];
    ret.ptype = type;
    jpr_base.ncomponents++;
    return ret;
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

    Component& jpr_comp = alloc_component(JSONSL_PATH_STRING);
    jpr_comp.pstr = const_cast<char*>(component);
    jpr_comp.len = len;
    jpr_comp.is_arridx = 0;
    jpr_comp.is_neg = 0;
    return 0;
}

jsonsl_error_t
Path::add_array_index(long ixnum)
{
    if (size() == Limits::MAX_COMPONENTS) {
        return JSONSL_ERROR_LEVELS_EXCEEDED;
    }

    Component& comp = alloc_component(JSONSL_PATH_NUMERIC);
    comp.len = 0;
    comp.is_arridx = 1;
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

/* So this should somehow give us a 'JPR' object.. */
int
Path::parse(const char *path, size_t len)
{
    /* Path's buffers cannot change */
    const char *c, *last, *path_end = path + len;
    int in_escape = 0;
    int in_bracket = 0;
    int n_backtick = 0;
    int rv;

    jpr_base.ncomponents = 0;
    jpr_base.components = components_s;
    jpr_base.orig = const_cast<char*>(path);
    jpr_base.norig = len;
    has_negix = false;

    alloc_component(JSONSL_PATH_ROOT);

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
                rv = add_str_component(last, c-last, n_backtick);
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
                rv = add_num_component(last, c-last);
                comp_added = 1;
                in_bracket = 0;
                /* Check next character is valid */
                const char *tmpnext = c + 1;
                if (tmpnext != path_end && *tmpnext != '[' && *tmpnext != '.') {
                    return JSONSL_ERROR_JPR_BADPATH;
                }
            }
        } else if (*c == '.') {
            rv = add_str_component(last, c-last, n_backtick);
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
        return add_str_component(last, c-last, n_backtick);
    } else {
        return 0;
    }
}

Path::Path() {
    memset(&jpr_base, 0, sizeof jpr_base);
    memset(components_s, 0, sizeof components_s);
    has_negix = false;
}

Path::~Path() {
    clear();
    std::list<std::string*>::iterator ii = m_cached.begin();
    for (; ii != m_cached.end(); ++ii) {
        delete *ii;
    }
}

void
Path::clear() {
    unsigned ii;
    for (ii = 1; ii < size(); ii++) {
        Component& comp = get_component(ii);
        comp.pstr = NULL;
        comp.is_arridx = 0;
    }
    m_cached.insert(m_cached.end(), m_used.begin(), m_used.end());
    m_used.clear();
}
