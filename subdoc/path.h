/*
 *     Copyright 2015-Present Couchbase, Inc.
 *
 *   Use of this software is governed by the Business Source License included
 *   in the file licenses/BSL-Couchbase.txt.  As of the Change Date specified
 *   in that file, in accordance with the Business Source License, use of this
 *   software will be governed by the Apache License, Version 2.0, included in
 *   the file licenses/APL2.txt.
 */

#pragma once

#include "subdoc-api.h"
#include "jsonsl_header.h"
#include <string>
#include <list>


typedef jsonsl_jpr_component_st PathComponent;

class PathComponentInfo : public jsonsl_jpr_st {
public:
    PathComponentInfo(PathComponent *comps = NULL, size_t n = 0) {
        components = comps;
        ncomponents = n;
        basestr = NULL;
        orig = NULL;
        norig = 0;
        match_type = 0;
    }

    size_t size() const { return ncomponents; }

    bool empty() const { return size() == 0; }

    PathComponent& operator[](size_t ix) const { return get_component(ix); }
    PathComponent& get_component(size_t ix) const { return components[ix]; }
    PathComponent& back() const { return get_component(size()-1); }

    /**Adds a component (without bounds checking)
     * @return the newly added component*/
    PathComponent& add(jsonsl_jpr_type_t ptype) {
        PathComponent& ret = get_component(size());
        ret.ptype = ptype;
        ncomponents++;
        return ret;
    }

    void pop() {
        ncomponents--;
    }

    using iterator = PathComponent*;
    iterator begin() const {
        return components;
    }
    iterator end() const {
        return components + size();
    }
};

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

class Path : public PathComponentInfo {
public:
    typedef PathComponent Component;
    typedef PathComponentInfo CompInfo;

    Path();
    ~Path();
    void clear();
    int parse(const char *, size_t);
    int parse(const char *s) { return parse(s, strlen(s)); }
    int parse(const std::string& s) { return parse(s.c_str(), s.size()); }

    Component components_s[Limits::PATH_COMPONENTS_ALLOC];
    jsonsl_error_t add_array_index(long ixnum);
    bool has_negix; /* True if there is a negative array index in the path */
private:
    inline const char * convert_escaped(const char *src, size_t &len);
    inline int add_num_component(const char *component, size_t len);
    inline int add_str_component(const char *component, size_t len, int n_backtick);
    inline int parse_bracket(const char *path, size_t len, size_t *n_comsumed);
    inline int parse_string(const char *path, size_t len, size_t *n_consumed);

    std::list<std::string*> m_cached;
    std::list<std::string*> m_used;
};
} // namespace Subdoc
