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

#include "subdoc-c.h"

#if !defined(SUBDOC_OPERATIONS_H) && defined(__cplusplus)
#define SUBDOC_OPERATIONS_H
#include "subdoc-api.h"
#include "loc.h"
#include "path.h"
#include "match.h"
#include "subdoc-util.h"

namespace Subdoc {

/**
 * This class acts as a user-allocated buffer region for the Operation
 * object to store results.
 *
 * This will be treated as an "out" param
 */
class Result {
public:
    Result() : m_newlen(0) {}
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
        return Buffer<Loc>(m_newdoc, m_newlen);
    }
    const Loc& matchloc() const { return m_match; }
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
    Loc m_newdoc[8];
    size_t m_newlen = 0;
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
    Error do_list_prepend();
    Error do_arith_op();
    Error do_insert();

    // Wrapper around Validator::validate(), this omits the
    // passing of arguments we already have (for example, the new object)
    // and also wraps the error code
    Error validate(int mode, int depth = -1);

    enum DepthMode {
        /// Last component of the path is the key for the new item
        PATH_HAS_NEWKEY,
        /// Path consists only of parent elements
        PATH_IS_PARENT
    };
    inline int get_maxdepth(DepthMode mode) const;

    //! Equivalent to m_newdoc[n]. This is here so our frequent access
    //! can occupy less line space.
    Loc& newdoc_at(size_t n) { return m_result->m_newdoc[n]; }
};
}

#endif
