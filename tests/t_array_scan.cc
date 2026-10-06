/*
 *     Copyright 2026-Present Couchbase, Inc.
 *
 *   Use of this software is governed by the Business Source License included
 *   in the file licenses/BSL-Couchbase.txt.  As of the Change Date specified
 *   in that file, in accordance with the Business Source License, use of this
 *   software will be governed by the Apache License, Version 2.0, included in
 *   the file licenses/APL2.txt.
 */

#include "subdoc/array_scan.h"
#include "subdoc/match.h"
#include "subdoc/path.h"
#include <gtest/gtest.h>
#include <random>
#include <string>
#include <utility>
#include <vector>

using Subdoc::ArrayScanResult;
using Subdoc::Match;
using Subdoc::Path;
using Subdoc::scan_array;
using ScanStatus = ArrayScanResult::Status;

namespace {

/**
 * Scan the array at the beginning of the document (with trailing data to
 * verify that the scan stops at the right place) and verify that the
 * located array is the expected one.
 */
void expectArray(std::string_view array, size_t elements) {
    const std::string doc = std::string(array) + R"(,"next":[1,2]})";
    const auto result = scan_array(doc, {});
    ASSERT_EQ(ScanStatus::Success, result.status) << array;
    EXPECT_EQ(array.size(), result.length) << array;
    EXPECT_EQ(elements, result.num_elements) << array;
}

} // namespace

TEST(ArrayScanTest, Empty) {
    expectArray("[]", 0);
    expectArray("[ \t\r\n ]", 0);
}

TEST(ArrayScanTest, Primitives) {
    expectArray("[1]", 1);
    expectArray("[1,2,3]", 3);
    expectArray(R"([ 1 , "two" , true , null , -1.5e10 ])", 5);
}

TEST(ArrayScanTest, Strings) {
    // Brackets, braces and commas inside strings must be ignored
    expectArray(R"(["]","[","}","{",","])", 5);
    // Escaped quotes
    expectArray(R"(["\"]", "a\"b\"c"])", 2);
    // Escaped backslash immediately before the closing quote
    expectArray(R"(["\\", "\\\\", "\\\"]"])", 3);
    // Unicode escapes
    expectArray(R"(["\u005d", "\u0022"])", 2);
}

TEST(ArrayScanTest, Nested) {
    expectArray(R"([[1,2],[3,[4,[5]]],{"a":[1,{"b":"]"}]}])", 3);
    expectArray(R"([{}, [], {"k":"v"}])", 3);
}

TEST(ArrayScanTest, Malformed) {
    EXPECT_EQ(ScanStatus::Malformed, scan_array("", {}).status);
    EXPECT_EQ(ScanStatus::Malformed, scan_array("{}", {}).status);
    EXPECT_EQ(ScanStatus::Malformed, scan_array("[", {}).status);
    EXPECT_EQ(ScanStatus::Malformed, scan_array("[1,2", {}).status);
    EXPECT_EQ(ScanStatus::Malformed, scan_array(R"(["abc])", {}).status);
    EXPECT_EQ(ScanStatus::Malformed, scan_array("[\"abc\\\"]", {}).status);
    EXPECT_EQ(ScanStatus::Malformed, scan_array("[[1,2]", {}).status);
    EXPECT_EQ(ScanStatus::Malformed, scan_array("[1}", {}).status);
}

TEST(ArrayScanTest, NeedleFound) {
    EXPECT_EQ(ScanStatus::Found, scan_array("[1,2,3]", "1").status);
    EXPECT_EQ(ScanStatus::Found, scan_array("[1,2,3]", "3").status);
    EXPECT_EQ(ScanStatus::Found, scan_array("[ 1 , 2 , 3 ]", "2").status);
    EXPECT_EQ(ScanStatus::Found, scan_array(R"(["a","b"])", R"("b")").status);
    EXPECT_EQ(ScanStatus::Found,
              scan_array("[1,\"a\\\"b\"]", "\"a\\\"b\"").status);
    EXPECT_EQ(ScanStatus::Found, scan_array("[null,true]", "true").status);
    // Found before the container
    EXPECT_EQ(ScanStatus::Found, scan_array("[1,[2]]", "1").status);
}

