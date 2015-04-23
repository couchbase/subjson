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
#include "subdoc-tests-common.h"

namespace t_subdoc {
using std::string;
string
getMatchString(const subdoc_MATCH& m)
{
    return string(m.loc_match.at, m.loc_match.length);
}

string
getMatchKey(const subdoc_MATCH& m)
{
    if (m.has_key) {
        return string(m.loc_key.at, m.loc_key.length);
    } else {
        return string();
    }
}
string
getParentString(const subdoc_MATCH& m)
{
    return string(m.loc_parent.at, m.loc_parent.length);
}

const char *
getJsnErrstr(jsonsl_error_t err) {
#define X(n) if (err == JSONSL_ERROR_##n) { return #n; }
    JSONSL_XERR;
#undef X
    return "UNKNOWN";
}
}
