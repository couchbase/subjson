/*
 *     Copyright 2015-Present Couchbase, Inc.
 *
 *   Use of this software is governed by the Business Source License included
 *   in the file licenses/BSL-Couchbase.txt.  As of the Change Date specified
 *   in that file, in accordance with the Business Source License, use of this
 *   software will be governed by the Apache License, Version 2.0, included in
 *   the file licenses/APL2.txt.
 */
#include "subdoc-tests-common.h"
#include "subdoc/validate.h"
#include <cstring>
#include <memory>

using namespace Subdoc;

class OpTests : public testing::Test {
protected:
    void SetUp() override {
        op.clear();
    }

    Operation op;
    Result res;

    std::string getNewDoc() const;
    auto returnedMatch() const {
        return res.matchloc().to_string();
    }
    void getAssignNewDoc(std::string& newdoc);
    Error runOp(Command, std::string_view path, std::string_view value = {});
};

static ::testing::AssertionResult ensureErrorResult(
        const char* expr, Error got, Error wanted = Error::SUCCESS) {
    using namespace testing;
    if (got != wanted) {
        return AssertionFailure()
               << expr << ": Expected " << wanted << std::hex << wanted << " ("
               << wanted.description() << "), Got " << got << std::hex << got
               << " (" << got.description() << ")";
    }
    return AssertionSuccess();
}
static ::testing::AssertionResult ensureErrorResult2(const char* a,
                                                     const char*,
                                                     Error got,
                                                     Error wanted) {
    return ensureErrorResult(a, got, wanted);
}

#define ASSERT_ERROK(exp) ASSERT_PRED_FORMAT1(ensureErrorResult, exp)

#define ASSERT_ERREQ(exp, err) ASSERT_PRED_FORMAT2(ensureErrorResult2, exp, err)

std::string OpTests::getNewDoc() const {
    std::string ret;
    for (auto ii : res.newdoc()) {
        ret.append(ii.at, ii.length);
    }

    // validate
    int rv = Validator::validate(ret, op.parser());
    if (rv != JSONSL_ERROR_SUCCESS) {
        Util::dump_newdoc(res);
    }
    EXPECT_EQ(JSONSL_ERROR_SUCCESS, rv)
            << Util::jsonerr(static_cast<jsonsl_error_t>(rv));
    return ret;
}

void OpTests::getAssignNewDoc(std::string& newdoc) {
    newdoc = getNewDoc();
    op.set_doc(newdoc);
}

Error OpTests::runOp(Command opcode,
                     const std::string_view path,
                     const std::string_view value) {
    op.clear();
    if (!value.empty()) {
        op.set_value(value.data(), value.length());
    }
    op.set_code(opcode);
    res.clear();
    op.set_result_buf(&res);
    return op.op_exec(path.data(), path.length());
}

#include "big_json.inc.h"
TEST_F(OpTests, testOperations) {
    std::string newdoc;
    op.set_doc(SAMPLE_big_json, strlen(SAMPLE_big_json));
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "name"));
    ASSERT_EQ(R"("Allagash Brewing")", Util::match_match(op.match()));
    ASSERT_EQ(Error::SUCCESS, runOp(Command::EXISTS, "name"));

    Error rv = runOp(Command::REMOVE, "address");
    ASSERT_TRUE(rv.success());

    getAssignNewDoc(newdoc);
    // Should return in KEY_ENOENT
    ASSERT_EQ(Error::PATH_ENOENT, runOp(Command::GET, "address"));

    // Insert something back, maybe :)
    rv = runOp(Command::DICT_ADD, "address", R"("123 Main St.")");
    ASSERT_TRUE(rv.success());

    getAssignNewDoc(newdoc);
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "address"));
    ASSERT_EQ(R"("123 Main St.")", Util::match_match(op.match()));

    // Replace the value now:
    rv = runOp(Command::REPLACE, "address", R"("33 Marginal Rd.")");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(newdoc);
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "address"));
    ASSERT_EQ(R"("33 Marginal Rd.")", Util::match_match(op.match()));

    // Get it back:
    op.set_doc(SAMPLE_big_json, strlen(SAMPLE_big_json));
    // add non-existent path
    rv = runOp(Command::DICT_ADD, "foo.bar.baz", "[1,2,3]");
    ASSERT_EQ(Error::PATH_ENOENT, rv);

    rv = runOp(Command::DICT_ADD_P, "foo.bar.baz", "[1,2,3]");
    ASSERT_TRUE(rv.success());
    // Verify that the complete document can be created successfully and that
    // the validator pass
    (void)getNewDoc();
}

