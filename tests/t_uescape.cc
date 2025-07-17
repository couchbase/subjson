/*
 *     Copyright 2015-Present Couchbase, Inc.
 *
 *   Use of this software is governed by the Business Source License included
 *   in the file licenses/BSL-Couchbase.txt.  As of the Change Date specified
 *   in that file, in accordance with the Business Source License, use of this
 *   software will be governed by the Apache License, Version 2.0, included in
 *   the file licenses/APL2.txt.
 */

#include "subdoc/uescape.h"
#include <gtest/gtest.h>

using Subdoc::UescapeConverter;
using Status = UescapeConverter::Status;

class UescapeTests : public testing::Test {};

TEST_F(UescapeTests, testBasic) {
    std::string in("nothing here"), out;
    auto rv = UescapeConverter::convert(in, out);
    ASSERT_EQ(in, out);
    ASSERT_TRUE(rv);

    out.clear();
    in = "simple\\u0020space";
    rv = UescapeConverter::convert(in, out);
    ASSERT_TRUE(rv);
    ASSERT_EQ(out, "simple space");

    out.clear();
    in = "\\u0000NUL";
    rv = UescapeConverter::convert(in, out);
    ASSERT_FALSE(rv);
    ASSERT_EQ(Status::EMBEDDED_NUL, rv.code());
}

TEST_F(UescapeTests, testSurrogates) {
    // http://www.fileformat.info/info/unicode/char/1d11e/index.htm
    std::string in("Clef\\uD834\\uDD1E"), out;
    // The way it's encoded in UTF8
    std::string exp("Clef\xf0\x9d\x84\x9e");

    auto rv = UescapeConverter::convert(in, out);
    ASSERT_TRUE(rv);
    ASSERT_EQ(exp, out);

    out.clear();
    in = "Clef\\uD834";
    rv = UescapeConverter::convert(in, out);
    ASSERT_FALSE(rv);
    ASSERT_EQ(Status::INCOMPLETE_SURROGATE, rv.code());

    in = "Clef\\uD834\\u0020";
    out.clear();
    rv = UescapeConverter::convert(in, out);
    ASSERT_FALSE(rv);
    ASSERT_EQ(Status::INVALID_SURROGATE, rv.code());
}

TEST_F(UescapeTests, testInvalidHex) {
    std::string in("\\uTTTT"), out;
    auto rv = UescapeConverter::convert(in, out);
    ASSERT_FALSE(rv);
    ASSERT_EQ(Status::INVALID_HEXCHARS, rv.code());

    for (size_t ii = 1; ii < 4; ++ii) {
        in = "\\u";
        for (size_t jj = 0; jj < ii; jj++) {
            in += '3';
        }
        rv = UescapeConverter::convert(in, out);
        ASSERT_FALSE(rv) << "Fails with uescape of " << ii << " length";
        ASSERT_EQ(Status::INVALID_HEXCHARS, rv.code());
    }
}
