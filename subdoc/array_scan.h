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
#include <limits>
#include <string>
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
        /**
         * The nesting depth exceeds the maximum depth (see scan_array())
         */
        TooDeep,
        /** The array is not terminated within the buffer, or is malformed */
        Malformed
    };

    Status status = Status::Malformed;

    /**
     * The length of the array (or object) including the opening and
     * closing bracket (or brace). Only valid when status is Success.
     */
    size_t length = 0;

    /**
     * The number of elements in the array (or members in the object).
     * Only valid when status is Success.
     */
    size_t num_elements = 0;

    /**
     * The (zero-based) index of the element equal to the needle. Only
     * valid when status is Found.
     */
    size_t found_index = 0;

    /**
     * The number of elements removed by filter_array(). Only valid when
     * status is Success.
     */
    size_t num_removed = 0;

    /**
     * The offset (from the beginning of the buffer) and length of the last
     * element in the array, with surrounding whitespace removed. Only set
     * by scan_array() (and only valid when status is Success and
     * num_elements is non-zero).
     */
    size_t last_element_offset = 0;
    size_t last_element_length = 0;
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
 * The nesting depth is limited in the same way as by a parser which needs
 * a level for every value (such as jsonsl): the array itself has depth 1,
 * the containers in it may be nested to a depth of at most max_depth, and
 * a container at max_depth must be empty (as its values would need
 * another level). Status::TooDeep is returned if the array exceeds it.
 *
 * @param buffer The buffer to scan. The first character must be the opening
 *               bracket of the array, and the buffer may extend beyond the
 *               end of the array.
 * @param needle If non-empty, the top-level elements of the array are
 *               compared (byte-wise, ignoring surrounding whitespace in the
 *               document) with the needle, and the scan stops as soon as a
 *               match is found (Status::Found) or a non-primitive element is
 *               found (Status::NonPrimitive, reported once the element has
 *               been scanned so that a depth error inside it takes
 *               precedence, as with a parser).
 * @param max_depth The maximum nesting depth (see above)
 * @return The result of the scan
 */
ArrayScanResult scan_array(
        std::string_view buffer,
        std::string_view needle,
        size_t max_depth = std::numeric_limits<size_t>::max());

/**
 * Locate the end of the JSON array or object starting at the beginning of
 * the provided buffer and count its elements (or members), with the same
 * restrictions as scan_array().
 *
 * @param buffer The buffer to scan. The first character must be the opening
 *               bracket of the array or the opening brace of the object, and
 *               the buffer may extend beyond its end.
 * @param max_depth The maximum nesting depth (see scan_array())
 * @return The result of the scan. For an object num_elements is the number
 *         of members (key-value pairs).
 */
ArrayScanResult scan_container(
        std::string_view buffer,
        size_t max_depth = std::numeric_limits<size_t>::max());

/**
 * Scan the JSON array starting at the beginning of the provided buffer
 * (with the same restrictions as scan_array()) and collect the top-level
 * elements which are not removed into survivors.
 *
 * An element is removed if it is byte-wise equal to the needle (ignoring
 * surrounding whitespace in the document); either only the first such
 * element or all of them. The surviving elements are appended to
 * survivors separated by commas, keeping their original order (whitespace
 * between consecutive surviving elements is preserved).
 *
 * @param buffer The buffer to scan. The first character must be the opening
 *               bracket of the array, and the buffer may extend beyond the
 *               end of the array.
 * @param needle The (non-empty) value to remove
 * @param remove_all If true remove all matching elements, otherwise only
 *                   the first one
 * @param survivors Where to append the surviving elements. Data is appended
 *                  to it even if the scan fails.
 * @param max_depth The maximum nesting depth (see scan_array())
 * @return The result of the scan. Status::NonPrimitive is returned if any
 *         element in the array is an array or object (regardless of its
 *         position). On Status::Success num_removed holds the number of
 *         removed elements.
 */
ArrayScanResult filter_array(
        std::string_view buffer,
        std::string_view needle,
        bool remove_all,
        std::string& survivors,
        size_t max_depth = std::numeric_limits<size_t>::max());

} // namespace Subdoc