// Mainly checks that we can perform generic DELETE and GET operations
// on array indices
TEST_F(OpTests, testGenericOps) {
    op.set_doc(SAMPLE_big_json, strlen(SAMPLE_big_json));
    Error rv;
    std::string newdoc;

    rv = runOp(Command::REMOVE, "address[0]");
    ASSERT_TRUE(rv.success());

    getAssignNewDoc(newdoc);
    rv = runOp(Command::GET, "address[0]");
    ASSERT_EQ(Error::PATH_ENOENT, rv);

    rv = runOp(
            Command::REPLACE, "address", R"(["500 B St.", "Anytown", "USA"])");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(newdoc);
    rv = runOp(Command::GET, "address[2]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("\"USA\"", Util::match_match(op.match()));

    rv = runOp(Command::REPLACE, "address[1]", R"("Sacramento")");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(newdoc);

    rv = runOp(Command::GET, "address[1]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ(R"("Sacramento")", Util::match_match(op.match()));
}

TEST_F(OpTests, testReplaceArrayDeep) {
    // Create an array at max level.
    std::string deep;
    for (size_t ii = 0; ii < Limits::MAX_COMPONENTS - 1; ii++) {
        deep += "[";
    }
    deep += "1";
    for (size_t ii = 0; ii < Limits::MAX_COMPONENTS - 1; ii++) {
        deep += "]";
    }
    op.set_doc(deep);

    // Sanity check - should be able to access maximum depth.
    std::string one_minus_max_path;
    for (size_t ii = 0; ii < Limits::MAX_COMPONENTS - 2; ii++) {
        one_minus_max_path += "[0]";
    }
    std::string max_path(one_minus_max_path + "[0]");
    Error rv = runOp(Command::GET, one_minus_max_path);
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("[1]", Util::match_match(op.match()));

    // Should be able to replace the array element with a different one.
    rv = runOp(Command::REPLACE, max_path, "2");
    ASSERT_TRUE(rv.success());
    std::string newdoc;
    getAssignNewDoc(newdoc);
    rv = runOp(Command::GET, one_minus_max_path);
    ASSERT_TRUE(rv.success());
    EXPECT_EQ("[2]", Util::match_match(op.match()));

    // Should be able to replace the last level array with a different
    // (larger) one.
    rv = runOp(Command::REPLACE, one_minus_max_path, "[3,4]");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(newdoc);
    rv = runOp(Command::GET, one_minus_max_path);
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("[3,4]", Util::match_match(op.match()));

    // Should not be able to make it any deeper (already at maximum).
    rv = runOp(Command::REPLACE, one_minus_max_path, "[[5]]");
    ASSERT_EQ(Error::VALUE_ETOODEEP, rv);
}

TEST_F(OpTests, testListOps) {
    std::string doc = "{}";
    op.set_doc(doc);

    Error rv = runOp(Command::DICT_UPSERT, "array", "[]");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    // Test append:
    rv = runOp(Command::ARRAY_APPEND, "array", "1");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runOp(Command::GET, "array[0]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("1", Util::match_match(op.match()));

    rv = runOp(Command::ARRAY_PREPEND, "array", "0");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runOp(Command::GET, "array[0]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("0", Util::match_match(op.match()));
    rv = runOp(Command::GET, "array[1]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("1", Util::match_match(op.match()));

    rv = runOp(Command::ARRAY_APPEND, "array", "2");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runOp(Command::GET, "array[2]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("2", Util::match_match(op.match()));

    rv = runOp(Command::ARRAY_APPEND, "array", R"({"foo":"bar"})");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runOp(Command::GET, "array[3].foo");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ(R"("bar")", Util::match_match(op.match()));

    // Test the various POP commands
    rv = runOp(Command::REMOVE, "array[0]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("0", Util::match_match(op.match()));
    getAssignNewDoc(doc);

    rv = runOp(Command::GET, "array[0]");

    rv = runOp(Command::REMOVE, "array[-1]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ(R"({"foo":"bar"})", Util::match_match(op.match()));
    getAssignNewDoc(doc);

    rv = runOp(Command::REMOVE, "array[-1]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("2", Util::match_match(op.match()));

    // Special prepend operations
    doc = "{}";
    op.set_doc(doc);

    // test prepend without _p
    rv = runOp(Command::ARRAY_PREPEND, "array", "123");
    ASSERT_EQ(Error::PATH_ENOENT, rv);

    rv = runOp(Command::ARRAY_PREPEND_P, "array", "123");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    // Now ensure the contents are the same
    rv = runOp(Command::GET, "array[0]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("123", Util::match_match(op.match()));

    // Remove the first element, making it empty
    rv = runOp(Command::REMOVE, "array[0]");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    // Prepend the first element (singleton)
    rv = runOp(Command::ARRAY_PREPEND, "array", "123");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runOp(Command::GET, "array[0]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("123", Util::match_match(op.match()));
}

TEST_F(OpTests, testArrayMultivalue) {
    std::string doc = R"({"array":[4,5,6]})";
    Error rv;
    op.set_doc(doc);

    rv = runOp(Command::ARRAY_PREPEND, "array", "1,2,3");
    ASSERT_TRUE(rv.success()) << rv;
    getAssignNewDoc(doc);

    rv = runOp(Command::GET, "array");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("[1,2,3,4,5,6]", Util::match_match(op.match()));

    rv = runOp(Command::ARRAY_APPEND, "array", "7,8,9");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runOp(Command::GET, "array");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("[1,2,3,4,5,6,7,8,9]", Util::match_match(op.match()));

    rv = runOp(Command::ARRAY_INSERT, "array[3]", "-3,-2,-1");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runOp(Command::GET, "array[4]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("-2", Util::match_match(op.match()));
}

TEST_F(OpTests, testArrayOpsNested) {
    const std::string array(R"([0,[1,[2]],{"key":"val"}])");
    op.set_doc(array);
    Error rv;

    rv = runOp(Command::REMOVE, "[1][1][0]");
    EXPECT_TRUE(rv.success());
    EXPECT_EQ(R"([0,[1,[]],{"key":"val"}])", getNewDoc());

    std::string array2;
    getAssignNewDoc(array2);
    rv = runOp(Command::REMOVE, "[1][1]");
    EXPECT_TRUE(rv.success());
    EXPECT_EQ(R"([0,[1],{"key":"val"}])", getNewDoc());
}

// Toplevel array with two elements.
TEST_F(OpTests, testArrayDelete) {
    // Toplevel array deletions
    const std::string array("[1,2]");
    op.set_doc(array);
    Error rv;

    // Delete beginning element.
    rv = runOp(Command::REMOVE, "[0]");
    EXPECT_TRUE(rv.success());
    EXPECT_EQ("[2]", getNewDoc());

    // Delete end element.
    rv = runOp(Command::REMOVE, "[1]");
    EXPECT_TRUE(rv.success());
    EXPECT_EQ("[1]", getNewDoc());

    // One element array. Delete last (final) element (via [0]).
    const std::string array2("[1]");
    op.set_doc(array2);

    rv = runOp(Command::REMOVE, "[0]");
    EXPECT_TRUE(rv.success());
    EXPECT_EQ("[]", getNewDoc());

    // Delete last element via [-1].
    rv = runOp(Command::REMOVE, "[-1]");
    EXPECT_TRUE(rv.success());
    EXPECT_EQ("[]", getNewDoc());
}

TEST_F(OpTests, testDictDelete) {
    const std::string dict(R"({"0": 1,"1": 2.0})");
    op.set_doc(dict);
    Error rv;

    // Delete element
    rv = runOp(Command::REMOVE, "0");
    EXPECT_TRUE(rv.success());

    // Check it's gone.
    std::string doc;
    getAssignNewDoc(doc);
    rv = runOp(Command::EXISTS, "0");
    ASSERT_EQ(Error::PATH_ENOENT, rv);
}

TEST_F(OpTests, testUnique) {
    std::string json = "{}";
    std::string doc;
    Error rv;

    op.set_doc(json);

    rv = runOp(Command::ARRAY_ADD_UNIQUE_P, "unique", R"("value")");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runOp(Command::ARRAY_ADD_UNIQUE, "unique", R"("value")");
    ASSERT_EQ(Error::DOC_EEXISTS, rv);

    rv = runOp(Command::ARRAY_ADD_UNIQUE, "unique", "1");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runOp(Command::ARRAY_ADD_UNIQUE, "unique", R"("1")");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runOp(Command::ARRAY_ADD_UNIQUE, "unique", "[]");
    ASSERT_EQ(Error::VALUE_CANTINSERT, rv) << "Cannot unique-add non-primitive";

    rv = runOp(Command::ARRAY_ADD_UNIQUE, "unique", "1,2,3");
    ASSERT_EQ(Error::VALUE_CANTINSERT, rv) << "Cannot unique-add multivalue";

    rv = runOp(Command::ARRAY_APPEND, "unique", "[]");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runOp(Command::ARRAY_ADD_UNIQUE, "unique", "2");
    ASSERT_EQ(Error::PATH_MISMATCH, rv)
            << "Mismatch with array containing non-primitive elements";
}

TEST_F(OpTests, testUniqueComparesWholeElement) {
    // A non-string value with the same length as a string element in the
    // array must not be considered equal to it just because the value
    // without its first and last character matches the content of the
    // string (1234 vs "23")
    std::string json = R"({"a":["23"]})";
    std::string doc;
    op.set_doc(json);
    ASSERT_EQ(Error::SUCCESS, runOp(Command::ARRAY_ADD_UNIQUE, "a", "1234"));
    getAssignNewDoc(doc);
    EXPECT_EQ(R"({"a":["23",1234]})", doc);
    EXPECT_EQ(Error::DOC_EEXISTS,
              runOp(Command::ARRAY_ADD_UNIQUE, "a", R"("23")"));
    EXPECT_EQ(Error::DOC_EEXISTS,
              runOp(Command::ARRAY_ADD_UNIQUE, "a", "1234"));
}

TEST_F(OpTests, testArraySearchIgnoresWhitespaceAroundValue) {
    // Whitespace around the value is accepted by the validation, so it
    // must not affect the comparison with the elements in the array
    const std::string json = R"({"a":[1,2]})";
    op.set_doc(json);
    EXPECT_EQ(Error::DOC_EEXISTS, runOp(Command::ARRAY_ADD_UNIQUE, "a", " 1 "));
    EXPECT_EQ(Error::SUCCESS, runOp(Command::ARRAY_INDEX_OF, "a", " 2"));
    EXPECT_EQ("1", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::ARRAY_REMOVE_FIRST, "a", "1 "));
    EXPECT_EQ(R"({"a":[2]})", getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::ARRAY_REMOVE_ALL, "a", "\t2\n"));
    EXPECT_EQ(R"({"a":[1]})", getNewDoc());
}

// The scanner used by append, add unique, index of and remove by value
// reports the same depth errors as jsonsl (which needs a level for every
// value)
TEST_F(OpTests, AppendUniqueDepthLimit) {
    for (size_t depth = Limits::PARSER_DEPTH - 6;
         depth <= Limits::PARSER_DEPTH + 2;
         ++depth) {
        for (const std::string inner : {"", "1", R"("s")", "[]", "{}"}) {
            const std::string json = R"({"a":[1,)" + std::string(depth, '[') +
                                     inner + std::string(depth, ']') + "]}";
            op.set_doc(json);
            // The levels used by the root object, "a", the arrays and the
            // innermost value
            const size_t levels = 2 + depth + (inner.empty() ? 0 : 1);
            const bool tooDeep = levels >= Limits::PARSER_DEPTH;
            const auto message =
                    "depth: " + std::to_string(depth) + " inner: " + inner;
            EXPECT_EQ(tooDeep ? Error::DOC_ETOODEEP : Error::SUCCESS,
                      runOp(Command::ARRAY_APPEND, "a", "2"))
                    << message;
            // The array has a non-primitive element
            EXPECT_EQ(tooDeep ? Error::DOC_ETOODEEP : Error::PATH_MISMATCH,
                      runOp(Command::ARRAY_ADD_UNIQUE, "a", "2"))
                    << message;
            EXPECT_EQ(tooDeep ? Error::DOC_ETOODEEP : Error::PATH_MISMATCH,
                      runOp(Command::ARRAY_INDEX_OF, "a", "2"))
                    << message;
            // A match before the deep element is found first
            EXPECT_EQ(Error::DOC_EEXISTS,
                      runOp(Command::ARRAY_ADD_UNIQUE, "a", "1"))
                    << message;
            // Remove by value scans the whole array
            EXPECT_EQ(tooDeep ? Error::DOC_ETOODEEP : Error::PATH_MISMATCH,
                      runOp(Command::ARRAY_REMOVE_FIRST, "a", "1"))
                    << message;
            EXPECT_EQ(tooDeep ? Error::DOC_ETOODEEP : Error::PATH_MISMATCH,
                      runOp(Command::ARRAY_REMOVE_ALL, "a", "1"))
                    << message;
        }
    }
}

TEST_F(OpTests, testUniqueToplevel) {
    std::string json("[]");
    std::string doc;
    Error rv;

    op.set_doc(json);

    rv = runOp(Command::ARRAY_ADD_UNIQUE_P, "", "0");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runOp(Command::ARRAY_ADD_UNIQUE_P, "", "0");
    ASSERT_EQ(Error::DOC_EEXISTS, rv);
}

TEST_F(OpTests, testArrayRemoveFirst) {
    Error rv;

    // Basic: single match, gets removed.
    std::string json = "[1,2,3]";
    op.set_doc(json);
    rv = runOp(Command::ARRAY_REMOVE_FIRST, "", "2");
    ASSERT_TRUE(rv.success());
    EXPECT_EQ("[1,3]", getNewDoc());

    // Multiple matches: only the first is removed, the rest survive
    // untouched (and in their original order).
    std::string dup = R"([1,"a",1,"a",1])";
    op.set_doc(dup);
    rv = runOp(Command::ARRAY_REMOVE_FIRST, "", "1");
    ASSERT_TRUE(rv.success());
    EXPECT_EQ(R"(["a",1,"a",1])", getNewDoc());

    // Not found.
    std::string nomatch = "[1,2,3]";
    op.set_doc(nomatch);
    rv = runOp(Command::ARRAY_REMOVE_FIRST, "", "99");
    ASSERT_EQ(Error::VALUE_ENOENT, rv);

    // Only element removed -> empty array.
    std::string single = R"(["only"])";
    op.set_doc(single);
    rv = runOp(Command::ARRAY_REMOVE_FIRST, "", R"("only")");
    ASSERT_TRUE(rv.success());
    EXPECT_EQ("[]", getNewDoc());

    // Empty array -> not found.
    std::string empty = "[]";
    op.set_doc(empty);
    rv = runOp(Command::ARRAY_REMOVE_FIRST, "", "1");
    ASSERT_EQ(Error::VALUE_ENOENT, rv);

    // Non-primitive value to search for -> VALUE_CANTINSERT.
    std::string arr = "[1,2,3]";
    op.set_doc(arr);
    rv = runOp(Command::ARRAY_REMOVE_FIRST, "", "[]");
    ASSERT_EQ(Error::VALUE_CANTINSERT, rv);

    // Array containing a non-primitive element -> PATH_MISMATCH.
    std::string nested = "[1,[2],3]";
    op.set_doc(nested);
    rv = runOp(Command::ARRAY_REMOVE_FIRST, "", "3");
    ASSERT_EQ(Error::PATH_MISMATCH, rv);

    // Path doesn't resolve to a list -> PATH_MISMATCH.
    std::string dict = R"({"a":1})";
    op.set_doc(dict);
    rv = runOp(Command::ARRAY_REMOVE_FIRST, "", "1");
    ASSERT_EQ(Error::PATH_MISMATCH, rv);

    // Nested path pointing at an array.
    std::string nestedArr = R"({"list":[1,2,3]})";
    op.set_doc(nestedArr);
    rv = runOp(Command::ARRAY_REMOVE_FIRST, "list", "2");
    ASSERT_TRUE(rv.success());
    EXPECT_EQ(R"({"list":[1,3]})", getNewDoc());

    // Path to the array doesn't exist -> PATH_ENOENT (as opposed to
    // VALUE_ENOENT when the array exists but lacks the value).
    op.set_doc(nestedArr);
    rv = runOp(Command::ARRAY_REMOVE_FIRST, "nosuchlist", "2");
    ASSERT_EQ(Error::PATH_ENOENT, rv);
}

TEST_F(OpTests, testArrayRemoveAll) {
    Error rv;

    // Removes every matching element, preserving order of the rest.
    std::string dup = R"([1,"a",1,"a",1])";
    op.set_doc(dup);
    rv = runOp(Command::ARRAY_REMOVE_ALL, "", "1");
    ASSERT_TRUE(rv.success());
    EXPECT_EQ(R"(["a","a"])", getNewDoc());

    // Removing every element leaves an empty array.
    std::string same = "[5,5,5]";
    op.set_doc(same);
    rv = runOp(Command::ARRAY_REMOVE_ALL, "", "5");
    ASSERT_TRUE(rv.success());
    EXPECT_EQ("[]", getNewDoc());

    // Not found.
    std::string nomatch = "[1,2,3]";
    op.set_doc(nomatch);
    rv = runOp(Command::ARRAY_REMOVE_ALL, "", "99");
    ASSERT_EQ(Error::VALUE_ENOENT, rv);

    // Non-numeric/non-string primitives (bool/null) compare correctly.
    std::string mixed = "[true,false,null,true,1]";
    op.set_doc(mixed);
    rv = runOp(Command::ARRAY_REMOVE_ALL, "", "true");
    ASSERT_TRUE(rv.success());
    EXPECT_EQ("[false,null,1]", getNewDoc());

    // Array containing a non-primitive element -> PATH_MISMATCH, even
    // though a primitive sibling preceding it matched.
    std::string nested = "[1,[2],3]";
    op.set_doc(nested);
    rv = runOp(Command::ARRAY_REMOVE_ALL, "", "1");
    ASSERT_EQ(Error::PATH_MISMATCH, rv);

    // ... and also when none of the primitive siblings match.
    op.set_doc(nested);
    rv = runOp(Command::ARRAY_REMOVE_ALL, "", "99");
    ASSERT_EQ(Error::PATH_MISMATCH, rv);
}

TEST_F(OpTests, testArrayRemoveComparesWholeElement) {
    // A non-string value with the same length as a string element in the
    // array must not be considered equal to it just because the value
    // without its first and last character matches the content of the
    // string (1234 vs "23")
    std::string json = R"(["23",1234,"23"])";
    op.set_doc(json);
    ASSERT_ERROK(runOp(Command::ARRAY_REMOVE_FIRST, "", "1234"));
    EXPECT_EQ(R"(["23","23"])", getNewDoc());

    op.set_doc(json);
    ASSERT_ERROK(runOp(Command::ARRAY_REMOVE_ALL, "", "1234"));
    EXPECT_EQ(R"(["23","23"])", getNewDoc());

    op.set_doc(json);
    ASSERT_ERREQ(runOp(Command::ARRAY_REMOVE_ALL, "", R"("23")"),
                 Error::SUCCESS);
    EXPECT_EQ("[1234]", getNewDoc());

    std::string onlyString = R"(["23"])";
    op.set_doc(onlyString);
    ASSERT_ERREQ(runOp(Command::ARRAY_REMOVE_FIRST, "", "1234"),
                 Error::VALUE_ENOENT);
}

TEST_F(OpTests, testArrayRemoveAllManyMatches) {
    // Result::m_newdoc has a fixed 8-slot capacity, but
    // do_array_remove_value() never needs more than 3 of those slots
    // regardless of match count: survivors are concatenated into a
    // dynamically-growing buffer, not spliced in as one segment per
    // removed element. Exercise well past 8 matches to cover that.
    std::string doc = "[";
    for (int ii = 0; ii < 20; ++ii) {
        if (ii) {
            doc += ",";
        }
        doc += (ii % 2 == 0) ? "1" : "2";
    }
    doc += "]";
    op.set_doc(doc);

    Error rv = runOp(Command::ARRAY_REMOVE_ALL, "", "1");
    ASSERT_TRUE(rv.success());
    std::string expected = "[";
    for (int ii = 0; ii < 10; ++ii) {
        if (ii) {
            expected += ",";
        }
        expected += "2";
    }
    expected += "]";
    EXPECT_EQ(expected, getNewDoc());
}

TEST_F(OpTests, testArrayIndexOf) {
    // Found: index of the element is returned.
    std::string json = "[1,2,3]";
    op.set_doc(json);
    ASSERT_ERROK(runOp(Command::ARRAY_INDEX_OF, "", "1"));
    ASSERT_EQ("0", returnedMatch());
    ASSERT_ERROK(runOp(Command::ARRAY_INDEX_OF, "", "3"));
    ASSERT_EQ("2", returnedMatch());

    // Multiple matches: the index of the first one is returned.
    std::string dup = R"([1,"a",1,"a",1])";
    op.set_doc(dup);
    ASSERT_ERROK(runOp(Command::ARRAY_INDEX_OF, "", R"("a")"));
    ASSERT_EQ("1", returnedMatch());

    // Not found: VALUE_ENOENT (the array itself does exist).
    op.set_doc(json);
    ASSERT_ERREQ(runOp(Command::ARRAY_INDEX_OF, "", "99"), Error::VALUE_ENOENT);

    // Empty array: VALUE_ENOENT.
    std::string empty = "[]";
    op.set_doc(empty);
    ASSERT_ERREQ(runOp(Command::ARRAY_INDEX_OF, "", "1"), Error::VALUE_ENOENT);

    // Raw text comparison: a string doesn't match the equivalent
    // number, and differently spelled numbers don't match each other.
    std::string mixed = R"(["1",1.0,1])";
    op.set_doc(mixed);
    ASSERT_ERROK(runOp(Command::ARRAY_INDEX_OF, "", "1"));
    ASSERT_EQ("2", returnedMatch());
    ASSERT_ERROK(runOp(Command::ARRAY_INDEX_OF, "", R"("1")"));
    ASSERT_EQ("0", returnedMatch());

    // A non-string value with the same length as a string element must
    // not match it (1234 vs "23")
    std::string sameLength = R"(["23",1234])";
    op.set_doc(sameLength);
    ASSERT_ERROK(runOp(Command::ARRAY_INDEX_OF, "", "1234"));
    ASSERT_EQ("1", returnedMatch());

    // Non-numeric/non-string primitives (bool/null) compare correctly.
    std::string special = "[true,false,null]";
    op.set_doc(special);
    ASSERT_ERROK(runOp(Command::ARRAY_INDEX_OF, "", "null"));
    ASSERT_EQ("2", returnedMatch());
    ASSERT_ERROK(runOp(Command::ARRAY_INDEX_OF, "", "false"));
    ASSERT_EQ("1", returnedMatch());

    // Nested path pointing at an array.
    std::string nestedArr = R"({"list":[1,2,3],"other":[4]})";
    op.set_doc(nestedArr);
    ASSERT_ERROK(runOp(Command::ARRAY_INDEX_OF, "list", "2"));
    ASSERT_EQ("1", returnedMatch());
    ASSERT_ERREQ(runOp(Command::ARRAY_INDEX_OF, "list", "4"),
                 Error::VALUE_ENOENT);

    // Path doesn't exist -> PATH_ENOENT.
    ASSERT_ERREQ(runOp(Command::ARRAY_INDEX_OF, "missing", "1"),
                 Error::PATH_ENOENT);

    // Path doesn't resolve to a list -> PATH_MISMATCH.
    std::string dict = R"({"a":1})";
    op.set_doc(dict);
    ASSERT_ERREQ(runOp(Command::ARRAY_INDEX_OF, "", "1"), Error::PATH_MISMATCH);
    ASSERT_ERREQ(runOp(Command::ARRAY_INDEX_OF, "a", "1"),
                 Error::PATH_MISMATCH);

    // Non-primitive value to search for -> VALUE_CANTINSERT.
    op.set_doc(json);
    ASSERT_ERREQ(runOp(Command::ARRAY_INDEX_OF, "", "[]"),
                 Error::VALUE_CANTINSERT);

    // Array containing a non-primitive element before any match ->
    // PATH_MISMATCH. The scan stops at the first match though, so a
    // match preceding the non-primitive element is still found (as for
    // ARRAY_ADD_UNIQUE).
    std::string nested = "[1,[2],3]";
    op.set_doc(nested);
    ASSERT_ERREQ(runOp(Command::ARRAY_INDEX_OF, "", "3"), Error::PATH_MISMATCH);
    ASSERT_ERROK(runOp(Command::ARRAY_INDEX_OF, "", "1"));
    ASSERT_EQ("0", returnedMatch());

    // The document isn't modified.
    op.set_doc(json);
    ASSERT_ERROK(runOp(Command::ARRAY_INDEX_OF, "", "2"));
    ASSERT_EQ(0U, res.newdoc().size());
}

TEST_F(OpTests, testNumeric) {
    std::string doc = "{}";
    Error rv;
    op.set_doc(doc);

    // Can we make a simple counter?
    rv = runOp(Command::COUNTER_P, "counter", "1");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("1", Util::match_match(op.match()));
    getAssignNewDoc(doc);

    rv = runOp(Command::COUNTER, "counter", "-101");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("-100", Util::match_match(op.match()));
    getAssignNewDoc(doc);

    // Get it raw
    rv = runOp(Command::GET, "counter");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("-100", Util::match_match(op.match()));

    rv = runOp(Command::COUNTER, "counter", "1");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("-99", Util::match_match(op.match()));
    getAssignNewDoc(doc);

    // Try with other things
    std::string dummy = std::to_string(INT64_MAX);
    rv = runOp(Command::COUNTER, "counter", dummy);
    ASSERT_TRUE(rv.success());
    ASSERT_EQ(std::to_string(INT64_MAX - 99), Util::match_match(op.match()));
    getAssignNewDoc(doc);

    dummy = "-" + std::to_string(INT64_MAX - 99);
    rv = runOp(Command::COUNTER, "counter", dummy);
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("0", Util::match_match(op.match()));
    getAssignNewDoc(doc);

    rv = runOp(
            Command::DICT_ADD_P, "counter2", "9999999999999999999999999999999");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runOp(Command::COUNTER, "counter2", "1");
    ASSERT_EQ(Error::NUM_E2BIG, rv);

    rv = runOp(Command::DICT_ADD_P, "counter3", "3.14");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runOp(Command::COUNTER, "counter3", "1");
    ASSERT_EQ(Error::PATH_MISMATCH, rv);

    doc = "[]";
    op.set_doc(doc);
    rv = runOp(Command::COUNTER, "[0]", "42");
    ASSERT_EQ(Error::PATH_ENOENT, rv);

    // Try with a _P variant. Should still be the same
    rv = runOp(Command::COUNTER_P, "[0]", "42");
    ASSERT_EQ(Error::PATH_ENOENT, rv);

    rv = runOp(Command::ARRAY_APPEND, "", "-20");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runOp(Command::COUNTER, "[0]", "1");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("-19", Util::match_match(op.match()));
}

TEST_F(OpTests, testCounterTruncatedBuffer) {
    // do_arith_op() must bound its scan of the matched number to
    // num_loc.length rather than relying on strtoll() to find a
    // non-digit terminator: the document buffer handed to set_doc()
    // is not required to be NUL-terminated, and a document that ends
    // abruptly right after the matched number (no closing '}') must
    // be rejected cleanly, not read past its own allocation.
    // Allocate a buffer to (hopefully) let Asan see a heap-buffer-overflow
    std::string src = R"({"counter":41)";
    // NOLINTNEXTLINE(modernize-avoid-c-arrays)
    auto buf = std::make_unique<char[]>(src.size());
    std::memcpy(buf.get(), src.data(), src.size());
    op.set_doc(buf.get(), src.size());

    Error rv = runOp(Command::COUNTER, "counter", "1");
    ASSERT_EQ(Error::PATH_MISMATCH, rv);
}

TEST_F(OpTests, testMalformedHashKeyEscape) {
    // An object key with an embedded \u0000 escape can't be
    // decoded (UescapeConverter::convert() rejects embedded NULs).
    // The document must be rejected outright rather than silently
    // matching against whatever partial/garbled key text convert()
    // managed to produce before failing.
    std::string doc = R"({"\u0000bad":"x"})";
    op.set_doc(doc);
    Error rv = runOp(Command::GET, "foo");
    ASSERT_EQ(Error::DOC_NOTJSON, rv);
}

TEST_F(OpTests, testResultPushNewdocCapacity) {
    // Result::m_newdoc is a fixed-capacity array; push_newdoc() is the
    // bounds-checked way to append to it (unlike Operation's private
    // newdoc_at(), which every current call site indexes directly
    // with a compile-time-known, in-range index). Confirm it actually
    // enforces that capacity rather than silently overflowing.
    Loc seg("x", 1);
    for (size_t ii = 0; ii < 8; ++ii) {
        ASSERT_TRUE(res.push_newdoc(seg)) << "segment " << ii;
    }
    ASSERT_FALSE(res.push_newdoc(seg));
}

TEST_F(OpTests, MB57177) {
    // Verify that the counter may cover the entire range from min to max
    const auto min = std::to_string(std::numeric_limits<int64_t>::min());
    const auto max = std::to_string(std::numeric_limits<int64_t>::max());
    // The document must outlive the operation (set_doc() only keeps a
    // pointer to it), so don't pass a temporary
    const std::string doc = R"({"min":0, "max":0})";
    op.set_doc(doc);
    auto rv = runOp(Command::COUNTER, "min", min);
    ASSERT_TRUE(rv.success()) << rv.description();
    ASSERT_EQ(min, Util::match_match(op.match()));

    op.set_doc(doc);
    rv = runOp(Command::COUNTER, "max", max);
    ASSERT_TRUE(rv.success()) << rv.description();
    ASSERT_EQ(max, Util::match_match(op.match()));
}

TEST_F(OpTests, testBadNumFormat) {
    std::string doc = "{}";
    op.set_doc(doc);

    ASSERT_EQ(Error::DELTA_EINVAL, runOp(Command::COUNTER_P, "pth", "bad"));
    ASSERT_EQ(Error::DELTA_EINVAL, runOp(Command::COUNTER_P, "pth", "3.14"));
    ASSERT_EQ(Error::DELTA_EINVAL, runOp(Command::COUNTER_P, "pth", "-"));
    ASSERT_EQ(Error::DELTA_EINVAL, runOp(Command::COUNTER_P, "pth", "43f"));
    ASSERT_EQ(Error::DELTA_EINVAL, runOp(Command::COUNTER_P, "pth", "0"));
    ASSERT_EQ(Error::DELTA_EINVAL, runOp(Command::COUNTER_P, "pth", "-0"));
    ASSERT_EQ(Error::DELTA_EINVAL, runOp(Command::COUNTER_P, "pth", "01"));
}

TEST_F(OpTests, testNumericLimits) {
    // Check we can increment from int64_t::max()-1 to max() successfully.
    constexpr int64_t max = std::numeric_limits<int64_t>::max();
    const std::string one_minus_max(R"({"counter":)" + std::to_string(max - 1) +
                                    "}");
    op.set_doc(one_minus_max);

    Error rv = runOp(Command::COUNTER, "counter", "1");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ(std::to_string(max), Util::match_match(op.match()));

    // Incrementing across the limit (max()-1 incremented by 2) should fail.
    op.set_doc(one_minus_max);

    rv = runOp(Command::COUNTER, "counter", "2");
    ASSERT_EQ(Error::DELTA_OVERFLOW, rv);

    // Same for int64_t::min() - 1 and decrement.
    constexpr int64_t min = std::numeric_limits<int64_t>::min();
    const std::string one_plus_min(R"({"counter":)" + std::to_string(min + 1) +
                                   "}");
    op.set_doc(one_plus_min);

    rv = runOp(Command::COUNTER, "counter", "-1");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ(std::to_string(min), Util::match_match(op.match()));

    // Decrementing across the limit (min()-1 decremented by 2) should fail.
    op.set_doc(one_plus_min);

    rv = runOp(Command::COUNTER, "counter", "-2");
    ASSERT_EQ(Error::DELTA_OVERFLOW, rv);
}

TEST_F(OpTests, testValueValidation) {
    std::string doc = "{}";
    Error rv;
    op.set_doc(doc);

    rv = runOp(Command::DICT_ADD_P, "foo.bar.baz", "INVALID");
    ASSERT_EQ(Error::VALUE_CANTINSERT, rv);

    rv = runOp(Command::DICT_ADD_P, "foo.bar.baz", "1,2,3,4");
    ASSERT_EQ(Error::VALUE_CANTINSERT, rv);

    // The value must be a single value (and not inject other keys)
    rv = runOp(Command::DICT_ADD_P, "foo.bar.baz", R"(1,"k2":2)");
    ASSERT_EQ(Error::VALUE_CANTINSERT, rv);

    // Dict key without a colon or value.
    rv = runOp(Command::DICT_ADD, "bad_dict", R"({ "foo" })");
    ASSERT_EQ(Error::VALUE_CANTINSERT, rv);

    rv = runOp(Command::DICT_ADD, "bad_dict", R"({ "foo": })");
    ASSERT_EQ(Error::VALUE_CANTINSERT, rv);

    // Dict without a colon or value.
    rv = runOp(Command::DICT_ADD_P, "bad_dict", R"({ "foo" })");
    EXPECT_EQ(Error::VALUE_CANTINSERT, rv);

    // Dict without a colon.
    rv = runOp(Command::DICT_ADD_P, "bad_dict", R"({ "foo": })");
    EXPECT_EQ(Error::VALUE_CANTINSERT, rv);

    // null with incorrect name.
    rv = runOp(Command::DICT_ADD_P, "bad_null", "nul");
    EXPECT_EQ(Error::VALUE_CANTINSERT, rv);

    // invalid float (more than one decimal point).
    rv = runOp(Command::DICT_ADD_P, "bad_float1", "2.0.0");
    EXPECT_EQ(Error::VALUE_CANTINSERT, rv);

    // invalid float (no digit after the '.').
    rv = runOp(Command::DICT_ADD_P, "bad_float2", "2.");
    EXPECT_EQ(Error::VALUE_CANTINSERT, rv);

    // invalid float (no exponential after the 'e').
    rv = runOp(Command::DICT_ADD_P, "bad_float3", "2.0e");
    EXPECT_EQ(Error::VALUE_CANTINSERT, rv);

    // invalid float (no digits after the exponential sign).
    rv = runOp(Command::DICT_ADD_P, "bad_float4", "2.0e+");
    EXPECT_EQ(Error::VALUE_CANTINSERT, rv);
}

TEST_F(OpTests, testNegativeIndex) {
    std::string json = "[1,2,3,4,5,6]";
    op.set_doc(json);

    Error rv = runOp(Command::GET, "[-1]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("6", Util::match_match(op.match()));

    json = "[1,2,3,[4,5,6,[7,8,9]]]";
    op.set_doc(json);
    rv = runOp(Command::GET, "[-1].[-1].[-1]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("9", Util::match_match(op.match()));

    std::string doc;
    rv = runOp(Command::REMOVE, "[-1].[-1].[-1]");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    // Can we PUSH values with a negative index?
    rv = runOp(Command::ARRAY_APPEND, "[-1].[-1]", "10");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runOp(Command::GET, "[-1].[-1].[-1]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("10", Util::match_match(op.match()));

    // Intermixed paths:
    json = R"({"k1": ["first", {"k2":[6,7,8]},"last"] })";
    op.set_doc(json);

    rv = runOp(Command::GET, "k1[-1]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("\"last\"", Util::match_match(op.match()));

    rv = runOp(Command::GET, "k1[1].k2[-1]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("8", Util::match_match(op.match()));
}

TEST_F(OpTests, testRootOps) {
    std::string json = "[]";
    op.set_doc(json);
    Error rv;

    rv = runOp(Command::GET, "");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("[]", Util::match_match(op.match()));

    rv = runOp(Command::ARRAY_APPEND, "", "null");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(json);

    rv = runOp(Command::GET, "");
    ASSERT_EQ("[null]", Util::match_match(op.match()));

    // Deleting root element should be CANTINSERT
    rv = runOp(Command::REMOVE, "");
    ASSERT_EQ(Error::VALUE_CANTINSERT, rv);
}

TEST_F(OpTests, testMismatch) {
    std::string doc = "{}";
    op.set_doc(doc);
    Error rv;

    rv = runOp(Command::ARRAY_APPEND, "", "null");
    ASSERT_EQ(Error::PATH_MISMATCH, rv);

    doc = "[]";
    op.set_doc(doc);
    rv = runOp(Command::DICT_UPSERT, "", "blah");
    ASSERT_EQ(Error::VALUE_CANTINSERT, rv);

    rv = runOp(Command::DICT_UPSERT, "key", "blah");
    ASSERT_EQ(Error::VALUE_CANTINSERT, rv);

    doc = "[null]";
    op.set_doc(doc);
    rv = runOp(Command::DICT_UPSERT, "", "blah");
    ASSERT_EQ(Error::VALUE_CANTINSERT, rv);

    rv = runOp(Command::DICT_UPSERT, "key", "blah");
    ASSERT_EQ(Error::VALUE_CANTINSERT, rv);

    rv = runOp(Command::ARRAY_APPEND_P, "foo.bar", "null");
    ASSERT_EQ(Error::PATH_MISMATCH, rv);
}

TEST_F(OpTests, testWhitespace) {
    std::string doc = "[ 1, 2, 3,       4        ]";
    op.set_doc(doc);
    Error rv;

    rv = runOp(Command::GET, "[-1]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("4", Util::match_match(op.match()));
}

TEST_F(OpTests, testTooDeep) {
    std::string deep = R"({"array":)";
    for (size_t ii = 0; ii < (Limits::MAX_COMPONENTS + 1) * 2; ii++) {
        deep += "[";
    }
    for (size_t ii = 0; ii < (Limits::MAX_COMPONENTS + 1) * 2; ii++) {
        deep += "]";
    }

    op.set_doc(deep);

    Error rv = runOp(Command::GET, "dummy.path");
    ASSERT_EQ(Error::DOC_ETOODEEP, rv);

    // Try with a really deep path:
    std::string dp = "dummy";
    for (size_t ii = 0; ii < (Limits::MAX_COMPONENTS + 1) * 2; ii++) {
        dp += ".dummy";
    }
    rv = runOp(Command::GET, dp);
    ASSERT_EQ(Error::PATH_E2BIG, rv);
}

TEST_F(OpTests, testTooDeepDict) {
    // Verify that we cannot create too deep a document with DICT_ADD
    // Should be able to do the maximum depth:
    std::string deep_dict("{");
    for (size_t ii = 1; ii < Limits::MAX_COMPONENTS; ii++) {
        deep_dict += "\"" + std::to_string(ii) + "\": {";
    }
    for (size_t ii = 0; ii < Limits::MAX_COMPONENTS; ii++) {
        deep_dict += "}";
    }
    op.set_doc(deep_dict);

    // Create base path at one less than the max.
    std::string one_less_max_path(std::to_string(1));
    for (size_t depth = 2; depth < Limits::MAX_COMPONENTS - 1; depth++) {
        one_less_max_path += std::string(".") + std::to_string(depth);
    }

    const std::string max_valid_path(
            one_less_max_path + "." +
            std::to_string(Limits::MAX_COMPONENTS - 1));
    // Assert we can access elements at the max depth (before we start
    // attempting to add more).
    Error rv = runOp(Command::GET, max_valid_path);
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("{}", Util::match_match(op.match()));

    // Should be able to add an element as the same level as the max.
    const std::string equal_max_path(one_less_max_path + ".sibling_max");
    rv = runOp(Command::DICT_ADD, equal_max_path, R"("also at max depth")");
    EXPECT_TRUE(rv.success()) << rv;
    std::string newDoc;
    getAssignNewDoc(newDoc);

    // Attempts to add one level deeper should fail.
    std::string too_long_path(max_valid_path + ".too_long");
    rv = runOp(Command::DICT_ADD, too_long_path, R"("past max depth")");
    EXPECT_EQ(Error::PATH_E2BIG, rv);
}

TEST_F(OpTests, testArrayInsert) {
    std::string doc("[1,2,4,5]");
    op.set_doc(doc);
    Error rv = runOp(Command::ARRAY_INSERT, "[2]", "3");
    ASSERT_TRUE(rv.success()) << "Insert op recognized";
    getAssignNewDoc(doc);
    ASSERT_EQ("[1,2,3,4,5]", doc) << "Insert works correctly in-between";

    // Do an effective 'prepend'
    rv = runOp(Command::ARRAY_INSERT, "[0]", "0");
    ASSERT_TRUE(rv.success()) << "Insert at position 0 OK";
    getAssignNewDoc(doc);
    ASSERT_EQ("[0,1,2,3,4,5]", doc) << "Insert at posititon 0 matches";

    // Do an effective 'append'
    rv = runOp(Command::ARRAY_INSERT, "[6]", "6");
    ASSERT_TRUE(rv.success()) << "Insert at posititon $SIZE ok";
    getAssignNewDoc(doc);
    ASSERT_EQ("[0,1,2,3,4,5,6]", doc) << "Insert at position $SIZE matches";

    // Reset the doc
    doc = "[1,2,3,5]";
    op.set_doc(doc);
    // -1 is not a valid insertion point.
    rv = runOp(Command::ARRAY_INSERT, "[-1]", "4");
    ASSERT_EQ(Error::PATH_EINVAL, rv)
            << "Terminal negative index invalid for insert.";

    // Insert at out-of-bounds element
    doc = "[1,2,3]";
    op.set_doc(doc);
    rv = runOp(Command::ARRAY_INSERT, "[4]", "null");
    ASSERT_EQ(Error::PATH_ENOENT, rv) << "Fails with out-of-bound index";

    // Insert not using array syntax
    rv = runOp(Command::ARRAY_INSERT, "[0].anything", "null");
    ASSERT_EQ(Error::PATH_EINVAL, rv) << "Using non-array parent in path fails";

    doc = "{}";
    op.set_doc(doc);
    // Insert with missing parent
    rv = runOp(Command::ARRAY_INSERT, "non_exist[0]", "null");
    ASSERT_EQ(Error::PATH_ENOENT, rv) << "Fails with missing parent";

    doc = "[]";
    op.set_doc(doc);
    rv = runOp(Command::ARRAY_INSERT, "[0]", "blah");
    ASSERT_EQ(Error::VALUE_CANTINSERT, rv)
            << "CANT_INSERT on invalid JSON value";

    doc = "{}";
    op.set_doc(doc);
    rv = runOp(Command::ARRAY_INSERT, "[0]", "null");
    ASSERT_EQ(Error::PATH_MISMATCH, rv) << "Fails with dict parent";
}

TEST_F(OpTests, testEmpty) {
    ASSERT_EQ(Error::VALUE_EMPTY, runOp(Command::DICT_ADD, "p"));
    ASSERT_EQ(Error::VALUE_EMPTY, runOp(Command::DICT_UPSERT, "p"));
    ASSERT_EQ(Error::VALUE_EMPTY, runOp(Command::REPLACE, "p"));
    ASSERT_EQ(Error::VALUE_EMPTY, runOp(Command::ARRAY_APPEND, "p"));
    ASSERT_EQ(Error::VALUE_EMPTY, runOp(Command::ARRAY_PREPEND, "p"));
    ASSERT_EQ(Error::VALUE_EMPTY, runOp(Command::ARRAY_ADD_UNIQUE, "p"));
    ASSERT_EQ(Error::VALUE_EMPTY, runOp(Command::ARRAY_INSERT, "p[0]"));
}

// When using the built-in result context, ensure the internal buffers are
// cleared between operations
TEST_F(OpTests, ensureRepeatable) {
    std::string doc = "{}";
    Error rv;

    op.set_doc(doc);
    rv = runOp(Command::DICT_UPSERT_P, "foo.bar", "true");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    res.clear();
    rv = runOp(Command::DICT_UPSERT_P, "bar.baz", "false");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);
}

TEST_F(OpTests, testDeleteNestedArray) {
    std::string doc = R"([0,[10,20,[100]],{"key":"value"}])";
    Error rv;
    op.set_doc(doc);

    rv = runOp(Command::GET, "[1]");
    ASSERT_EQ(Error::SUCCESS, rv);
    ASSERT_EQ("[10,20,[100]]", Util::match_match(op.match()));

    rv = runOp(Command::REMOVE, "[1][2][0]");
    ASSERT_EQ(Error::SUCCESS, rv);
    getAssignNewDoc(doc);

    rv = runOp(Command::GET, "[1]");
    ASSERT_EQ(Error::SUCCESS, rv);
    ASSERT_EQ("[10,20,[]]", Util::match_match(op.match()));

    rv = runOp(Command::REMOVE, "[1][2]");
    ASSERT_EQ(Error::SUCCESS, rv);
    getAssignNewDoc(doc);

    rv = runOp(Command::GET, "[1]");
    ASSERT_EQ(Error::SUCCESS, rv);
    ASSERT_EQ("[10,20]", Util::match_match(op.match()));

    rv = runOp(Command::REMOVE, "[1]");
    ASSERT_EQ(Error::SUCCESS, rv);
    getAssignNewDoc(doc);

    rv = runOp(Command::GET, "[1]");
    ASSERT_EQ(Error::SUCCESS, rv);
    ASSERT_EQ("{\"key\":\"value\"}", Util::match_match(op.match()));
}

TEST_F(OpTests, testEscapedJson) {
    std::string doc = R"({")"
                      R"(\)"
                      R"("quoted":true})";
    Error rv;
    op.set_doc(doc);
    rv = runOp(Command::GET, R"(\"quoted)");
    ASSERT_EQ(Error::SUCCESS, rv);
    ASSERT_EQ("true", Util::match_match(op.match()));

    // Try with insertion
    rv = runOp(Command::DICT_UPSERT_P, R"(another.\"nested.field)", "null");
    ASSERT_EQ(Error::SUCCESS, rv);

    getAssignNewDoc(doc);

    ASSERT_EQ(Error::PATH_EINVAL, runOp(Command::GET, R"("missing.quote)"));
}

TEST_F(OpTests, testUpsertArrayIndex) {
    // This test verifies some corner cases where there is a missing
    // array index which would normally be treated like a dictionary.
    // Ensure that we never automatically add an array index without
    // explicit array operations.

    std::string doc = R"({"array":[null]})";
    Error rv;
    op.set_doc(doc);

    rv = runOp(Command::DICT_UPSERT, "array[0]", "true");
    ASSERT_EQ(Error::PATH_EINVAL, rv);

    rv = runOp(Command::DICT_UPSERT, "array[1]", "true");
    ASSERT_EQ(Error::PATH_EINVAL, rv);

    rv = runOp(Command::DICT_UPSERT, "array[-1]", "true");
    ASSERT_EQ(Error::PATH_EINVAL, rv);

    rv = runOp(Command::ARRAY_APPEND_P, "array[1]", "true");
    ASSERT_EQ(Error::PATH_ENOENT, rv);

    rv = runOp(Command::ARRAY_APPEND_P, "array[2]", "true");
    ASSERT_EQ(Error::PATH_ENOENT, rv);

    rv = runOp(Command::ARRAY_ADD_UNIQUE_P, "array[2]", "true");
    ASSERT_EQ(Error::PATH_ENOENT, rv);

    rv = runOp(Command::COUNTER_P, "array[1]", "100");
    ASSERT_EQ(Error::PATH_ENOENT, rv);
}

TEST_F(OpTests, testRootAppend) {
    // Tests append on an empty path, which should use an optimized codebase
    std::string doc("[]");
    Error rv;
    op.set_doc(doc);

    rv = runOp(Command::ARRAY_APPEND, "", "1");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runOp(Command::GET, "[0]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("1", Util::match_match(op.match()));

    // Perform add_unique again
    rv = runOp(Command::ARRAY_ADD_UNIQUE, "", "1");
    ASSERT_EQ(Error::DOC_EEXISTS, rv);

    rv = runOp(Command::ARRAY_APPEND, "", "2");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);
    rv = runOp(Command::GET, "[1]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("2", Util::match_match(op.match()));

    // Try one more
    rv = runOp(Command::ARRAY_APPEND, "", "3");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);
    rv = runOp(Command::GET, "[2]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("3", Util::match_match(op.match()));

    // See how well we handle errors
    doc = "nonjson";
    op.set_doc(doc);
    rv = runOp(Command::ARRAY_APPEND, "", "123");
    ASSERT_EQ(Error::DOC_NOTJSON, rv);

    // An empty document must be rejected outright rather than read
    // backward from one-past-the-end of the (empty) buffer.
    doc = "";
    op.set_doc(doc);
    rv = runOp(Command::ARRAY_APPEND, "", "123");
    ASSERT_EQ(Error::DOC_NOTJSON, rv);

    doc = "{}";
    op.set_doc(doc);
    rv = runOp(Command::ARRAY_APPEND, "", "123");
    ASSERT_EQ(Error::PATH_MISMATCH, rv);

    doc = "[[]]";
    op.set_doc(doc);
    rv = runOp(Command::ARRAY_APPEND, "[0]", "123");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);
    rv = runOp(Command::GET, "[0][0]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("123", Util::match_match(op.match()));

    doc = "[0, {\"1\": 1}]";
    op.set_doc(doc);
    rv = runOp(Command::ARRAY_APPEND, "", "123");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);
    rv = runOp(Command::GET, "[-1]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("123", Util::match_match(op.match()));

    // Because we don't parse the array, ]] is valid.
    /*
    doc = "]]";
    op.set_doc(doc);
    rv = runOp(Command::ARRAY_APPEND, "", "123");
    ASSERT_EQ(Error::DOC_NOTJSON, rv);
    */

    // This won't work either because we don't validate against terminating
    // JSON tokens.
    /*
    doc = "{]";
    op.set_doc(doc);
    rv = runOp(Command::ARRAY_APPEND, "", "123");
    ASSERT_EQ(Error::DOC_NOTJSON, rv);
    */

    // This follows the same codepath as "normal", but good to verify just
    // in case
    doc = "[]";
    op.set_doc(doc);
    rv = runOp(Command::ARRAY_APPEND, "", "notjson");
    ASSERT_EQ(Error::VALUE_CANTINSERT, rv);
}

TEST_F(OpTests, testGetCount) {
    std::string doc = "{\"array\":[1,2,3,4]}";
    op.set_doc(doc);
    ASSERT_ERROK(runOp(Command::GET_COUNT, "array"));
    ASSERT_EQ("4", returnedMatch());

    ASSERT_ERREQ(runOp(Command::GET_COUNT, "array[0]"), Error::PATH_MISMATCH);

    doc = "[]";
    op.set_doc(doc);
    ASSERT_ERROK(runOp(Command::GET_COUNT, ""));
    ASSERT_EQ("0", returnedMatch());

    doc = "{}";
    op.set_doc(doc);
    ASSERT_ERREQ(runOp(Command::GET_COUNT, "non.exist.path"),
                 Error::PATH_ENOENT);

    doc = R"({"a":"k"})";
    op.set_doc(doc);
    ASSERT_ERREQ(runOp(Command::GET_COUNT, "n"), Error::PATH_ENOENT);

    doc = "{\"array\":[1]}";
    op.set_doc(doc);
    ASSERT_ERROK(runOp(Command::GET_COUNT, "array"));

    doc = "{\"array\":[[]]}";
    op.set_doc(doc);
    ASSERT_ERROK(runOp(Command::GET_COUNT, "array[-1]"));
    ASSERT_EQ("0", returnedMatch());

    ASSERT_ERROK(runOp(Command::GET_COUNT, ""));
    ASSERT_EQ("1", returnedMatch());

    doc = "{}";
    op.set_doc(doc);
    ASSERT_ERROK(runOp(Command::GET_COUNT, ""));
    ASSERT_EQ("0", returnedMatch());

    doc = R"({"a":1,"b":2})";
    op.set_doc(doc);
    ASSERT_ERROK(runOp(Command::GET_COUNT, ""));
    ASSERT_EQ("2", returnedMatch());
}

// GET_COUNT through a path with a negative index
TEST_F(OpTests, testGetCountNegativeIndex) {
    const std::string json =
            R"({"a":[1,{"o":{"x":1,"y":[2,3]},"p":[1,2,3]}],"b":true})";
    op.set_doc(json);
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET_COUNT, "a[-1].o"));
    EXPECT_EQ("2", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET_COUNT, "a[-1].p"));
    EXPECT_EQ("3", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET_COUNT, "a[-1].o.y"));
    EXPECT_EQ("2", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET_COUNT, "a[-1]"));
    EXPECT_EQ("2", returnedMatch());
    // Not a container
    EXPECT_EQ(Error::PATH_MISMATCH, runOp(Command::GET_COUNT, "b"));
}

// GET_COUNT (and ARRAY_APPEND on an object, which is located the same
// way) reports the same depth errors as jsonsl
TEST_F(OpTests, testGetCountDepthLimit) {
    for (size_t depth = Limits::PARSER_DEPTH - 6;
         depth <= Limits::PARSER_DEPTH + 2;
         ++depth) {
        for (const std::string inner : {"", "1", R"("s")", "[]", "{}"}) {
            const std::string deep =
                    std::string(depth, '[') + inner + std::string(depth, ']');
            const std::string json =
                    R"({"a":[1,)" + deep + R"(],"o":{"x":)" + deep + "}}";
            op.set_doc(json);
            // The levels used by the root object, "a" (or "o"), the arrays
            // and the innermost value
            const size_t levels = 2 + depth + (inner.empty() ? 0 : 1);
            const bool tooDeep = levels >= Limits::PARSER_DEPTH;
            const auto message =
                    "depth: " + std::to_string(depth) + " inner: " + inner;
            EXPECT_EQ(tooDeep ? Error::DOC_ETOODEEP : Error::SUCCESS,
                      runOp(Command::GET_COUNT, "a"))
                    << message;
            EXPECT_EQ(tooDeep ? Error::DOC_ETOODEEP : Error::SUCCESS,
                      runOp(Command::GET_COUNT, "o"))
                    << message;
            // An object can't be appended to
            EXPECT_EQ(tooDeep ? Error::DOC_ETOODEEP : Error::PATH_MISMATCH,
                      runOp(Command::ARRAY_APPEND, "o", "1"))
                    << message;
        }
    }
}

// ARRAY_APPEND on an object fails without modifying the document
TEST_F(OpTests, testArrayAppendOnObject) {
    const std::string json = R"({"o":{"x":[1,2],"y":{"z":3}},"b":1})";
    op.set_doc(json);
    EXPECT_EQ(Error::PATH_MISMATCH, runOp(Command::ARRAY_APPEND, "o", "1"));
    EXPECT_EQ(Error::PATH_MISMATCH, runOp(Command::ARRAY_APPEND, "o.y", "1"));
    EXPECT_EQ(Error::PATH_MISMATCH, runOp(Command::ARRAY_ADD_UNIQUE, "o", "1"));
    ASSERT_EQ(Error::SUCCESS, runOp(Command::ARRAY_APPEND, "o.x", "3"));
    EXPECT_EQ(R"({"o":{"x":[1,2,3],"y":{"z":3}},"b":1})", getNewDoc());
}

// Some commands require paths with specific endings. For example, DICT_UPSERT
// requires its last element MUST NOT be an array element, while ARRAY_INSERT
// requires its last element MUST be an array element. Some commands
// don't care.
enum LastElementType {
    LAST_ELEM_KEY, // Last element must be a dict key
    LAST_ELEM_INDEX, // Last element must be array index
    LAST_ELEM_ANY // Both dict keys and array indexes are acceptable
};

// Generate a path of a specific depth
// @param depth the nominal depth of the path
// @param final_type the type of last element, either an array index or dict key
// @return the path
static std::string genDeepPath(size_t depth,
                               LastElementType final_type = LAST_ELEM_KEY) {
    std::stringstream ss;
    size_t ii = 0;
    for (; ii < depth - 1; ++ii) {
        ss << "P" << std::to_string(ii) << ".";
    }
    if (final_type == LAST_ELEM_KEY) {
        ss << "P" << std::to_string(ii);
    } else {
        ss << "[0]";
    }
    return ss.str();
}

TEST_F(OpTests, TestDeepPath) {
    using Subdoc::Path;

    // "Traits" structure for the path
    struct CommandInfo {
        Command code; // Command code
        LastElementType last_elem_type; // Expected last element type
        bool implicit_child; // Whether the real size of the path is actually
                             // n+1
    };

    static const std::vector<CommandInfo> cinfo(
            {{Command::GET, LAST_ELEM_ANY, false},
             {Command::EXISTS, LAST_ELEM_ANY, false},
             {Command::REPLACE, LAST_ELEM_ANY, false},
             {Command::REMOVE, LAST_ELEM_ANY, false},
             {Command::DICT_UPSERT, LAST_ELEM_KEY, false},
             {Command::DICT_ADD, LAST_ELEM_KEY, false},
             {Command::ARRAY_PREPEND, LAST_ELEM_ANY, true},
             {Command::ARRAY_APPEND, LAST_ELEM_ANY, true},
             {Command::ARRAY_ADD_UNIQUE, LAST_ELEM_ANY, true},
             {Command::ARRAY_REMOVE_FIRST, LAST_ELEM_ANY, true},
             {Command::ARRAY_REMOVE_ALL, LAST_ELEM_ANY, true},
             {Command::ARRAY_INSERT, LAST_ELEM_INDEX, false},
             {Command::COUNTER, LAST_ELEM_ANY, false},
             {Command::GET_COUNT, LAST_ELEM_ANY, false}});

    for (auto opinfo : cinfo) {
        // List of final-element-types to try:
        std::vector<LastElementType> elemTypes;

        if (opinfo.last_elem_type == LAST_ELEM_ANY) {
            // If both element types are supported, simply add them here
            elemTypes.push_back(LAST_ELEM_INDEX);
            elemTypes.push_back(LAST_ELEM_KEY);
        } else {
            elemTypes.push_back(opinfo.last_elem_type);
        }

        for (auto etype : elemTypes) {
            size_t ncomps = Subdoc::Limits::MAX_COMPONENTS - 1;
            if (opinfo.implicit_child) {
                // If an implicit child is involved, the maximum allowable
                // path length is actually one less because the child occupies
                // ncomps+1
                ncomps--;
            }

            std::string path = genDeepPath(ncomps, etype);
            Error rv = runOp(opinfo.code, path, "123");
            ASSERT_NE(Error::PATH_E2BIG, rv)
                    << "Opcode: " << std::to_string(opinfo.code);

            // Failure case:
            ncomps++;
            path = genDeepPath(ncomps, etype);
            rv = runOp(opinfo.code, path, "123");

            if (opinfo.implicit_child) {
                // If the path is one that contains an implicit child, then
                // the failure is not on the path itself, but on the fact that
                // the value is too deep (by virtue of it having a depth of >0)
                ASSERT_EQ(Error::VALUE_ETOODEEP, rv);
            } else {
                ASSERT_EQ(Error::PATH_E2BIG, rv)
                        << "Opcode: " << std::to_string(opinfo.code);
            }
        }
    }
}

TEST_F(OpTests, testUtf8Path) {
    // Ensure we can set and retrieve paths with non-ascii utf8
    // \xc3\xba = ú
    std::string path("F\xC3\xBAtbol");
    std::string value(R"("value")");
    std::string doc("{}");
    op.set_doc(doc);
    ASSERT_EQ(Error::SUCCESS, runOp(Command::DICT_UPSERT, path, value));
    getAssignNewDoc(doc);

    // Try to retrieve the value
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, path));
    ASSERT_EQ(R"("value")", returnedMatch());
}

TEST_F(OpTests, testRemoveAfterNegativeIndex) {
    // Removing an element requires information about its siblings to
    // remove the right separating comma. That must also work when the
    // path contains a negative index followed by more components.
    std::string json = R"({"a":[{"b":1,"c":2,"d":3}],"x":[[1,2,3]]})";
    op.set_doc(json);
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REMOVE, "a[-1].b"));
    EXPECT_EQ(R"({"a":[{"c":2,"d":3}],"x":[[1,2,3]]})", getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REMOVE, "a[-1].c"));
    EXPECT_EQ(R"({"a":[{"b":1,"d":3}],"x":[[1,2,3]]})", getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REMOVE, "a[-1].d"));
    EXPECT_EQ(R"({"a":[{"b":1,"c":2}],"x":[[1,2,3]]})", getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REMOVE, "x[-1][0]"));
    EXPECT_EQ(R"({"a":[{"b":1,"c":2,"d":3}],"x":[[2,3]]})", getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REMOVE, "x[-1][2]"));
    EXPECT_EQ(R"({"a":[{"b":1,"c":2,"d":3}],"x":[[1,2]]})", getNewDoc());

    // The last element itself (which already worked)
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REMOVE, "x[-1][-1]"));
    EXPECT_EQ(R"({"a":[{"b":1,"c":2,"d":3}],"x":[[1,2]]})", getNewDoc());

    std::string single = R"({"a":[{"b":1}]})";
    op.set_doc(single);
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REMOVE, "a[-1].b"));
    EXPECT_EQ(R"({"a":[{}]})", getNewDoc());
}

