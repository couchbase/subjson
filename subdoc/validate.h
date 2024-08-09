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

namespace Subdoc {
class Validator {
public:
    /// Possible values to pass as the 'flags' argument to ::validate().
    /// The `PARENT_` and `VALUE_` flags may be bitwise-OR'd together.
    enum Options {
        /// Value should be valid in its own context
        PARENT_NONE = 0x00,

        /// Value should be a valid series of array elements
        PARENT_ARRAY = 0x02,

        /// Value should be a valid dictionary value
        PARENT_DICT = 0x03,

        /// No constraint on the value type
        VALUE_ANY = 0x000,

        /// Value must be a single item (not series)
        VALUE_SINGLE = 0x100,

        /// Value must be a JSON primitive (not a container)
        VALUE_PRIMITIVE = 0x200,

    };

    enum Status {
        /// Requested a primitive, but value is not a primitive
        ENOTPRIMITIVE = JSONSL_ERROR_GENERIC + 1,
        /// Buffer contains more than a single top-level object
        EMULTIELEM,
        /// A full JSON value could not be obtained
        EPARTIAL,
        /// Max depth exceeded
        ETOODEEP = JSONSL_ERROR_LEVELS_EXCEEDED
    };

    /**
     * Convenience function to scan an item and see if it's JSON.
     *
     * @param s Buffer to check
     * @param n Size of buffer
     * @param jsn Parser. If NULL, one will be allocated and freed internally
     * @param maxdepth The maximum allowable depth of the value. This should
     *        less than COMPONENTS_ALLOC
     * @param mode The context in which the value should be checked. This is one of
     * the @ref Option constants. The mode may also be combined
     * with one of the flags to add additional constraints on the added value.
     *
     * @return JSONSL_ERROR_SUCCESS if JSON, error code otherwise.
     */
    static int validate(const char *s, size_t n, jsonsl_t jsn, int maxdepth = -1, int flags = 0);

    static int validate(const std::string& s, jsonsl_t jsn, int maxdepth = -1, int flags = 0) {
        return validate(s.c_str(), s.size(), jsn, maxdepth, flags);
    }

    static int validate(const Loc& loc, jsonsl_t jsn, int maxdepth = -1, int flags = 0) {
        return validate(loc.at, loc.length, jsn, maxdepth, flags);
    }

    static const char *errstr(int);

private:
    static const int VALUE_MASK = 0xFF00;
    static const int PARENT_MASK = 0xFF;
};
} // namespace Subdoc
