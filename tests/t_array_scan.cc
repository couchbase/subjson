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

/**
 * Scan the array at the beginning of the document (with trailing data)
 * and return the last element as located by the scanner.
 */
std::string lastElement(std::string_view array) {
    const std::string doc = std::string(array) + R"(,"next":[1,2]})";
    const auto result = scan_array(doc, {});
    EXPECT_EQ(ScanStatus::Success, result.status) << array;
    return doc.substr(result.element_offset, result.element_length);
}

} // namespace

TEST(ArrayScanTest, LastElement) {
    EXPECT_EQ("1", lastElement("[1]"));
    EXPECT_EQ("3", lastElement("[1,2,3]"));
    // Surrounding whitespace isn't part of the element
    EXPECT_EQ("-1.5e10", lastElement("[ 1 , true ,\t-1.5e10 \n]"));
    EXPECT_EQ("null", lastElement("[ null ]"));
    // Commas and brackets inside strings and containers are ignored
    EXPECT_EQ("\"a,\\\"],b\"", lastElement("[1,\"a,\\\"],b\"]"));
    EXPECT_EQ(R"({"a":[4,5],"b":","})",
              lastElement(R"([1,[2,3],{"a":[4,5],"b":","}])"));
    EXPECT_EQ("[1,2]", lastElement("[[1,2]]"));
    EXPECT_EQ("[]", lastElement("[1,[]]"));

    // The last comma and the end of the array in different 64 byte blocks
    const std::string escaped = R"("a\\\"],\\\\")";
    for (size_t pad = 0; pad < 130; ++pad) {
        const std::string last = "\"" + std::string(pad, 'x') + "\"";
        EXPECT_EQ(last,
                  lastElement("[" + escaped + "," + std::string(pad, ' ') +
                              last + " ]"))
                << pad;
        EXPECT_EQ(escaped,
                  lastElement("[" + last + "," + std::string(pad, ' ') +
                              escaped + "]"))
                << pad;
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
 * Verify that matching a container (which is located with the scanner)
 * gives the container and the number of children in the generated
 * document, with random values before, in and after it.
 */
TEST(ArrayScanTest, RandomDocumentsMatchContainer) {
    std::mt19937 gen(0xc0ffee);
    auto* jsn = Match::jsn_alloc();
    Path path;
    ASSERT_EQ(0, path.parse("container"));
    for (int iteration = 0; iteration < 4000; ++iteration) {
        // Every other document has an object rather than an array
        const bool object = iteration % 2;
        std::string doc = R"({"before":)";
        randomScanValue(gen, doc, 0);
        doc.append(R"(,"container":)");
        const size_t begin = doc.size();
        doc.push_back(object ? '{' : '[');
        const int elements = int(gen() % 40);
        for (int ii = 0; ii < elements; ++ii) {
            if (ii) {
                doc.push_back(',');
            }
            if (object) {
                doc.append("\"k" + std::to_string(ii) + "\":");
            }
            randomScanValue(gen, doc, 1);
        }
        doc.push_back(object ? '}' : ']');
        const std::string container = doc.substr(begin);
        doc.append(R"(,"after":)");
        randomScanValue(gen, doc, 0);
        doc.push_back('}');

        Match match;
        match.exec_match(doc, path, jsn);
        ASSERT_EQ(JSONSL_ERROR_SUCCESS, match.status) << doc;
        ASSERT_EQ(JSONSL_MATCH_COMPLETE, match.matchres) << doc;
        ASSERT_EQ(object ? JSONSL_T_OBJECT : JSONSL_T_LIST, match.type) << doc;
        ASSERT_EQ(container, match.loc_deepest.to_string()) << doc;
        // jsonsl counts both the keys and the values in an object
        ASSERT_EQ(object ? 2 * elements : elements, int(match.num_children))
                << doc;
        ASSERT_TRUE(match.immediate_parent_found) << doc;
    }
    Match::jsn_free(jsn);
}

TEST(ArrayScanTest, Objects) {
    auto expectObject = [](std::string_view object, size_t members) {
        const std::string doc = std::string(object) + R"(,"next":{"a":1}})";
        const auto result = Subdoc::scan_container(doc);
        ASSERT_EQ(ScanStatus::Success, result.status) << object;
        EXPECT_EQ(object.size(), result.length) << object;
        EXPECT_EQ(members, result.num_elements) << object;
    };
    expectObject("{}", 0);
    expectObject("{ \n }", 0);
    expectObject(R"({"a":1})", 1);
    expectObject(R"({"a":1,"b":[1,2],"c":{"d":"}","e":","}})", 3);
    expectObject(R"({"}":"{", "a\"b":"\\"})", 2);

    // Arrays work as well
    EXPECT_EQ(3, Subdoc::scan_container("[1,[2,3],4]").num_elements);

    EXPECT_EQ(ScanStatus::Malformed,
              Subdoc::scan_container(R"({"a":1)").status);
    EXPECT_EQ(ScanStatus::Malformed,
              Subdoc::scan_container(R"({"a":1])").status);
    EXPECT_EQ(ScanStatus::Malformed, Subdoc::scan_container("1").status);
    // Only arrays may be searched / filtered
    EXPECT_EQ(ScanStatus::Malformed, scan_array(R"({"a":1})", "1").status);
    EXPECT_EQ(ScanStatus::Malformed, scan_array(R"({"a":1})", {}).status);
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

