#include <gtest/gtest.h>
#include "subdoc-api.h"
#include "path.h"
#include "match.h"
#include "operations.h"
#include <string>
#include <iostream>

namespace t_subdoc {
using std::string;
static string
getMatchString(const subdoc_MATCH& m)
{
    return string(m.loc_match.at, m.loc_match.length);
}

static string
getMatchKey(const subdoc_MATCH& m)
{
    if (m.has_key) {
        return string(m.loc_key.at, m.loc_key.length);
    } else {
        return string();
    }
}
static string
getParentString(const subdoc_MATCH& m)
{
    return string(m.loc_parent.at, m.loc_parent.length);
}

static const char *
getJsnErrstr(jsonsl_error_t err) {
#define X(n) if (err == JSONSL_ERROR_##n) { return #n; }
    JSONSL_XERR;
#undef X
    return "UNKNOWN";
}
}