TEST_F(OpTests, testUniqueWithNegativeIndex) {
    // The uniqueness must be checked when the path contains negative
    // indexes as well
    std::string json = R"({"a":[[0],[1,2]],"o":[{"b":[1,2]}]})";
    std::string doc;
    op.set_doc(json);
    EXPECT_EQ(Error::DOC_EEXISTS,
              runOp(Command::ARRAY_ADD_UNIQUE, "a[-1]", "2"));
    EXPECT_EQ(Error::DOC_EEXISTS,
              runOp(Command::ARRAY_ADD_UNIQUE, "o[-1].b", "1"));
    EXPECT_EQ(Error::DOC_EEXISTS,
              runOp(Command::ARRAY_ADD_UNIQUE_P, "o[-1].b", "1"));

    ASSERT_EQ(Error::SUCCESS, runOp(Command::ARRAY_ADD_UNIQUE, "a[-1]", "0"));
    EXPECT_EQ(R"({"a":[[0],[1,2,0]],"o":[{"b":[1,2]}]})", getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::ARRAY_ADD_UNIQUE, "o[-1].b", "3"));
    EXPECT_EQ(R"({"a":[[0],[1,2]],"o":[{"b":[1,2,3]}]})", getNewDoc());

    // Uniqueness can't be determined for arrays with non-primitives
    std::string nested = R"({"a":[[1,[2]]]})";
    op.set_doc(nested);
    EXPECT_EQ(Error::PATH_MISMATCH,
              runOp(Command::ARRAY_ADD_UNIQUE, "a[-1]", "3"));

    // The last element isn't an array
    std::string notarray = R"({"a":[1,{"b":1}]})";
    op.set_doc(notarray);
    EXPECT_EQ(Error::PATH_MISMATCH,
              runOp(Command::ARRAY_ADD_UNIQUE, "a[-1]", "3"));
    for (const auto* primitive : {"1", R"("s")", "true", "null", "1.5"}) {
        std::string last = R"({"a":[[1],)" + std::string(primitive) + "]}";
        op.set_doc(last);
        EXPECT_EQ(Error::PATH_MISMATCH,
                  runOp(Command::ARRAY_ADD_UNIQUE, "a[-1]", "3"))
                << last;
    }

    // Nested negative indexes
    std::string deep = R"({"a":[[1],[[2],[3,4]]]})";
    op.set_doc(deep);
    EXPECT_EQ(Error::DOC_EEXISTS,
              runOp(Command::ARRAY_ADD_UNIQUE, "a[-1][-1]", "4"));
    ASSERT_EQ(Error::SUCCESS,
              runOp(Command::ARRAY_ADD_UNIQUE, "a[-1][-1]", "1"));
    EXPECT_EQ(R"({"a":[[1],[[2],[3,4,1]]]})", getNewDoc());

    // ARRAY_INDEX_OF uses the same search
    ASSERT_EQ(Error::SUCCESS, runOp(Command::ARRAY_INDEX_OF, "a[-1][-1]", "4"));
    EXPECT_EQ("1", returnedMatch());
    std::string lists = R"({"a":[[0],[7,8,9]],"o":[{"b":[5,6]}]})";
    op.set_doc(lists);
    ASSERT_EQ(Error::SUCCESS, runOp(Command::ARRAY_INDEX_OF, "a[-1]", "9"));
    EXPECT_EQ("2", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::ARRAY_INDEX_OF, "o[-1].b", "6"));
    EXPECT_EQ("1", returnedMatch());
    EXPECT_EQ(Error::VALUE_ENOENT,
              runOp(Command::ARRAY_INDEX_OF, "a[-1]", "0"));
}