TEST(ArrayScanTest, FindElement) {
    const std::string array = R"([ 1 , "a\"],b" ,[2,[3]],{"x":"]"}, null ])";
    auto element = [&array](size_t index) {
        const auto result = Subdoc::find_array_element(array + ",9]", index);
        EXPECT_EQ(ScanStatus::Found, result.status) << index;
        return std::make_pair(
                array.substr(result.element_offset, result.element_length),
                result.has_next);
    };
    EXPECT_EQ(std::make_pair(std::string("1"), true), element(0));
    EXPECT_EQ(std::make_pair(std::string("\"a\\\"],b\""), true), element(1));
    EXPECT_EQ(std::make_pair(std::string("[2,[3]]"), true), element(2));
    EXPECT_EQ(std::make_pair(std::string(R"({"x":"]"})"), true), element(3));
    EXPECT_EQ(std::make_pair(std::string("null"), false), element(4));

    // Past the end
    auto result = Subdoc::find_array_element(array, 5);
    EXPECT_EQ(ScanStatus::Success, result.status);
    EXPECT_EQ(5, result.num_elements);
    EXPECT_EQ(array.size(), result.length);
    result = Subdoc::find_array_element("[]", 0);
    EXPECT_EQ(ScanStatus::Success, result.status);
    EXPECT_EQ(0, result.num_elements);

    EXPECT_EQ(ScanStatus::Malformed,
              Subdoc::find_array_element("[1,2", 5).status);
    EXPECT_EQ(ScanStatus::Malformed,
              Subdoc::find_array_element(R"({"a":1})", 0).status);
}

/**
 * Verify find_array_element() (and the last element reported by
 * scan_array()) against a trivial implementation on random arrays which
 * cross the 64 byte block boundaries
 */
