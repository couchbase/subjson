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

#include "loc.h"
#include "path.h"

namespace Subdoc {

/** Structure describing a match for an item */
class Match {
public:
    /**The JSON type for the result (i.e. jsonsl_type_t). If the match itself
     * is not found, this will contain the innermost _parent_ type. */
    uint32_t type = 0;

    /**Error status (jsonsl_error_t) */
    uint16_t status = 0;

    /** result of the match. jsonsl_jpr_match_t */
    int16_t matchres = 0;

    /** Flags, if #type is JSONSL_TYPE_SPECIAL */
    uint16_t sflags = 0;

    /**
     * The deepest level at which a possible match was found.
     * In jsonsl, the imaginary root level is 0, the top level container
     * is 1, etc.
     */
    uint16_t match_level = 0;

    /**
     * The current position of the match. This value is 0-based and works
     * in conjunction with #num_siblings to determine how to handle
     * surrounding items for various modification items.
     */
    unsigned position = 0;

    /**The number of children in the last parent. Note this may not necessarily
     * be the immediate parent; but rather indicates whether any kind of special
     * comma handling is required.
     *
     * This is used by insertion/deletion operations to determine if any
     * trailing tokens from surrounding items should be stripped.
     * If a match is found, then this _excludes_ the match (in other words,
     * this is not the size of the container, but rather how many elements
     * in the container are not the match)
     */
    unsigned num_siblings =0;

    /** For array matches, contains the number of children in the array */
    unsigned num_children = 0;

    /**
     * Check if match is the first of many
     * @return true iff match is the first of multiple siblings
     */
    bool is_first() const {
        return num_siblings && position == 0;
    }

    /**
     * Check if match is the last of many
     * @return true iff match is the last of multiple siblings
     */
    bool is_last() const {
        return num_siblings && position == num_siblings;
    }

    /**
     * Check if the match is alone in the container
     * @return true iff match is the only element in the container
     */
    bool is_only() const {
        return num_siblings == 0;
    }

    bool has_key() const {
        return !loc_key.empty();
    }

    /**
     * Response flag indicating that the match's immediate parent was found,
     * and its location is in #loc_parent.
     *
     * This flag is implied to be true if #matchres is JSONSL_MATCH_COMPLETE
     */
    unsigned char immediate_parent_found = 0;

    /**Request flag; indicating whether the last child position should be
     * returned inside the `loc_key` member. Note that the position will
     * be indicated in the `loc_key`'s _length_ field, and its `at` field
     * will be set to NULL
     *
     * This requires that the last child be an array element, and thus the
     * parent match object be an array.
     *
     * This also changes some of the match semantics. Here most of the
     * information will be adapted to suit the last child; this includes
     * things like the value type and such.
     */
    unsigned char get_last = 0;

    enum SearchOptions {
        GET_MATCH_ONLY = 0,
        GET_FOLLOWING_SIBLINGS
    };

    SearchOptions extra_options = GET_MATCH_ONLY;

    /**If 'ensure_unique' is true, set to true if the value of #ensure_unique
     * already exists */
    unsigned char unique_item_found = 0;

    /**
     * Deepest match found. If the match was completely found, then this
     * points to the actual match. Otherwise, this is one of the parents.
     */
    Loc loc_deepest;

    /**Location desribing the key for the item. Valid only if #has_key is true*/
    Loc loc_key;

    /**If set to true, will also descend each child element to ensure that
     * the contents here are unique. Will set an error code accordingly, if
     * types are mismatched. */
    Loc ensure_unique;

    int exec_match(const char *value, size_t nvalue, const Path *path, jsonsl_t jsn);
    int exec_match(const Loc& loc, const Path* path, jsonsl_t jsn) {
        return exec_match(loc.at, loc.length, path, jsn);
    }
    int exec_match(const std::string& s, const Path& path, jsonsl_t jsn) {
        return exec_match(s.c_str(), s.size(), &path, jsn);
    }

    Match();
    ~Match();
    void clear();

    static jsonsl_t jsn_alloc();
    static void jsn_free(jsonsl_t jsn);
private:
    inline int exec_match_simple(const char *value, size_t nvalue, const Path::CompInfo *jpr, jsonsl_t jsn);
    inline int exec_match_negix(const char *value, size_t nvalue, const Path *pth, jsonsl_t jsn);
};
} // namespace Subdoc
