/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
*     Copyright 2015-Present Couchbase, Inc.
*
*   Use of this software is governed by the Business Source License included
*   in the file licenses/BSL-Couchbase.txt.  As of the Change Date specified
*   in that file, in accordance with the Business Source License, use of this
*   software will be governed by the Apache License, Version 2.0, included in
*   the file licenses/APL2.txt.
*/

#ifndef LCB_SUBDOCAPI_H
#define LCB_SUBDOCAPI_H
#include <cstdint>
namespace Subdoc {

/**
 * The following error codes are returned when a certain sub-document
 * manipulation function could not be executed. Traditional memcached
 * errors may also be returned.
 */
class Error {
public:
    enum Code {
        SUCCESS = 0, /* PROTOCOL_BINARY_RESPONSE_SUCCESS*/
        /** Document exists, but the given path was not found in the document */
        PATH_ENOENT,
        /** There was a conflict between the data and the path */
        PATH_MISMATCH,
        /** The path is not a valid path (i.e. does not parse correctly) */
        PATH_EINVAL,
        /** The document reference exists, but is not JSON */
        DOC_NOTJSON,
        /**The requested operation required the value did not already exist, but it exists */
        DOC_EEXISTS,
        /**The path requested is too long/deep to traverse */
        PATH_E2BIG,
        /**The number to increment was too big (could not fit in an int64_t) */
        NUM_E2BIG,
        /**Combining the delta with the number would cause an over/underflow*/
        DELTA_OVERFLOW,
        /**Delta is either not a number, 0, or not within range of int64 */
        DELTA_EINVAL,
        /**Invalid value for insertion. Inserting this value would invalidate
         * the JSON document */
        VALUE_CANTINSERT,
        /** Document too deep to parse */
        DOC_ETOODEEP,

        /** Missing a value */
        VALUE_EMPTY,

        /** Inserting the value would cause the document to be too deep */
        VALUE_ETOODEEP,

        /* MEMCACHED ERROR CODES */
        GLOBAL_ENOSUPPORT,
    };

    Code m_code;
    Error(Code c = SUCCESS) : m_code(c) {}
    operator int() const { return static_cast<int>(m_code); }
    bool success() const { return m_code == SUCCESS; }
    Code code() const { return m_code; }
    const char *description() const;
};

/**@name Paths
 * A Sub-Document _PATH_ is a path to the container of the item you want to
 * access. Every JSON primitive is stored either as an array element or a
 * dictionary value. In the case of an array element, the _path_ is the path
 * to the numeric index of the array; in the case of a dictionary value, the
 * _path_ is the path to the string key for the value to be accessed (or
 * modified).
 *
 * Path components are separated by a period (`.`). To escape literal `.`
 * characters, encapsulate the given component in backticks.
 *
 * Any other tokens in the path must match _exactly_ the way they might be
 * represented in the document and must be valid JSON. For example, if an
 * element in the path contains a literal quote, the quote must be escaped
 * like so:
 *
 * @code
 * foo.qu\"oted.path
 * @endcode
 */



/**
 * @name Commands
 *
 * Each of these commands operates on a subdoc_PATH. The actual semantics
 * of the path depends on the operation. However, in general:
 *
 * _Dictionary Paths_ are expected to point to a specific dictionary _key_. For
 * update operations, the existing value is replaced. For removal operations
 * the key and value are removed. For
 *
 *
 * @{
 */
class Command {
public:
    enum Code {
        /* These operations are common because they operate on the _value_ only: */
        /** Get the value located in the path */
        GET = 0x00,

        /** Simply check the path exists */
        EXISTS = 0x01,

        /** Replace the value, if the path exists */
        REPLACE = 0x02,

        /** Remove the value, if the path exists */
        REMOVE = 0x03,

        /* Dictionary operations. Only valid if PATH points to a dictionary.
         * The _P variants are similar to `mkdir -p` and will create intermediate
         * path entries if they do not exist. For example, consider an empty
         * document, an `ADD` command for `foo.bar.baz` will fail (since `foo.bar`
         * does not exist), however an `ADD_P` command will succeed, creating
         * `foo` and `bar` as dictionaries, resulting in a document that looks
         * like this:
         * {"foo":{"bar":{"baz":VALUE}}}
         */

        /** Add or replace a value for the given path */
        DICT_UPSERT = 0x04,
        DICT_UPSERT_P = 0x84,

        /** Add a value for the given path. Fail if the value already exists */
        DICT_ADD = 0x05,
        DICT_ADD_P = 0x85,

        /* Array operations. Only valid if PATH points to an array */

        /* Note, there is no insert/upsert for an array, since this would require
         * padding surrounding elements with something else, which is probably not
         * what a user wants */

        /* The _P variants will create intermediate path elements, if they do
         * not exist */
        ARRAY_PREPEND = 0x06,
        ARRAY_PREPEND_P = 0x86,

        ARRAY_APPEND = 0x07,
        ARRAY_APPEND_P = 0x87,

        /**Adds a value to a list, ensuring that the value does not already exist.
         * Values added can only be primitives, and the list itself must already
         * only contain primitives. If any of these is violated, the error
         * SUBDOC_PATH_MISMATCH is returned. */
        ARRAY_ADD_UNIQUE = 0x08,
        ARRAY_ADD_UNIQUE_P = 0x88,

        /**Inserts a new value at a given position. Existing values will be
         * pushed back by one */
        ARRAY_INSERT = 0x09,

        /* In the protocol this should contain a 64-bit integer
         *
         * If the number itself does not fit into a uint64_t (if unsigned) or an
         * int64_t (if signed), a SUBDOC_NUM_E2BIG error is returned.
         *
         * If the resulting item does exist, but is not a signed or unsigned integer,
         * then a SUBDOC_PATH_MISMATCH error is returned. This is the case for
         * 'floats' and 'exponents' as well. Only whole integers are supported.
         */
        COUNTER = 0x0A,
        COUNTER_P = 0x8A,

        /**
         * Returns the number of elements in a list or key-value pairs in a
         * dictionary.
         */
        GET_COUNT = 0x0B,

        INVALID = 0xff,
        FLAG_MKDIR_P = 0x80
    };

    uint8_t code;
    Command(uint8_t code = GET) : code(code) {}
    operator uint8_t() const { return code; }

    /// Whether the command should create intermediates
    bool is_mkdir_p() const { return (code & FLAG_MKDIR_P) != 0; }

    /// Return the base command (with any modifiers stripped)
    Code base() const { return static_cast<Code>(code & ~FLAG_MKDIR_P); }

};
/**@}*/
}

#endif
