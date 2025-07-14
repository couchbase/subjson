/*
Copyright 2015-Present Couchbase, Inc.

Use of this software is governed by the Business Source License included in
the file licenses/BSL-Couchbase.txt.  As of the Change Date specified in that
file, in accordance with the Business Source License, use of this software will
be governed by the Apache License, Version 2.0, included in the file
licenses/APL2.txt.
*/

#include "subdoc-tests-common.h"
#include "subdoc/validate.h"

using std::string;
using Subdoc::Validator;

class ValidateTest : public ::testing::Test {
};

TEST_F(ValidateTest, testPlain) {
    // We're not testing jsonsl itself, but how we return/report validation
    // errors.
    string txt("[\"Hello\"]");
    int rv = Validator::validate(txt, nullptr);
    ASSERT_EQ(0, rv) << "Simple validation works OK";

    txt = "[";
    rv = Validator::validate(txt, nullptr);
    ASSERT_EQ(Validator::EPARTIAL, rv);
}

TEST_F(ValidateTest, testContext) {
    // Tests stuff in various contexts
    string txt("null");
    int rv = Validator::validate(txt, nullptr);
    ASSERT_NE(0, rv) << "Basic mode does not accept primitives (should fail)";

    rv = Validator::validate(txt, nullptr, -1, Validator::PARENT_ARRAY);
    ASSERT_EQ(0, rv) << "primitive in context of array (ok)";

    rv = Validator::validate(txt, nullptr, -1, Validator::PARENT_DICT);
    ASSERT_EQ(0, rv) << "primitive in context of dictionary (ok)";

    txt = "null, null";
    rv = Validator::validate(txt, nullptr, -1, Validator::PARENT_ARRAY);
    ASSERT_EQ(0, rv) << "multiple primitives in array (ok)";

    rv = Validator::validate(txt, nullptr, -1, Validator::PARENT_DICT);
    ASSERT_NE(0, rv) << "multiple primitives in dict (fail)";

    rv = Validator::validate(txt,
                             nullptr,
                             -1,
                             Validator::PARENT_ARRAY | Validator::VALUE_SINGLE);
    ASSERT_NE(0, rv) << "multiple primitives in array with VALUE_SINGLE (fail)";

    txt = "[1,2,3]";
    rv = Validator::validate(txt, nullptr, -1, Validator::PARENT_ARRAY);
    ASSERT_EQ(0, rv) << "container in context of list (ok)";

    rv = Validator::validate(txt, nullptr, -1, Validator::PARENT_DICT);
    ASSERT_EQ(0, rv) << "container in context of dict (ok)";

    txt = "[[[]]]";
    rv = Validator::validate(txt, nullptr, -1, Validator::PARENT_DICT);
    ASSERT_EQ(0, rv);

    rv = Validator::validate(
            txt,
            nullptr,
            -1,
            Validator::PARENT_DICT | Validator::VALUE_PRIMITIVE);
    ASSERT_NE(0, rv) << "container with VALUE_PRIMITIVE (fail)";
}

TEST_F(ValidateTest, testDepth) {
    // Tests depth constraints
    string txt("[[[]]]");
    int rv;

    rv = Validator::validate(txt, nullptr, 3);
    ASSERT_EQ(0, rv);

    rv = Validator::validate(txt, nullptr, 2);
    ASSERT_EQ(Validator::ETOODEEP, rv);

    rv = Validator::validate(txt, nullptr, 3, Validator::PARENT_DICT);
    ASSERT_EQ(0, rv);

    rv = Validator::validate(txt, nullptr, 2, Validator::PARENT_DICT);
    ASSERT_EQ(Validator::ETOODEEP, rv);
}