TEST(ArrayScanTest, FindElementRandom) {
    std::mt19937 gen(0xbeef);
    for (int iteration = 0; iteration < 1000; ++iteration) {
        std::vector<std::string> elements;
        const int count = int(gen() % 50);
        for (int ii = 0; ii < count; ++ii) {
            switch (gen() % 4) {
            case 0:
                elements.emplace_back(std::to_string(gen() % 1000));
                break;
            case 1:
                elements.emplace_back(R"("x\"],\\)" +
                                      std::string(gen() % 70, 'y') + "\"");
                break;
            case 2:
                elements.emplace_back(R"({"a":[1,"]"],"b":{}})");
                break;
            default:
                elements.emplace_back("[[],[" + std::to_string(ii) + "]]");
                break;
            }
        }
        std::string array = "[";
        for (int ii = 0; ii < count; ++ii) {
            if (ii) {
                array.append(gen() % 3 ? "," : " , ");
            }
            array.append(elements[ii]);
        }
        array.append("]");
        const std::string doc = array + R"(,"next":[1,2,3]})";

        for (int ii = 0; ii <= count; ++ii) {
            const auto result = Subdoc::find_array_element(doc, ii);
            if (ii == count) {
                ASSERT_EQ(ScanStatus::Success, result.status) << array;
                ASSERT_EQ(size_t(count), result.num_elements) << array;
                ASSERT_EQ(array.size(), result.length) << array;
            } else {
                ASSERT_EQ(ScanStatus::Found, result.status)
                        << array << " " << ii;
                ASSERT_EQ(elements[ii],
                          doc.substr(result.element_offset,
                                     result.element_length))
                        << array << " " << ii;
                ASSERT_EQ(ii + 1 < count, result.has_next) << array;
            }
        }

        const auto result = scan_array(doc, {});
        ASSERT_EQ(ScanStatus::Success, result.status) << array;
        if (count) {
            ASSERT_EQ(elements.back(),
                      doc.substr(result.element_offset, result.element_length))
                    << array;
        }
    }
}

TEST(ArrayScanTest, FindStringEnd) {
    using Subdoc::find_string_end;
    constexpr auto npos = std::string_view::npos;
    EXPECT_EQ(1, find_string_end("\"\""));
    EXPECT_EQ(4, find_string_end("\"abc\",\"next\""));
    // Escaped quotes and backslashes
    EXPECT_EQ(5, find_string_end("\"a\\\"b\",\"c\""));
    EXPECT_EQ(3, find_string_end("\"\\\\\",\"c\""));
    EXPECT_EQ(8, find_string_end("\"\\\\\\\"x\\\\\"]"));
    // Structural characters in the string
    EXPECT_EQ(10, find_string_end("\"[{,:]} ab\"}"));
    // Unterminated
    EXPECT_EQ(npos, find_string_end("\"abc"));
    EXPECT_EQ(npos, find_string_end("\"abc\\\""));
    EXPECT_EQ(npos, find_string_end(""));
    EXPECT_EQ(npos, find_string_end("abc\""));
}

namespace {

/** Reference implementation of find_string_end() */
size_t referenceStringEnd(std::string_view buffer) {
    for (size_t idx = 1; idx < buffer.size(); ++idx) {
        if (buffer[idx] == '\\') {
            ++idx;
        } else if (buffer[idx] == '"') {
            return idx;
        }
    }
    return std::string_view::npos;
}

} // namespace

TEST(ArrayScanTest, FindStringEndRandom) {
    std::mt19937 gen(4711);
    // Mostly plain characters, so that strings of all lengths are generated
    const std::string alphabet = R"(aaaaaaaaaaaaaaaaaaaaaaa \\"[]{},:)";
    for (int ii = 0; ii < 20000; ++ii) {
        std::string buffer = "\"";
        const auto length = gen() % 300;
        for (size_t jj = 0; jj < length; ++jj) {
            buffer.push_back(alphabet[gen() % alphabet.size()]);
        }
        EXPECT_EQ(referenceStringEnd(buffer), Subdoc::find_string_end(buffer))
                << buffer;
    }
}

TEST(ArrayScanTest, FindStringEndBlockBoundaries) {
    // Put the escapes and the closing quote at every position around the
    // 64 byte block boundaries
    for (size_t length = 0; length < 200; ++length) {
        for (const std::string tail : {"", R"(\\)", R"(\")", R"(\\\")"}) {
            const std::string buffer =
                    "\"" + std::string(length, 'x') + tail + R"(","next")";
            EXPECT_EQ(referenceStringEnd(buffer),
                      Subdoc::find_string_end(buffer))
                    << buffer;
            EXPECT_EQ(length + tail.size() + 1, Subdoc::find_string_end(buffer))
                    << buffer;
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
