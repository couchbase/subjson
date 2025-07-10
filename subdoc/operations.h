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
#include "loc.h"
#include "path.h"
#include "match.h"

#include <array>

namespace Subdoc {

/**
 * This class acts as a user-allocated buffer region for the Operation
 * object to store results.
 *
 * This will be treated as an "out" param
 */
class Result {
public:
    Result() = default;
    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;

    /**
     * Returns the segments of the new document. Note that the underlying
     * buffer (the one used in Operation::set_doc()) as well as this object
     * itself (i.e. the Result object) must both still remain valid for
     * the returned buffer's contents to be considered valid.
     * @return a Buffer object representing the layout of the new document.
     */
    const Buffer<Loc> newdoc() const {
        return Buffer<Loc>(m_newdoc.data(), m_newlen);
    }

    /**
     * For operations which result in a match returned to the user (e.g.
     * Command::GET, Command::COUNTER), the result is returned here.
     *
     * The returned @ref Loc may refer to regions within the input document
     * (Operation::set_doc()), and thus should be considered invalid when the
     * buffer passed to Operation::set_doc() has been modified.
     *
     * @return The location of the match.
     */
    const Loc& matchloc() const { return m_match; }

    /**
     * For operations that are non-subjson, this method provides a way to
     * pass around results within the existing infrastructure.
     *
     * Care should be taken with this method to ensure that match is a valid Loc
     *
     * @param match The match location
     */
    void set_matchloc(Loc match) {
        m_match = match;
    }

    bool push_newdoc(Loc newLoc) {
        if (m_newlen >= m_newdoc.size()) {
            return false;
        }
        m_newdoc[m_newlen++] = newLoc;
        return true;
    }

    void clear() {
        m_bkbuf.clear();
        m_numbuf.clear();
        m_match.length = 0;
        m_newlen = 0;
    }
private:
    friend class Operation;
    std::string m_bkbuf;
    std::string m_numbuf;
    std::array<Loc, 8> m_newdoc;
    size_t m_newlen = 0; // The number of Locs used in m_newdoc
    Loc m_match;
};

class Operation {
public:
    Operation();
    void clear();
    ~Operation();
    Error op_exec(const char *pth, size_t npth);
    Error op_exec(const std::string& s) { return op_exec(s.c_str(), s.size()); }

    void set_value(const char *s, size_t n) { m_userval.assign(s, n); }
    void set_value(const std::string& s) { set_value(s.c_str(), s.size()); }
    void set_result_buf(Result *res) { m_result = res; }
    void set_doc(const char *s, size_t n) { m_doc.assign(s, n); }
    void set_doc(const std::string& s) { set_doc(s.c_str(), s.size()); }
    void set_code(uint8_t code) { m_optype = code; }

    const Match& match() const { return m_match; }
    const Path& path() const { return *m_path; }
    jsonsl_t parser() const { return m_jsn; }

private:
    /* malloc'd because this block is pretty big (several k) */
    Path *m_path;
    /* cached JSON parser */
    jsonsl_t m_jsn;

    Match m_match;

    /* opcode */
    Command m_optype;

    /* Location of original document */
    Loc m_doc;

    /* Location of the user's "Value" (if applicable) */
    Loc m_userval;

    //! Pointer to result given by user
    Result *m_result;

    Error do_match_common(Match::SearchOptions options);
    Error do_get();
    Error do_store_dict();
    Error do_remove();

    enum MkdirPMode {
        MKDIR_P_ARRAY, //!< Insert ... "key": [ value ]
        MKDIR_P_DICT //!< Insert ... "key":value
    };
    Error do_mkdir_p(MkdirPMode mode);
    Error insert_singleton_element();
    Error do_list_append();
    Error do_empty_append();
    Error do_list_prepend();
    Error do_arith_op();
    Error do_insert();
    Error do_container_size();

    // Wrapper around Validator::validate(), this omits the
    // passing of arguments we already have (for example, the new object)
    // and also wraps the error code
    Error validate(int mode, size_t depth);

    enum DepthMode {
        /// Last component of the path is the key for the new item
        PATH_HAS_NEWKEY,
        /// Path consists only of parent elements
        PATH_IS_PARENT
    };
    inline size_t get_maxdepth(DepthMode mode) const;

    //! Equivalent to m_newdoc[n]. This is here so our frequent access
    //! can occupy less line space.
    Loc& newdoc_at(size_t n) { return m_result->m_newdoc[n]; }
};
} // namespace Subdoc
