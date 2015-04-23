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
