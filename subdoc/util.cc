/*
 *     Copyright 2015-Present Couchbase, Inc.
 *
 *   Use of this software is governed by the Business Source License included
 *   in the file licenses/BSL-Couchbase.txt.  As of the Change Date specified
 *   in that file, in accordance with the Business Source License, use of this
 *   software will be governed by the Apache License, Version 2.0, included in
 *   the file licenses/APL2.txt.
 */

#include "util.h"
#include <stdexcept>

using namespace Subdoc;
using std::string;

string
Util::match_match(const Match& m)
{
    return m.loc_deepest.to_string();
}

string
Util::match_key(const Match& m)
{
    if (m.has_key()) {
        return m.loc_key.to_string();
    } else {
        return string();
    }
}

string
Util::match_parent(const Match& m)
{
    return m.loc_deepest.to_string();
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

jsonsl_type_t
Util::get_root_type(Command command, const char *path, size_t len)
{
    char c = 0;
    // Find first non-whitespace character
    for (size_t ii = 0; ii < len; ++ii) {
        if (!isspace(path[ii])) {
            c = path[ii];
            break;
        }
    }

    if (!c) {
        switch (command.base()) {
        case Command::ARRAY_APPEND:
        case Command::ARRAY_PREPEND:
        case Command::ARRAY_ADD_UNIQUE:
            return JSONSL_T_LIST;
        default:
            return JSONSL_T_UNKNOWN;
        }
    }

    if (c == '[') {
        return JSONSL_T_LIST;
    } else {
        return JSONSL_T_OBJECT;
    }
}