TEST_F(OpTests, testNegativeIndexOnPrimitive) {
    // A path continuing after a negative index which refers to a
    // primitive can't exist, and must be rejected like the same path
    // using the positive index (instead of matching an empty location
    // or failing to parse the primitive)
    for (const auto* last : {"false", "12", R"("s")", "null"}) {
        std::string json = R"({"a":[[1],)" + std::string(last) + "]}";
        op.set_doc(json);
        for (const auto& [command, value] :
             std::vector<std::pair<Command, std::string>>{
                     {Command::GET, ""},
                     {Command::REPLACE, "null"},
                     {Command::REMOVE, ""},
                     {Command::DICT_UPSERT, "1"},
                     {Command::ARRAY_APPEND, "1"},
                     {Command::ARRAY_PREPEND, "1"}}) {
            for (const auto* suffix : {"[-1]", "[0]", ".k"}) {
                const std::string negative = std::string("a[-1]") + suffix;
                const std::string positive = std::string("a[1]") + suffix;
                const auto expected = runOp(command, positive, value);
                EXPECT_EQ(Error::PATH_MISMATCH, expected)
                        << json << " " << positive;
                EXPECT_EQ(expected, runOp(command, negative, value))
                        << json << " " << negative;
            }
        }
    }

    // This used to insert the value in front of false
    std::string json = R"({"a":[true,false]})";
    op.set_doc(json);
    EXPECT_EQ(Error::PATH_MISMATCH,
              runOp(Command::REPLACE, "a[-1][-1]", "null"));
}

