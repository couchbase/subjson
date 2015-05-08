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

#include <gtest/gtest.h>
#include "subdoc/subdoc-api.h"
#include "subdoc/path.h"
#include "subdoc/match.h"
#include "subdoc/operations.h"
#include <string>
#include <iostream>

namespace t_subdoc {
    std::string getMatchString(const subdoc_MATCH& m);
    std::string getMatchKey(const subdoc_MATCH& m);
    std::string getParentString(const subdoc_MATCH& m);
    const char *getJsnErrstr(jsonsl_error_t err);
}

namespace std {
inline ostream& operator<<(ostream& os, const Subdoc::Error::Code& err) {
    os << "0x" << std::hex << static_cast<int>(err)
       << " (" << Subdoc::Error(err).description() << ")";
    return os;
}
inline ostream& operator<<(ostream& os, const Subdoc::Error& err) {
    return os << err.code();
}
}
