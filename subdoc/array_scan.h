/*
 *     Copyright 2026-Present Couchbase, Inc.
 *
 *   Use of this software is governed by the Business Source License included
 *   in the file licenses/BSL-Couchbase.txt.  As of the Change Date specified
 *   in that file, in accordance with the Business Source License, use of this
 *   software will be governed by the Apache License, Version 2.0, included in
 *   the file licenses/APL2.txt.
 */

#pragma once

#include <cstddef>
#include <string_view>

namespace Subdoc {

/** The result of scanning an array with scan_array() */
struct ArrayScanResult {
    enum class Status {
        /** The end of the array was located */
        Success,
        /** The needle was found as one of the elements in the array */
        Found,
        /**
         * A container (array or object) was found in the array before the
         * needle was found (only reported when searching for a needle)
         */
        NonPrimitive,
        /** The array is not terminated within the buffer, or is malformed */
        Malformed
    };

    Status status = Status::Malformed;

    /**
     * The length of the array including the opening and closing bracket.
     * Only valid when status is Success.
     */
    size_t length = 0;

    /**
     * The number of elements in the array. Only valid when status is
     * Success.
     */
    size_t num_elements = 0;

    /**
     * The (zero-based) index of the element equal to the needle. Only
     * valid when status is Found.
     */
    size_t found_index = 0;
};

/**
 * Locate the end of the JSON array starting at the beginning of the provided
 * buffer, and (optionally) search for an element which is byte-wise equal to
 * the provided needle.
 *
 * Unlike the jsonsl based matching this does not tokenize or validate the
 * contents of the array; it only tracks string boundaries and nesting of
 * brackets. This makes it significantly faster than running the full parser
 * over the array, and the caller must only use it on documents which are
 * known to contain valid JSON. Should the scan run off the end of the
 * buffer (for instance due to an unterminated string or array)
 * Status::Malformed is returned.
 *
 * @param buffer The buffer to scan. The first character must be the opening
 *               bracket of the array, and the buffer may extend beyond the
 *               end of the array.
 * @param needle If non-empty, the top-level elements of the array are
 *               compared (byte-wise, ignoring surrounding whitespace in the
 *               document) with the needle, and the scan stops as soon as a
 *               match is found (Status::Found) or a non-primitive element is
 *               found (Status::NonPrimitive).
 * @return The result of the scan
 */
ArrayScanResult scan_array(std::string_view buffer, std::string_view needle);

} // namespace Subdoc
