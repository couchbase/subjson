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

#if !defined(SUBDOC_PATH_H) && defined(__cplusplus)
#define SUBDOC_PATH_H

#include "subdoc-api.h"
#include "jsonsl_header.h"
#include <string>
#include <list>


namespace Subdoc {
/**
 * Path limits:
 *
 * Path limits are designed to avoid abuse of the server by making paths too
 * deep. The jsonsl parser has a fixed limit of depth (adjustable at runtime,
 * but it is still fixed when allocating it).
 *
 * In the context of JSONSL, each JSON document contains an ephemeral "root"
 * object which is at level 0. The actual top-level object (the list or object)
 * is considered to be at level 1, and any of its children are considered to be
 * level 2 and so on. Thus for example
 *
 * @code
 * L0
 *    L1
 *    {
 *       L2
 *       "foo"
 *       :
 *       "bar"
 *    }
 * @endcode
 *
 * and so on.
 *
 * It also follows that a maximum depth of 1 only allows a top level (empty)
 * object.
 *
 */
class Limits {
public:
    static const size_t MAX_COMPONENTS = 32;
    static const size_t PARSER_DEPTH = MAX_COMPONENTS + 1;
    static const size_t PATH_COMPONENTS_ALLOC = MAX_COMPONENTS + 1;
};

class Path {
public:
    typedef jsonsl_jpr_component_st Component;
    typedef jsonsl_jpr_st CompInfo;

    Path();
    ~Path();
    void clear();
    int parse(const char *, size_t);
    int parse(const char *s) { return parse(s, strlen(s)); }
    int parse(const std::string& s) { return parse(s.c_str(), s.size()); }
    void pop_component() { jpr_base.ncomponents--; }
    jsonsl_error_t add_array_index(long ixnum);
    size_t size() const { return jpr_base.ncomponents; }
    Component& get_component(int ix) const { return jpr_base.components[ix]; }
    Component& operator[](size_t ix) const { return get_component(ix); }

    CompInfo jpr_base;
    Component components_s[Limits::PATH_COMPONENTS_ALLOC];
    bool has_negix; /* True if there is a negative array index in the path */
private:
    inline const char * convert_escaped(const char *src, size_t &len);
    inline int add_num_component(const char *component, size_t len);
    inline int add_str_component(const char *component, size_t len, int n_backtick);
    inline Component& alloc_component(jsonsl_jpr_type_t type);

    std::list<std::string*> m_cached;
    std::list<std::string*> m_used;
};
}

#endif
