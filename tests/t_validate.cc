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

using Subdoc::Validator;

class ValidateTest : public testing::Test {};

TEST_F(ValidateTest, testPlain) {
    // We're not testing jsonsl itself, but how we return/report validation
    // errors.
    std::string txt(R"(["Hello"])");
    int rv = Validator::validate(txt, nullptr);
    ASSERT_EQ(0, rv) << "Simple validation works OK";

    txt = "[";
    rv = Validator::validate(txt, nullptr);
    ASSERT_EQ(Validator::EPARTIAL, rv);
}

TEST_F(ValidateTest, testContext) {
    // Tests stuff in various contexts
    std::string txt("null");
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
    std::string txt("[[[]]]");
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

TEST_F(ValidateTest, testErrorsAndErrstr) {
    // Multiple elements in PARENT_DICT | VALUE_SINGLE mode
    EXPECT_EQ(Validator::EMULTIELEM,
              Validator::validate(
                      R"(1,"k":2)",
                      nullptr,
                      -1,
                      Validator::PARENT_DICT | Validator::VALUE_SINGLE));

    // Non-primitive in PARENT_ARRAY | VALUE_PRIMITIVE mode
    EXPECT_EQ(Validator::ENOTPRIMITIVE,
              Validator::validate(
                      "[1]",
                      nullptr,
                      -1,
                      Validator::PARENT_ARRAY | Validator::VALUE_PRIMITIVE));

    // Invalid parent mode bits
    EXPECT_EQ(JSONSL_ERROR_GENERIC,
              Validator::validate("1", nullptr, -1, 0x04));

    // Reusing an explicit jsonsl_t parser instance
    auto* parser = Subdoc::Match::jsn_alloc();
    EXPECT_EQ(JSONSL_ERROR_SUCCESS, Validator::validate(R"({"a":1})", parser));
    Subdoc::Match::jsn_free(parser);

    // Error string descriptions for jsonsl and Validator-specific error codes
    EXPECT_NE(nullptr, Validator::errstr(JSONSL_ERROR_SUCCESS));
    EXPECT_NE(nullptr, Validator::errstr(JSONSL_ERROR_GENERIC));
    EXPECT_STREQ("Found multiple elements (single expected)",
                 Validator::errstr(Validator::EMULTIELEM));
    EXPECT_STREQ("Found incomplete JSON",
                 Validator::errstr(Validator::EPARTIAL));
    EXPECT_STREQ("Single primitive expected",
                 Validator::errstr(Validator::ENOTPRIMITIVE));
    EXPECT_STREQ("UNKNOWN", Validator::errstr(999));
}