TEST_F(OpTests, testArrayRemoveValueWithNegativeIndex) {
    // The value must be removed when the path contains negative indexes
    // as well
    std::string json = R"({"a":[[0],[1,2,1]],"o":[{"b":[1,2]}]})";
    op.set_doc(json);
    ASSERT_EQ(Error::SUCCESS, runOp(Command::ARRAY_REMOVE_ALL, "a[-1]", "1"));
    EXPECT_EQ(R"({"a":[[0],[2]],"o":[{"b":[1,2]}]})", getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::ARRAY_REMOVE_FIRST, "a[-1]", "1"));
    EXPECT_EQ(R"({"a":[[0],[2,1]],"o":[{"b":[1,2]}]})", getNewDoc());
    ASSERT_EQ(Error::SUCCESS,
              runOp(Command::ARRAY_REMOVE_FIRST, "o[-1].b", "2"));
    EXPECT_EQ(R"({"a":[[0],[1,2,1]],"o":[{"b":[1]}]})", getNewDoc());
    EXPECT_EQ(Error::VALUE_ENOENT,
              runOp(Command::ARRAY_REMOVE_ALL, "a[-1]", "0"));
    EXPECT_EQ(Error::VALUE_ENOENT,
              runOp(Command::ARRAY_REMOVE_ALL, "o[-1].b", "3"));

    // Arrays with non-primitives
    std::string nested = R"({"a":[[1,[2]]]})";
    op.set_doc(nested);
    EXPECT_EQ(Error::PATH_MISMATCH,
              runOp(Command::ARRAY_REMOVE_ALL, "a[-1]", "1"));

    // The last element isn't an array
    std::string notarray = R"({"a":[1,{"b":1}]})";
    op.set_doc(notarray);
    EXPECT_EQ(Error::PATH_MISMATCH,
              runOp(Command::ARRAY_REMOVE_ALL, "a[-1]", "1"));
    for (const auto* primitive : {"1", R"("s")", "true", "null", "1.5"}) {
        std::string last = R"({"a":[[1],)" + std::string(primitive) + "]}";
        op.set_doc(last);
        EXPECT_EQ(Error::PATH_MISMATCH,
                  runOp(Command::ARRAY_REMOVE_ALL, "a[-1]", "1"))
                << last;
    }

    // Nested negative indexes
    std::string deep = R"({"a":[[1],[[2],[3,4]]]})";
    op.set_doc(deep);
    ASSERT_EQ(Error::SUCCESS,
              runOp(Command::ARRAY_REMOVE_ALL, "a[-1][-1]", "3"));
    EXPECT_EQ(R"({"a":[[1],[[2],[4]]]})", getNewDoc());
}

