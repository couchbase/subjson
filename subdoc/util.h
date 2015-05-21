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