TEST(ArrayScanTest, NeedleNotFound) {
    auto result = scan_array("[1,2,3]", "4");
    EXPECT_EQ(ScanStatus::Success, result.status);
    EXPECT_EQ(7, result.length);
    EXPECT_EQ(3, result.num_elements);

    // Prefix/suffix of an element is not a match
    EXPECT_EQ(ScanStatus::Success, scan_array("[12,21]", "1").status);
    // The string and the number differ
    EXPECT_EQ(ScanStatus::Success, scan_array("[1]", R"("1")").status);
    EXPECT_EQ(ScanStatus::Success, scan_array(R"(["1"])", "1").status);
    // Same length, but the number is not the content of the string
    EXPECT_EQ(ScanStatus::Success, scan_array(R"(["23"])", "1234").status);
    // The needle inside a nested string doesn't count
    EXPECT_EQ(ScanStatus::Success,
              scan_array("[\"x,\\\"a\\\"\"]", R"("a")").status);
    EXPECT_EQ(ScanStatus::Success, scan_array("[]", "1").status);
}

TEST(ArrayScanTest, NeedleNonPrimitive) {
    EXPECT_EQ(ScanStatus::NonPrimitive, scan_array("[[1]]", "1").status);
    EXPECT_EQ(ScanStatus::NonPrimitive,
              scan_array(R"([0,{"a":1},1])", "1").status);
}

TEST(ArrayScanTest, BlockBoundaries) {
    // Place strings with escapes, brackets and the end of the array at
    // every offset relative to the 64 byte blocks used by the scanner
    const std::string escaped = R"("a\\\"],\\\\")";
    for (size_t pad = 0; pad < 130; ++pad) {
        const std::string array = "[" + std::string(pad, ' ') + "3," + escaped +
                                  ",\"" + std::string(pad, 'x') +
                                  R"(",{"b":"]"},"\\",[1,[2]]])";
        expectArray(array, 6);
        EXPECT_EQ(ScanStatus::Found, scan_array(array, "3").status) << array;
        EXPECT_EQ(ScanStatus::Found, scan_array(array, escaped).status)
                << array;
        EXPECT_EQ(ScanStatus::NonPrimitive, scan_array(array, "4").status)
                << array;
        EXPECT_EQ(ScanStatus::NonPrimitive,
                  scan_array(array, "\"\\\\\"").status)
                << array;
    }
}

namespace {

/** Generate a random JSON value (biased towards nasty strings) */
void randomScanValue(std::mt19937& gen, std::string& out, int depth) {
    std::uniform_int_distribution<int> kind(0, depth > 3 ? 3 : 5);
    std::uniform_int_distribution<int> len(0, 80);
    static const std::string_view alphabet = R"(ab[]{},:" \)";
    switch (kind(gen)) {
    case 0:
        out.append(std::to_string(gen() % 100000));
        return;
    case 1:
        out.append(gen() % 2 ? "true" : "null");
        return;
    case 2:
    case 3: {
        out.push_back('"');
        const int n = len(gen);
        for (int ii = 0; ii < n; ++ii) {
            const char ch = alphabet[gen() % alphabet.size()];
            if (ch == '"' || ch == '\\') {
                out.push_back('\\');
            }
            out.push_back(ch);
        }
        out.push_back('"');
        return;
    }
    case 4: {
        out.push_back('[');
        const int n = len(gen) % 6;
        for (int ii = 0; ii < n; ++ii) {
            if (ii) {
                out.append(gen() % 4 ? "," : " , ");
            }
            randomScanValue(gen, out, depth + 1);
        }
        out.push_back(']');
        return;
    }
    default: {
        out.push_back('{');
        const int n = len(gen) % 6;
        for (int ii = 0; ii < n; ++ii) {
            if (ii) {
                out.push_back(',');
            }
            out.append("\"k" + std::to_string(ii) + "\":");
            randomScanValue(gen, out, depth + 1);
        }
        out.push_back('}');
        return;
    }
    }
}

} // namespace

/**
 * Verify that matching an array with skip_array_contents (using
 * scan_array()) gives the same result as letting jsonsl parse it.
 */