TEST_F(OpTests, testDictValueMustBeSingleValue) {
    // DICT_ADD, DICT_UPSERT and REPLACE store a single value; a value
    // which is only valid as part of an object (as it contains more than
    // one key-value pair) must be rejected rather than injecting keys into
    // the object (or invalid JSON into an array)
    std::string json = R"({"k":1,"a":[1,2,3]})";
    op.set_doc(json);
    for (const auto* value : {R"(1,"z":2)", R"("s","k":2)", R"({},"z":{})"}) {
        EXPECT_EQ(Error::VALUE_CANTINSERT,
                  runOp(Command::DICT_ADD, "new", value))
                << value;
        EXPECT_EQ(Error::VALUE_CANTINSERT,
                  runOp(Command::DICT_ADD_P, "x.y", value))
                << value;
        EXPECT_EQ(Error::VALUE_CANTINSERT,
                  runOp(Command::DICT_UPSERT, "k", value))
                << value;
        EXPECT_EQ(Error::VALUE_CANTINSERT,
                  runOp(Command::DICT_UPSERT_P, "x.y", value))
                << value;
        EXPECT_EQ(Error::VALUE_CANTINSERT, runOp(Command::REPLACE, "k", value))
                << value;
        EXPECT_EQ(Error::VALUE_CANTINSERT,
                  runOp(Command::REPLACE, "a[0]", value))
                << value;
    }

    // Single values of any kind (with surrounding whitespace) are fine
    for (const auto* value :
         {"1", R"("s")", "null", R"({"z":1,"y":2})", "[1,2]", " 2 "}) {
        ASSERT_EQ(Error::SUCCESS, runOp(Command::DICT_UPSERT, "k", value))
                << value;
        ASSERT_EQ(Error::SUCCESS, runOp(Command::REPLACE, "a[0]", value))
                << value;
    }

    // The array commands still accept multiple values
    ASSERT_EQ(Error::SUCCESS, runOp(Command::ARRAY_APPEND, "a", "4,5"));
    EXPECT_EQ(R"({"k":1,"a":[1,2,3,4,5]})", getNewDoc());
}

TEST_F(OpTests, testWhitespaceOnlyValue) {
    // A value consisting only of whitespace contains no JSON value, and
    // must be rejected by all commands taking a value (it used to be
    // accepted by the array commands, and was inserted as an empty element
    // producing invalid JSON such as [1, ])
    std::string json = R"({"k":1,"a":[1,2]})";
    op.set_doc(json);
    for (const auto* value : {" ", "\n", " \t\r\n "}) {
        for (const auto& [command, path] :
             std::vector<std::pair<Command, std::string>>{
                     {Command::ARRAY_APPEND, "a"},
                     {Command::ARRAY_APPEND_P, "a"},
                     {Command::ARRAY_PREPEND, "a"},
                     {Command::ARRAY_PREPEND_P, "a"},
                     {Command::ARRAY_INSERT, "a[1]"},
                     {Command::ARRAY_ADD_UNIQUE, "a"},
                     {Command::ARRAY_ADD_UNIQUE_P, "a"},
                     {Command::ARRAY_REMOVE_FIRST, "a"},
                     {Command::ARRAY_REMOVE_ALL, "a"},
                     {Command::DICT_ADD, "new"},
                     {Command::DICT_UPSERT, "k"},
                     {Command::REPLACE, "k"},
                     {Command::COUNTER, "k"}}) {
            EXPECT_FALSE(runOp(command, path, value).success())
                    << "command:" << static_cast<int>(command) << " value:'"
                    << value << "'";
        }
        EXPECT_EQ(Error::VALUE_CANTINSERT,
                  runOp(Command::ARRAY_APPEND, "a", value));
    }

    // Whitespace around a value is fine
    ASSERT_EQ(Error::SUCCESS, runOp(Command::ARRAY_APPEND, "a", " 3 "));
    EXPECT_EQ(R"({"k":1,"a":[1,2, 3 ]})", getNewDoc());
}

TEST_F(OpTests, testValueMustBeStrictlyValidJson) {
    // jsonsl (used to validate the value) accepts some invalid JSON, which
    // must not be inserted into the document
    std::string json = R"({"k":0,"a":[1]})";
    op.set_doc(json);
    for (const auto* value : {"[1 2]",
                              R"({"x":[1 2]})",
                              R"("\u12")",
                              "\"\xff\"",
                              "\"\xc3\x28\"",
                              R"(["\u12"])"}) {
        for (const auto& [command, path] :
             std::vector<std::pair<Command, std::string>>{
                     {Command::DICT_ADD, "new"},
                     {Command::DICT_UPSERT, "k"},
                     {Command::REPLACE, "a[0]"},
                     {Command::ARRAY_APPEND, "a"},
                     {Command::ARRAY_PREPEND, "a"},
                     {Command::ARRAY_INSERT, "a[0]"}}) {
            EXPECT_EQ(Error::VALUE_CANTINSERT, runOp(command, path, value))
                    << "command: " << static_cast<int>(command)
                    << " value: " << value;
        }
    }
    // Multiple primitives without a separating comma, as a list of values
    // for the array commands
    for (const auto* value : {"1 2", "true false", R"("a" "b")"}) {
        EXPECT_EQ(Error::VALUE_CANTINSERT,
                  runOp(Command::ARRAY_APPEND, "a", value))
                << value;
    }
    // Primitives for the commands only accepting primitives
    for (const auto* value : {R"("\u12")", "\"\xff\""}) {
        EXPECT_EQ(Error::VALUE_CANTINSERT,
                  runOp(Command::ARRAY_ADD_UNIQUE, "a", value))
                << value;
    }

    // Valid values (including multi-byte UTF-8 and \u escapes) are fine
    ASSERT_EQ(Error::SUCCESS,
              runOp(Command::ARRAY_APPEND,
                    "a",
                    "\"\\u00e9\",\"\xc3\xa9\",[1,2]"));
    EXPECT_EQ("{\"k\":0,\"a\":[1,\"\\u00e9\",\"\xc3\xa9\",[1,2]]}",
              getNewDoc());
}

TEST_F(OpTests, testNewKeyMustBeValidJsonString) {
    // A key added to the document is copied verbatim from the path, so it
    // must be valid JSON string content
    std::string json = R"({"o":{}})";
    op.set_doc(json);
    // The paths flagged with true have missing intermediate components
    for (const auto& [path, missing_parent] :
         std::vector<std::pair<const char*, bool>>{
                 {"new\xff", false},
                 {"new\xc3\x28", false},
                 {"new\xc0\xaf", false}, // Overlong '/'
                 {"new\xe0\x80\xaf", false}, // Overlong '/'
                 {"new\xed\xa0\x80", false}, // Surrogate U+D800
                 {"new\xf4\x90\x80\x80", false}, // U+110000
                 {"new\xe2\x82", false}, // Truncated
                 {"new\x1f", false},
                 {"o.new\xff", false},
                 {"o.`new\xff`", false},
                 {"o.x.new\xff", true},
                 {"new\xff.x", true}}) {
        // The commands flagged with true create missing path components
        for (const auto& [command, creates_parents] :
             std::vector<std::pair<Command, bool>>{
                     {Command::DICT_ADD, false},
                     {Command::DICT_UPSERT, false},
                     {Command::DICT_ADD_P, true},
                     {Command::DICT_UPSERT_P, true},
                     {Command::COUNTER_P, true},
                     {Command::ARRAY_APPEND_P, true},
                     {Command::ARRAY_PREPEND_P, true},
                     {Command::ARRAY_ADD_UNIQUE_P, true}}) {
            // A command which doesn't create the missing intermediate
            // components fails with PATH_ENOENT before the key is checked
            const auto expected = missing_parent && !creates_parents
                                          ? Error::PATH_ENOENT
                                          : Error::PATH_EINVAL;
            EXPECT_EQ(expected, runOp(command, path, "1"))
                    << "command: " << static_cast<int>(command)
                    << " path: " << path;
        }
    }
    EXPECT_EQ(Error::PATH_EINVAL, runOp(Command::COUNTER, "new\xff", "1"));

    // Valid keys (including multi-byte UTF-8 and escapes) are fine
    ASSERT_EQ(Error::SUCCESS,
              runOp(Command::DICT_ADD_P, "o.\xc3\xa9.a\\tb", "1"));
    EXPECT_EQ("{\"o\":{\"\xc3\xa9\":{\"a\\tb\":1}}}", getNewDoc());
    ASSERT_EQ(Error::SUCCESS,
              runOp(Command::DICT_ADD, "\xf4\x8f\xbf\xbf", "1")); // U+10FFFF
    EXPECT_EQ("{\"o\":{},\"\xf4\x8f\xbf\xbf\":1}", getNewDoc());
}

