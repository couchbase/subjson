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
#ifndef SUBDOC_UTIL_H
#define SUBDOC_UTIL_H

#include "operations.h"
#include <iostream>

namespace Subdoc {

/// This class contains various utilities, mainly useful for testing/debugging
class Util {
public:
    /// Gets the actual match string
    static std::string match_match(const Match&);

    /// Gets the string of the match's parent container
    static std::string match_parent(const Match&);

    /// Gets the string of the match's key
    static std::string match_key(const Match&);

    /// Prints a representation of the various segments of the new document
    static void dump_newdoc(const Result&, std::ostream& = std::cerr);

    static const char *jsonerr(jsonsl_error_t err);

    static void do_assert(const char *e,
            const char *func, const char *file, int line);

    /// Determines the type of container the parent object must be. It derives
    /// this from the path.
    /// @param command the command associated with this path
    /// @param path the path
    /// @param length of the path
    /// @return JSONL_T_ROOT or JSONSL_T_LIST if the path parent should be
    ///     a list or or dictionary. JSONSL_T_UNKNOWN if the parent type cannot
    ///     be determined.
    static jsonsl_type_t get_root_type(Command command, const char *path, size_t len);

    static jsonsl_type_t get_root_type(Command command, const std::string& s) {
        return get_root_type(command, s.c_str(), s.size());
    }
private:
    Util();
};
} // namespace

namespace std {
ostream& operator<<(ostream&, const Subdoc::Error::Code&);
inline ostream& operator<<(ostream& os, const Subdoc::Error& err) {
    return os << err.code();
}
}

#ifdef _MSC_VER
#define SUBDOC__func__ __FUNCTION__
#else
#define SUBDOC__func__ __func__
#endif

#define SUBDOC_ASSERT(e) \
    if (!(e)) { Util::do_assert(#e, SUBDOC__func__, __FILE__, __LINE__); }

#endif /* SUBDOC_UTIL_H */