TEST(ArrayScanTest, RandomDocumentsMatchJsonsl) {
    std::mt19937 gen(0xc0ffee);
    auto* jsn = Match::jsn_alloc();
    Path path;
    ASSERT_EQ(0, path.parse("arr"));
    for (int iteration = 0; iteration < 2000; ++iteration) {
        std::string doc = R"({"before":)";
        randomScanValue(gen, doc, 0);
        doc.append(R"(,"arr":[)");
        const int elements = int(gen() % 40);
        for (int ii = 0; ii < elements; ++ii) {
            if (ii) {
                doc.push_back(',');
            }
            randomScanValue(gen, doc, 1);
        }
        doc.append(R"(],"after":)");
        randomScanValue(gen, doc, 0);
        doc.push_back('}');

        Match parsed;
        parsed.exec_match(doc, path, jsn);
        Match scanned;
        scanned.skip_array_contents = true;
        scanned.exec_match(doc, path, jsn);

        ASSERT_EQ(JSONSL_ERROR_SUCCESS, parsed.status) << doc;
        ASSERT_EQ(JSONSL_MATCH_COMPLETE, parsed.matchres) << doc;
        ASSERT_EQ(parsed.status, scanned.status) << doc;
        ASSERT_EQ(parsed.matchres, scanned.matchres) << doc;
        ASSERT_EQ(parsed.loc_deepest.to_string(),
                  scanned.loc_deepest.to_string())
                << doc;
        ASSERT_EQ(parsed.num_children, scanned.num_children) << doc;
        ASSERT_EQ(parsed.immediate_parent_found, scanned.immediate_parent_found)
                << doc;
    }
    Match::jsn_free(jsn);
}

namespace {

/** Run filter_array() and return the status and the survivors */
std::pair<ScanStatus, std::string> filter(std::string_view array,
                                          std::string_view needle,
                                          bool remove_all) {
    std::string survivors;
    const auto result =
            Subdoc::filter_array(array, needle, remove_all, survivors);
    return {result.status, survivors};
}

} // namespace

TEST(ArrayScanTest, FilterFirst) {
    EXPECT_EQ(std::make_pair(ScanStatus::Success, std::string("1,3")),
              filter("[1,2,3]", "2", false));
    EXPECT_EQ(
            std::make_pair(ScanStatus::Success, std::string(R"("a",1,"a",1)")),
            filter(R"([1,"a",1,"a",1])", "1", false));
    EXPECT_EQ(std::make_pair(ScanStatus::Success, std::string()),
              filter(R"(["only"])", R"("only")", false));
    EXPECT_EQ(std::make_pair(ScanStatus::Success, std::string("1,2,3")),
              filter("[1,2,3]", "99", false));
    EXPECT_EQ(std::make_pair(ScanStatus::Success, std::string()),
              filter("[]", "1", false));
}

TEST(ArrayScanTest, FilterAll) {
    EXPECT_EQ(std::make_pair(ScanStatus::Success, std::string(R"("a","a")")),
              filter(R"([1,"a",1,"a",1])", "1", true));
    EXPECT_EQ(std::make_pair(ScanStatus::Success, std::string()),
              filter("[5,5,5]", "5", true));
    EXPECT_EQ(std::make_pair(ScanStatus::Success, std::string("2,3,4")),
              filter("[1,2,3,1,4,1]", "1", true));
}

TEST(ArrayScanTest, FilterCountsRemoved) {
    std::string survivors;
    auto result = Subdoc::filter_array("[1,2,1,3,1]", "1", true, survivors);
    EXPECT_EQ(ScanStatus::Success, result.status);
    EXPECT_EQ(3, result.num_removed);
    EXPECT_EQ(5, result.num_elements);
    EXPECT_EQ(11, result.length);

    survivors.clear();
    result = Subdoc::filter_array("[1,2,1,3,1]", "1", false, survivors);
    EXPECT_EQ(1, result.num_removed);

    survivors.clear();
    result = Subdoc::filter_array("[1,2,1,3,1]", "4", true, survivors);
    EXPECT_EQ(0, result.num_removed);
}

TEST(ArrayScanTest, FilterWhitespace) {
    // Whitespace around the removed elements is dropped, and whitespace
    // between consecutive survivors is kept
    EXPECT_EQ(std::make_pair(ScanStatus::Success, std::string("1 , 2,3 ,\n4")),
              filter("[ 1 , 2 , 9 , 3 ,\n4 ]", "9", false));
}

TEST(ArrayScanTest, FilterStrings) {
    EXPECT_EQ(std::make_pair(ScanStatus::Success,
                             std::string("\"]\",\"a\\\"b\",\",\"")),
              filter("[\"]\",\"x\",\"a\\\"b\",\"x\",\",\"]", R"("x")", true));
    // The full text of the element is compared
    EXPECT_EQ(std::make_pair(ScanStatus::Success, std::string(R"("23")")),
              filter(R"(["23"])", "1234", true));
}