TEST_F(OpTests, ArrayScanAppendWithTrickyStrings) {
    const std::string json = R"({"a":["]","\"]",{"x":"]}"},[["\\"]]],"b":[1]})";
    op.set_doc(json);
    ASSERT_EQ(Error::SUCCESS, runOp(Command::ARRAY_APPEND, "a", "42"));
    std::string doc;
    getAssignNewDoc(doc);
    EXPECT_EQ(
            "{\"a\":[\"]\",\"\\\"]\",{\"x\":\"]}\"},[[\"\\\\\"]],42],\"b\":[1]"
            "}",
            doc);
}

TEST_F(OpTests, ArrayScanAppendEmptyArray) {
    const std::string json = R"({"a":[ ],"b":[1]})";
    op.set_doc(json);
    ASSERT_EQ(Error::SUCCESS, runOp(Command::ARRAY_APPEND, "a", "42"));
    std::string doc;
    getAssignNewDoc(doc);
    EXPECT_EQ(R"({"a":[42],"b":[1]})", doc);
}

TEST_F(OpTests, ArrayScanAppendNestedPath) {
    const std::string json = R"({"a":{"b":[[1],[2,3]]},"c":0})";
    op.set_doc(json);
    ASSERT_EQ(Error::SUCCESS, runOp(Command::ARRAY_APPEND, "a.b[1]", "4"));
    std::string doc;
    getAssignNewDoc(doc);
    EXPECT_EQ(R"({"a":{"b":[[1],[2,3,4]]},"c":0})", doc);
    ASSERT_EQ(Error::SUCCESS, runOp(Command::ARRAY_APPEND, "a.b[-1]", "5"));
    getAssignNewDoc(doc);
    EXPECT_EQ(R"({"a":{"b":[[1],[2,3,4,5]]},"c":0})", doc);
}

TEST_F(OpTests, ArrayScanAppendToNonArray) {
    const std::string json = R"({"a":{"b":1}})";
    op.set_doc(json);
    EXPECT_EQ(Error::PATH_MISMATCH, runOp(Command::ARRAY_APPEND, "a", "4"));
    EXPECT_EQ(Error::PATH_MISMATCH, runOp(Command::ARRAY_ADD_UNIQUE, "a", "4"));
}

TEST_F(OpTests, ArrayScanAppendToTruncatedDocument) {
    const std::string json = R"({"a":[1,2,3)";
    op.set_doc(json);
    EXPECT_EQ(Error::DOC_NOTJSON, runOp(Command::ARRAY_APPEND, "a", "4"));
    EXPECT_EQ(Error::DOC_NOTJSON, runOp(Command::ARRAY_ADD_UNIQUE, "a", "4"));
}

TEST_F(OpTests, RemoveValueNestedPath) {
    const std::string json = R"({"a":{"b":[[1],["x",2,"x"]]},"c":0})";
    op.set_doc(json);
    ASSERT_EQ(Error::SUCCESS,
              runOp(Command::ARRAY_REMOVE_ALL, "a.b[1]", R"("x")"));
    EXPECT_EQ(R"({"a":{"b":[[1],[2]]},"c":0})", getNewDoc());
}

// GET_COUNT of arrays and objects
TEST_F(OpTests, GetCountContainers) {
    const std::string json =
            R"({"a":[1,"],",{"x":[1,2]},[3,4]],"o":{"k1":"},","k2":[1],)"
            R"("k3":{"a":1,"b":2}},"e":[],"eo":{}})";
    op.set_doc(json);
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET_COUNT, "a"));
    EXPECT_EQ("4", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET_COUNT, "o"));
    EXPECT_EQ("3", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET_COUNT, "o.k3"));
    EXPECT_EQ("2", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET_COUNT, "a[2].x"));
    EXPECT_EQ("2", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET_COUNT, "e"));
    EXPECT_EQ("0", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET_COUNT, "eo"));
    EXPECT_EQ("0", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET_COUNT, ""));
    EXPECT_EQ("4", returnedMatch());
    EXPECT_EQ(Error::PATH_MISMATCH, runOp(Command::GET_COUNT, "a[0]"));
}

TEST_F(OpTests, LastElementGet) {
    const std::string json =
            R"({"s":[1,"a\"],b"],"n":["x", -12 ],"l":[1,[2,"]",[3]]],)"
            R"("o":[1,{"a":[1,2],"b":"}"}],"e":[],"t":1})";
    op.set_doc(json);
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "s[-1]"));
    EXPECT_EQ("\"a\\\"],b\"", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "n[-1]"));
    EXPECT_EQ("-12", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "l[-1]"));
    EXPECT_EQ(R"([2,"]",[3]])", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "l[-1][-1]"));
    EXPECT_EQ("[3]", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "l[-1][-1][-1]"));
    EXPECT_EQ("3", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "o[-1].b"));
    EXPECT_EQ(R"("}")", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET_COUNT, "o[-1]"));
    EXPECT_EQ("2", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET_COUNT, "l[-1]"));
    EXPECT_EQ("3", returnedMatch());
    EXPECT_EQ(Error::PATH_ENOENT, runOp(Command::GET, "e[-1]"));
    EXPECT_EQ(Error::PATH_MISMATCH, runOp(Command::GET, "t[-1]"));
}

TEST_F(OpTests, LastElementMutations) {
    const std::string json = R"({"a":[1,2, 5 ],"f":[1.5],"l":[[],[1]]})";
    op.set_doc(json);
    ASSERT_EQ(Error::SUCCESS, runOp(Command::COUNTER, "a[-1]", "1"));
    EXPECT_EQ(R"({"a":[1,2, 6 ],"f":[1.5],"l":[[],[1]]})", getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REPLACE, "a[-1]", "true"));
    EXPECT_EQ(R"({"a":[1,2, true ],"f":[1.5],"l":[[],[1]]})", getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REMOVE, "a[-1]"));
    EXPECT_EQ(R"({"a":[1,2 ],"f":[1.5],"l":[[],[1]]})", getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REMOVE, "f[-1]"));
    EXPECT_EQ(R"({"a":[1,2, 5 ],"f":[],"l":[[],[1]]})", getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::ARRAY_APPEND, "l[-1]", "2"));
    EXPECT_EQ(R"({"a":[1,2, 5 ],"f":[1.5],"l":[[],[1,2]]})", getNewDoc());
    EXPECT_EQ(Error::PATH_MISMATCH, runOp(Command::COUNTER, "f[-1]", "1"));
}

// Operations on the last element of an array report the depth errors of
// the array, whether the deep element is the last one or before it
TEST_F(OpTests, LastElementDepthLimit) {
    for (size_t depth = Limits::PARSER_DEPTH - 6;
         depth <= Limits::PARSER_DEPTH + 2;
         ++depth) {
        for (const std::string inner : {"", "1", R"("s")", "[]", "{}"}) {
            const std::string deep =
                    std::string(depth, '[') + inner + std::string(depth, ']');
            // The levels used by the root object, "a", the arrays and the
            // innermost value
            const size_t levels = 2 + depth + (inner.empty() ? 0 : 1);
            const bool tooDeep = levels >= Limits::PARSER_DEPTH;
            for (const auto& json :
                 {R"({"a":[1,)" + deep + "]}", R"({"a":[)" + deep + ",1]}"}) {
                op.set_doc(json);
                const auto message =
                        "depth: " + std::to_string(depth) + " json: " + json;
                const auto expected =
                        tooDeep ? Error::DOC_ETOODEEP : Error::SUCCESS;
                EXPECT_EQ(expected, runOp(Command::GET, "a[-1]")) << message;
                EXPECT_EQ(expected, runOp(Command::REPLACE, "a[-1]", "0"))
                        << message;
                EXPECT_EQ(expected, runOp(Command::REMOVE, "a[-1]")) << message;
            }
        }
    }
}

TEST_F(OpTests, ArrayIndexGet) {
    const std::string json =
            R"({"a":[1, "b\"],", [2,[3]] ,{"x":"]"},null],"t":true})";
    op.set_doc(json);
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "a[0]"));
    EXPECT_EQ("1", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "a[1]"));
    EXPECT_EQ("\"b\\\"],\"", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "a[2]"));
    EXPECT_EQ("[2,[3]]", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "a[2][1][0]"));
    EXPECT_EQ("3", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "a[3].x"));
    EXPECT_EQ(R"("]")", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "a[4]"));
    EXPECT_EQ("null", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET_COUNT, "a[2]"));
    EXPECT_EQ("2", returnedMatch());
    EXPECT_EQ(Error::PATH_ENOENT, runOp(Command::GET, "a[5]"));
    EXPECT_EQ(Error::PATH_ENOENT, runOp(Command::GET, "a[5].x"));
    EXPECT_EQ(Error::PATH_MISMATCH, runOp(Command::GET, "t[0]"));
}

TEST_F(OpTests, ArrayIndexMutations) {
    const std::string json = R"({"a":[1,2,3],"b":0})";
    op.set_doc(json);
    ASSERT_EQ(Error::SUCCESS, runOp(Command::ARRAY_INSERT, "a[2]", "9"));
    EXPECT_EQ(R"({"a":[1,2,9,3],"b":0})", getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::ARRAY_INSERT, "a[3]", "9"));
    EXPECT_EQ(R"({"a":[1,2,3,9],"b":0})", getNewDoc());
    EXPECT_EQ(Error::PATH_ENOENT, runOp(Command::ARRAY_INSERT, "a[4]", "9"));
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REMOVE, "a[0]"));
    EXPECT_EQ(R"({"a":[2,3],"b":0})", getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REMOVE, "a[1]"));
    EXPECT_EQ(R"({"a":[1,3],"b":0})", getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REMOVE, "a[2]"));
    EXPECT_EQ(R"({"a":[1,2],"b":0})", getNewDoc());
    EXPECT_EQ(Error::PATH_ENOENT, runOp(Command::REMOVE, "a[3]"));
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REPLACE, "a[1]", "true"));
    EXPECT_EQ(R"({"a":[1,true,3],"b":0})", getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::COUNTER, "a[2]", "5"));
    EXPECT_EQ(R"({"a":[1,2,8],"b":0})", getNewDoc());

    const std::string single = R"({"a":[1]})";
    op.set_doc(single);
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REMOVE, "a[0]"));
    EXPECT_EQ(R"({"a":[]})", getNewDoc());
    const std::string empty = R"({"a":[]})";
    op.set_doc(empty);
    ASSERT_EQ(Error::SUCCESS, runOp(Command::ARRAY_INSERT, "a[0]", "1"));
    EXPECT_EQ(R"({"a":[1]})", getNewDoc());
}

// An array index reports the depth errors of the elements up to (and
// including) the element with the index, but not of the elements after it
TEST_F(OpTests, ArrayIndexDepthLimit) {
    for (size_t depth = Limits::PARSER_DEPTH - 6;
         depth <= Limits::PARSER_DEPTH + 2;
         ++depth) {
        for (const std::string inner : {"", "1", R"("s")", "[]", "{}"}) {
            const std::string deep =
                    std::string(depth, '[') + inner + std::string(depth, ']');
            // The levels used by the root object, "a", the arrays and the
            // innermost value
            const size_t levels = 2 + depth + (inner.empty() ? 0 : 1);
            const bool tooDeep = levels >= Limits::PARSER_DEPTH;
            // The deep element before, at and after index 1
            for (const auto& [json, parsed] :
                 std::vector<std::pair<std::string, bool>>{
                         {R"({"a":[)" + deep + ",1,2]}", true},
                         {R"({"a":[0,)" + deep + ",2]}", true},
                         {R"({"a":[0,1,)" + deep + "]}", false}}) {
                op.set_doc(json);
                const auto message =
                        "depth: " + std::to_string(depth) + " json: " + json;
                const auto expected = tooDeep && parsed ? Error::DOC_ETOODEEP
                                                        : Error::SUCCESS;
                EXPECT_EQ(expected, runOp(Command::GET, "a[1]")) << message;
                EXPECT_EQ(expected, runOp(Command::REPLACE, "a[1]", "0"))
                        << message;
                EXPECT_EQ(expected, runOp(Command::REMOVE, "a[1]")) << message;
                EXPECT_EQ(expected, runOp(Command::ARRAY_INSERT, "a[1]", "9"))
                        << message;
            }
        }
    }
}

// Operations on members located after arrays and objects
TEST_F(OpTests, MemberAfterContainers) {
    const std::string json =
            R"({"unrelated":[1,2,{"k":"v"}],"target":42,"other":{"a":1}})";
    op.set_doc(json);
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "target"));
    EXPECT_EQ("42", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REPLACE, "target", "99"));
    EXPECT_EQ(R"({"unrelated":[1,2,{"k":"v"}],"target":99,"other":{"a":1}})",
              getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REMOVE, "target"));
    EXPECT_EQ(R"({"unrelated":[1,2,{"k":"v"}],"other":{"a":1}})", getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::COUNTER, "target", "10"));
    EXPECT_EQ(R"({"unrelated":[1,2,{"k":"v"}],"target":52,"other":{"a":1}})",
              getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "other.a"));
    EXPECT_EQ("1", returnedMatch());

    // Mismatched brackets inside unrelated
    const std::string mismatched =
            R"({"unrelated":[1,2,{"k":"v"},"target":42})";
    op.set_doc(mismatched);
    EXPECT_EQ(Error::DOC_NOTJSON, runOp(Command::GET, "target"));
}

