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
class Operation {
public:
    /* malloc'd because this block is pretty big (several k) */
    Path *path;
    /* cached JSON parser */
    jsonsl_t jsn;

    Match match;

    /* opcode */
    Command optype;

    /* Location of original document */
    Loc doc_cur;

    /* Location of the user's "Value" (if applicable) */
    union {
        Loc user_in;
        struct {
            uint64_t delta_in;
            int64_t cur;
        } arith;
    };

    /* Location of the fragments consisting of the _new_ value */
    Loc doc_new[8];
    /* Number of fragments active */
    size_t doc_new_len;

    Operation();
    void clear();
    ~Operation();
    Error op_exec(const char *pth, size_t npth);
    Error op_exec(const std::string& s) { return op_exec(s.c_str(), s.size()); }

    void set_value(const char *s, size_t n) { user_in.assign(s, n); }
    void set_value(const std::string& s) { set_value(s.c_str(), s.size()); }
    void set_delta(uint64_t delta) { arith.delta_in = delta; }
    int64_t get_numresult() const { return arith.cur; }

    void set_doc(const char *s, size_t n) { doc_cur.assign(s, n); }
    void set_doc(const std::string& s) { set_doc(s.c_str(), s.size()); }

    void set_code(uint8_t code) { optype = code; }

private:
    std::string bkbuf;
    std::string numbuf;

    Error do_match_common();
    Error do_get();
    Error do_store_dict();

    enum MkdirPMode {
        MKDIR_P_ARRAY, //!< Insert ... "key": [ value ]
        MKDIR_P_DICT //!< Insert ... "key":value
    };
    Error do_mkdir_p(MkdirPMode mode);
    Error find_first_element();
    Error find_last_element();
    Error insert_singleton_element();
    Error do_list_enoent();
    Error do_list_op();
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
};
}

typedef Subdoc::Operation subdoc_OPERATION;
typedef Subdoc::Loc subdoc_LOC;
typedef Subdoc::Path subdoc_PATH, subdoc_PATH_st;
typedef Subdoc::Operation subdoc_OPERATION;
typedef Subdoc::Match subdoc_MATCH;
typedef Subdoc::Error subdoc_ERRORS;
typedef Subdoc::Command::Code subdoc_OPTYPE;

#endif