TEST(ArrayScanTest, FilterNonPrimitive) {
    // A container anywhere in the array is an error, even after the
    // element to remove is found
    EXPECT_EQ(ScanStatus::NonPrimitive, filter("[1,[2],3]", "1", false).first);
    EXPECT_EQ(ScanStatus::NonPrimitive, filter("[1,2,{}]", "1", false).first);
    EXPECT_EQ(ScanStatus::NonPrimitive, filter("[[1],1]", "9", true).first);
}

TEST(ArrayScanTest, FilterMalformed) {
    EXPECT_EQ(ScanStatus::Malformed, filter("[1,2", "1", false).first);
    EXPECT_EQ(ScanStatus::Malformed, filter(R"([1,"2])", "1", true).first);
}

/**
 * Verify filter_array() against a trivial implementation on random arrays
 * of primitives (which also cross the 64 byte block boundaries)
 */
TEST(ArrayScanTest, FilterRandom) {
    std::mt19937 gen(0xfeed);
    for (int iteration = 0; iteration < 2000; ++iteration) {
        std::vector<std::string> elements;
        const int count = int(gen() % 60);
        for (int ii = 0; ii < count; ++ii) {
            // A small value space ensures plenty of duplicates
            switch (gen() % 4) {
            case 0:
                elements.emplace_back(std::to_string(gen() % 5));
                break;
            case 1:
                elements.emplace_back(R"("x\"],\\)" +
                                      std::to_string(gen() % 5) + "\"");
                break;
            case 2:
                elements.emplace_back(gen() % 70, 'y');
                elements.back() = "\"" + elements.back() + "\"";
                break;
            default:
                elements.emplace_back(gen() % 2 ? "true" : "null");
                break;
            }
        }
        auto join = [](const std::vector<std::string>& values) {
            std::string ret;
            for (const auto& value : values) {
                if (!ret.empty()) {
                    ret.push_back(',');
                }
                ret.append(value);
            }
            return ret;
        };
        const std::string array = "[" + join(elements) + "]";
        const std::string needle = elements.empty()
                                           ? std::string("0")
                                           : elements[gen() % elements.size()];

        for (const bool remove_all : {false, true}) {
            std::vector<std::string> expected;
            bool removed = false;
            for (const auto& element : elements) {
                if (element == needle && (remove_all || !removed)) {
                    removed = true;
                } else {
                    expected.emplace_back(element);
                }
            }
            ASSERT_EQ(std::make_pair(ScanStatus::Success, join(expected)),
                      filter(array + R"(,{"a":[1]}])", needle, remove_all))
                    << array << " needle:" << needle;
        }
    }
}

// The depth is limited as by a parser which needs a level for every value:
// containers at the maximum depth must be empty
TEST(ArrayScanTest, DepthLimit) {
    EXPECT_EQ(ScanStatus::Success, scan_array("[[[]]]", {}, 3).status);
    EXPECT_EQ(ScanStatus::Success, scan_array("[[[ ]],[{}]]", {}, 3).status);
    EXPECT_EQ(ScanStatus::TooDeep, scan_array("[[[]]]", {}, 2).status);
    EXPECT_EQ(ScanStatus::TooDeep, scan_array("[[[1]]]", {}, 3).status);
    EXPECT_EQ(ScanStatus::TooDeep, scan_array(R"([[{"a":1}]])", {}, 3).status);
    EXPECT_EQ(ScanStatus::Success, scan_array("[[[1]]]", {}, 4).status);
    EXPECT_EQ(ScanStatus::TooDeep, scan_array("[1]", {}, 1).status);
    EXPECT_EQ(ScanStatus::Success, scan_array("[]", {}, 1).status);
    EXPECT_EQ(ScanStatus::TooDeep, scan_array("[]", {}, 0).status);
    // Brackets in strings don't count
    EXPECT_EQ(ScanStatus::Success,
              scan_array(R"(["[[[[",["{{"]])", {}, 3).status);

    // A non-primitive element is reported once it has been scanned, so a
    // depth error in it takes precedence (as with a parser)
    EXPECT_EQ(ScanStatus::NonPrimitive,
              scan_array("[1,[[2]],3]", "3", 4).status);
    EXPECT_EQ(ScanStatus::TooDeep, scan_array("[1,[[2]],3]", "3", 3).status);
    // ... but not over a match found before it
    EXPECT_EQ(ScanStatus::Found, scan_array("[3,[[2]]]", "3", 2).status);
}