TEST_F(OpTests, MemberAfterContainersNested) {
    const std::string json =
            R"({"x":{"big":[[1],{"a":[2]}],"y":{"z":[3,{"q":[4]}]},"w":5},)"
            R"("x2":[[6],{"k":[7]},{"k":"v"}]})";
    op.set_doc(json);
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "x.w"));
    EXPECT_EQ("5", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "x.y.z[1].q[0]"));
    EXPECT_EQ("4", returnedMatch());
    // Elements before the index
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "x2[2].k"));
    EXPECT_EQ(R"("v")", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET_COUNT, "x2"));
    EXPECT_EQ("3", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REMOVE, "x.y"));
    EXPECT_EQ(R"({"x":{"big":[[1],{"a":[2]}],"w":5},)"
              R"("x2":[[6],{"k":[7]},{"k":"v"}]})",
              getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::DICT_ADD, "x.new", "true"));
    EXPECT_EQ(R"({"x":{"big":[[1],{"a":[2]}],"y":{"z":[3,{"q":[4]}]},)"
              R"("w":5,"new":true},"x2":[[6],{"k":[7]},{"k":"v"}]})",
              getNewDoc());
    EXPECT_EQ(Error::PATH_ENOENT, runOp(Command::GET, "x.missing"));
}

/**
 * An array before the member in the path reports exactly the same depth
 * errors as when the array itself is accessed. A stack level is needed for
 * every value (including the primitives in the innermost container), so
 * test nesting around the limit with different innermost values, using a
 * GET of the array itself as the reference.
 */
TEST_F(OpTests, MemberAfterContainerDepthLimit) {
    for (size_t depth = Limits::PARSER_DEPTH - 6;
         depth <= Limits::PARSER_DEPTH + 2;
         ++depth) {
        for (const std::string inner : {"", "1", R"("s")", "[]", "{}"}) {
            const std::string json = R"({"skip":)" + std::string(depth, '[') +
                                     inner + std::string(depth, ']') +
                                     R"(,"b":1})";
            op.set_doc(json);
            const auto expected = runOp(Command::GET, "skip");
            EXPECT_EQ(expected, runOp(Command::GET, "b"))
                    << "depth: " << depth << " inner: " << inner;
        }
    }
}

// Operations on a whole array or object, in different positions in the
// document and with whitespace
TEST_F(OpTests, WholeContainer) {
    const std::string json =
            R"({"a":[1,{"b":[2,3]}],"o":{ "x" : [ 1 , 2 ] , "y":{}},"z":[]})";
    op.set_doc(json);
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "a"));
    EXPECT_EQ(R"([1,{"b":[2,3]}])", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "o.x"));
    EXPECT_EQ("[ 1 , 2 ]", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::EXISTS, "o.x"));
    EXPECT_EQ("[ 1 , 2 ]", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "a[1]"));
    EXPECT_EQ(R"({"b":[2,3]})", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET_COUNT, "o"));
    EXPECT_EQ("2", returnedMatch());

    ASSERT_EQ(Error::SUCCESS, runOp(Command::REPLACE, "a", "0"));
    EXPECT_EQ(R"({"a":0,"o":{ "x" : [ 1 , 2 ] , "y":{}},"z":[]})", getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::DICT_UPSERT, "o.x", "1"));
    EXPECT_EQ(R"({"a":[1,{"b":[2,3]}],"o":{ "x" : 1 , "y":{}},"z":[]})",
              getNewDoc());

    // Remove has to locate the sibling following the container (if any)
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REMOVE, "a"));
    EXPECT_EQ(R"({"o":{ "x" : [ 1 , 2 ] , "y":{}},"z":[]})", getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REMOVE, "o.x"));
    EXPECT_EQ(R"({"a":[1,{"b":[2,3]}],"o":{  "y":{}},"z":[]})", getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REMOVE, "o.y"));
    EXPECT_EQ(R"({"a":[1,{"b":[2,3]}],"o":{ "x" : [ 1 , 2 ] },"z":[]})",
              getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REMOVE, "z"));
    EXPECT_EQ(R"({"a":[1,{"b":[2,3]}],"o":{ "x" : [ 1 , 2 ] , "y":{}}})",
              getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REMOVE, "a[1]"));
    EXPECT_EQ(R"({"a":[1],"o":{ "x" : [ 1 , 2 ] , "y":{}},"z":[]})",
              getNewDoc());
}

// Operations on members located after long strings, and on long strings
TEST_F(OpTests, LongStrings) {
    // Escapes and structural characters around the 64 byte block boundaries
    const std::string text =
            std::string(70, 'x') + R"(\"]}\\)" + std::string(70, 'y');
    const std::string str = "\"" + text + "\"";
    const std::string json =
            R"({"s":)" + str + R"(,"t":1,"a":[)" + str + R"(,2],"k\u0065y":3})";
    op.set_doc(json);
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "t"));
    EXPECT_EQ("1", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "a[1]"));
    EXPECT_EQ("2", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "key"));
    EXPECT_EQ("3", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "s"));
    EXPECT_EQ(str, returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "a[0]"));
    EXPECT_EQ(str, returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REMOVE, "s"));
    EXPECT_EQ("{\"t\":1,\"a\":[" + str + ",2],\"k\\u0065y\":3}", getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REPLACE, "s", "1"));
    EXPECT_EQ("{\"s\":1,\"t\":1,\"a\":[" + str + ",2],\"k\\u0065y\":3}",
              getNewDoc());
}

// The members of the objects in the path, after large values and with
// escaped and duplicate keys
TEST_F(OpTests, ObjectMemberInPath) {
    // A large value (more than 512 bytes)
    std::string big = "[";
    for (int ii = 0; ii < 40; ++ii) {
        big += (ii ? "," : "") + std::string(R"({"k":"x,}y"})");
    }
    big += "]";
    // With an escaped key and duplicate keys
    const std::string tail = R"("k\u0065y":3,"dup":1,"dup":2,"last":true})";
    const std::string json =
            R"({"a":1, "big":)" + big + R"(,"o":{"x":1,"y":{"z":2}},)" + tail;
    const std::string head = R"({"a":1, "big":)" + big + ",";
    op.set_doc(json);

    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "a"));
    EXPECT_EQ("1", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "last"));
    EXPECT_EQ("true", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "o.y.z"));
    EXPECT_EQ("2", returnedMatch());
    // The key in the document is escaped
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "key"));
    EXPECT_EQ("3", returnedMatch());
    // The first of the duplicates
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "dup"));
    EXPECT_EQ("1", returnedMatch());
    EXPECT_EQ(Error::PATH_ENOENT, runOp(Command::GET, "missing"));
    EXPECT_EQ(Error::PATH_ENOENT, runOp(Command::GET, "o.missing"));
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET_COUNT, "o"));
    EXPECT_EQ("2", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET_COUNT, ""));
    EXPECT_EQ("7", returnedMatch());

    ASSERT_EQ(Error::SUCCESS, runOp(Command::DICT_ADD, "o.new", "1"));
    EXPECT_EQ(head + R"("o":{"x":1,"y":{"z":2},"new":1},)" + tail, getNewDoc());
    EXPECT_EQ(Error::DOC_EEXISTS, runOp(Command::DICT_ADD, "last", "1"));
    ASSERT_EQ(Error::SUCCESS, runOp(Command::DICT_UPSERT, "last", "false"));
    EXPECT_EQ(head + R"("o":{"x":1,"y":{"z":2}},"k\u0065y":3,"dup":1,)"
                     R"("dup":2,"last":false})",
              getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::DICT_ADD_P, "o.q.r", "1"));
    EXPECT_EQ(head + R"("o":{"x":1,"y":{"z":2},"q":{"r":1}},)" + tail,
              getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::COUNTER, "o.y.z", "5"));
    EXPECT_EQ(head + R"("o":{"x":1,"y":{"z":7}},)" + tail, getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REPLACE, "big", "0"));
    EXPECT_EQ(R"({"a":1, "big":0,"o":{"x":1,"y":{"z":2}},)" + tail,
              getNewDoc());

    ASSERT_EQ(Error::SUCCESS, runOp(Command::REMOVE, "o.x"));
    EXPECT_EQ(head + R"("o":{"y":{"z":2}},)" + tail, getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REMOVE, "dup"));
    EXPECT_EQ(head + R"("o":{"x":1,"y":{"z":2}},"k\u0065y":3,"dup":2,)"
                     R"("last":true})",
              getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REMOVE, "last"));
    EXPECT_EQ(head + R"("o":{"x":1,"y":{"z":2}},"k\u0065y":3,"dup":1,)"
                     R"("dup":2})",
              getNewDoc());
}

// The members before the one with the key report the same depth errors as
// when the member itself is accessed
TEST_F(OpTests, ObjectMemberInPathDepthLimit) {
    for (size_t depth = Limits::PARSER_DEPTH - 6;
         depth <= Limits::PARSER_DEPTH + 2;
         ++depth) {
        for (const std::string inner : {"", "1", R"("s")", "[]", "{}"}) {
            const std::string json = R"({"m":)" + std::string(depth, '[') +
                                     inner + std::string(depth, ']') +
                                     R"(,"b":1})";
            op.set_doc(json);
            // The levels used by the root object, the arrays and the
            // innermost value
            const size_t levels = 1 + depth + (inner.empty() ? 0 : 1);
            const auto expected = levels < Limits::PARSER_DEPTH
                                          ? Error::SUCCESS
                                          : Error::DOC_ETOODEEP;
            for (auto command : {Command::GET, Command::REMOVE}) {
                EXPECT_EQ(expected, runOp(command, "b"))
                        << "depth: " << depth << " inner: " << inner
                        << " command: " << int(command);
            }
        }
    }
}

// Array indexes which aren't the last component of the path
TEST_F(OpTests, ArrayIndexInPath) {
    const std::string json =
            R"({"arr":[1, true,null ,"s",[2],{"k":1},{"k":2},  {"k":3} ],)"
            R"("n":[[1,2],[3,[4,5]]]})";
    op.set_doc(json);
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "arr[5].k"));
    EXPECT_EQ("1", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "arr[7].k"));
    EXPECT_EQ("3", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "arr[4][0]"));
    EXPECT_EQ("2", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "n[1][1][0]"));
    EXPECT_EQ("4", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "arr[-1].k"));
    EXPECT_EQ("3", returnedMatch());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "n[-1][0]"));
    EXPECT_EQ("3", returnedMatch());
    // The array doesn't have that many elements
    EXPECT_EQ(Error::PATH_ENOENT, runOp(Command::GET, "arr[8].k"));
    EXPECT_EQ(Error::PATH_ENOENT, runOp(Command::GET, "arr[9].k"));
    EXPECT_EQ(Error::PATH_ENOENT, runOp(Command::DICT_ADD_P, "arr[9].k", "1"));
    EXPECT_EQ(Error::PATH_MISMATCH, runOp(Command::GET, "arr[1].k"));
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET_COUNT, "n[1][1]"));
    EXPECT_EQ("2", returnedMatch());

    ASSERT_EQ(Error::SUCCESS, runOp(Command::REMOVE, "arr[6].k"));
    EXPECT_EQ(R"({"arr":[1, true,null ,"s",[2],{"k":1},{},  {"k":3} ],)"
              R"("n":[[1,2],[3,[4,5]]]})",
              getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::DICT_ADD, "arr[7].new", "1"));
    EXPECT_EQ(R"({"arr":[1, true,null ,"s",[2],{"k":1},{"k":2},  )"
              R"({"k":3,"new":1} ],"n":[[1,2],[3,[4,5]]]})",
              getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::REPLACE, "n[1][1][1]", "0"));
    EXPECT_EQ(R"({"arr":[1, true,null ,"s",[2],{"k":1},{"k":2},  {"k":3} ],)"
              R"("n":[[1,2],[3,[4,0]]]})",
              getNewDoc());
    ASSERT_EQ(Error::SUCCESS, runOp(Command::ARRAY_APPEND, "n[1][1]", "6"));
    EXPECT_EQ(R"({"arr":[1, true,null ,"s",[2],{"k":1},{"k":2},  {"k":3} ],)"
              R"("n":[[1,2],[3,[4,5,6]]]})",
              getNewDoc());
}

// The elements before the one with the index report the same depth
// errors as when the element itself is accessed
TEST_F(OpTests, ArrayIndexInPathDepthLimit) {
    for (size_t depth = Limits::PARSER_DEPTH - 6;
         depth <= Limits::PARSER_DEPTH + 2;
         ++depth) {
        for (const std::string inner : {"", "1", R"("s")", "[]", "{}"}) {
            const std::string json = R"({"a":[)" + std::string(depth, '[') +
                                     inner + std::string(depth, ']') +
                                     R"(,{"b":1}]})";
            op.set_doc(json);
            // The levels used by the root object, "a", the arrays and the
            // innermost value
            const size_t levels = 2 + depth + (inner.empty() ? 0 : 1);
            const auto expected = levels < Limits::PARSER_DEPTH
                                          ? Error::SUCCESS
                                          : Error::DOC_ETOODEEP;
            for (auto command : {Command::GET, Command::REMOVE}) {
                EXPECT_EQ(expected, runOp(command, "a[1].b"))
                        << "depth: " << depth << " inner: " << inner
                        << " command: " << int(command);
            }
        }
    }
}

// Operations on a whole array report the depth errors of its contents
TEST_F(OpTests, WholeContainerDepthLimit) {
    for (size_t depth = Limits::PARSER_DEPTH - 6;
         depth <= Limits::PARSER_DEPTH + 2;
         ++depth) {
        for (const std::string inner : {"", "1", R"("s")", "[]", "{}"}) {
            const std::string json = R"({"m":)" + std::string(depth, '[') +
                                     inner + std::string(depth, ']') +
                                     R"(,"b":1})";
            op.set_doc(json);
            // The levels used by the root object, the arrays and the
            // innermost value
            const size_t levels = 1 + depth + (inner.empty() ? 0 : 1);
            const auto expected = levels < Limits::PARSER_DEPTH
                                          ? Error::SUCCESS
                                          : Error::DOC_ETOODEEP;
            for (auto command :
                 {Command::GET, Command::EXISTS, Command::REMOVE}) {
                EXPECT_EQ(expected, runOp(command, "m"))
                        << "depth: " << depth << " inner: " << inner
                        << " command: " << int(command);
            }
        }
    }
}
