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

TEST_F(UescapeTests, testSurrogateInterruptedByLiteral) {
    // A high surrogate followed by ordinary literal text before the
    // low surrogate arrives must not leave the pairing state pending:
    // otherwise a later, unrelated low surrogate would be silently
    // combined with the stale high surrogate instead of erroring.
    std::string in("\\uD800junk\\uDC00"), out;
    auto rv = UescapeConverter::convert(in, out);
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

TEST_F(UescapeTests, testMultiByteUtf8AndEdgeCases) {
    std::string out;

    // 2-byte UTF-8 sequence (0x80 <= codepoint < 0x800): U+00E9 ('é')
    auto rv = UescapeConverter::convert("caf\\u00e9", out);
    ASSERT_TRUE(rv);
    EXPECT_EQ("caf\xc3\xa9", out);

    // Boundary 2-byte UTF-8 sequences: U+0080 and U+07FF
    out.clear();
    rv = UescapeConverter::convert("\\u0080\\u07FF", out);
    ASSERT_TRUE(rv);
    EXPECT_EQ("\xc2\x80\xdf\xbf", out);

    // 3-byte UTF-8 sequence (0x800 <= codepoint < 0x10000): U+20AC ('€')
    out.clear();
    rv = UescapeConverter::convert("price:\\u20AC", out);
    ASSERT_TRUE(rv);
    EXPECT_EQ("price:\xe2\x82\xac", out);

    // Boundary 3-byte UTF-8 sequences: U+0800 and U+FFFF
    out.clear();
    rv = UescapeConverter::convert("\\u0800\\uFFFF", out);
    ASSERT_TRUE(rv);
    EXPECT_EQ("\xe0\xa0\x80\xef\xbf\xbf", out);

    // Lone low surrogate (0xDC00..0xDFFF without a preceding high surrogate)
    out.clear();
    rv = UescapeConverter::convert("\\uDC00", out);
    ASSERT_FALSE(rv);
    EXPECT_EQ(Status::INVALID_CODEPOINT, rv.code());

    // High surrogate followed by a codepoint > 0xDFFF
    out.clear();
    rv = UescapeConverter::convert("\\uD800\\uE000", out);
    ASSERT_FALSE(rv);
    EXPECT_EQ(Status::INVALID_SURROGATE, rv.code());

    // High surrogate followed by a non-\u escape
    out.clear();
    rv = UescapeConverter::convert("\\uD800\\n", out);
    ASSERT_FALSE(rv);
    EXPECT_EQ(Status::INVALID_SURROGATE, rv.code());

    // Non-\u escapes and trailing backslash are preserved verbatim
    out.clear();
    rv = UescapeConverter::convert(R"(a\nb\u0041c\)", out);
    ASSERT_TRUE(rv);
    EXPECT_EQ("a\\nbAc\\", out);
}
