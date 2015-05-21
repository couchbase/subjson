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

#include "util.h"
using namespace Subdoc;
using std::string;

string
Util::match_match(const Match& m)
{
    return m.loc_match.to_string();
}

string
Util::match_key(const Match& m)
{
    if (m.has_key) {
        return m.loc_key.to_string();
    } else {
        return string();
    }
}

string
Util::match_parent(const Match& m)
{
    return m.loc_parent.to_string();
}

void
Util::dump_newdoc(const Result& op, std::ostream& os)
{
    auto newdoc = op.newdoc();
    os << "Dumping doc with "
       << std::dec << newdoc.size() << " segments" << std::endl;
    for (size_t ii = 0; ii < newdoc.size(); ++ii) {
        os << "[" << std::dec << ii << "]: ";
        os.write(newdoc[ii].at, newdoc[ii].length);
        os << std::endl;
    }
}

const char *
Util::jsonerr(jsonsl_error_t err)
{
#define X(n) if (err == JSONSL_ERROR_##n) { return #n; }
    JSONSL_XERR;
#undef X
    return "UNKNOWN";
}

namespace std {
ostream&
operator<<(ostream& os, const Subdoc::Error::Code& err) {
    os << "0x" << std::hex << static_cast<int>(err)
       << " (" << Subdoc::Error(err).description() << ")";
    return os;
}
}
