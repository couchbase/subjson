/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
*     Copyright 2015 Couchbase, Inc
*
*   Licensed under the Apache License, Version 2.0 (the "License");
*   you may not use this file except in compliance with the License.
*   You may obtain a copy of the License at
*
*       http://www.apache.org/licenses/LICENSE-2.0
*
*   Unless required by applicable law or agreed to in writing, software
*   distributed under the License is distributed on an "AS IS" BASIS,
*   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*   See the License for the specific language governing permissions and
*   limitations under the License.
*/
#include "subdoc-tests-common.h"
#include "subdoc/validate.h"
using std::string;
using std::cerr;
using std::endl;
using Subdoc::Operation;
using Subdoc::Match;
using Subdoc::Loc;
using Subdoc::Error;
using Subdoc::Command;
using Subdoc::Validator;
using Subdoc::Util;
using Subdoc::Limits;
using Subdoc::Result;

class OpTests : public ::testing::Test {
protected:
  virtual void SetUp() {
      op.clear();
  }

  Operation op;
  Result res;

  string getNewDoc();
  void getAssignNewDoc(string& newdoc);
  Error runOp(Command, const char *path, const char *value = NULL, size_t nvalue = 0);
};

string
OpTests::getNewDoc()
{
    string ret;
    for (auto ii : res.newdoc()) {
        ret.append(ii.at, ii.length);
    }

    // validate
    int rv = Validator::validate(ret, op.parser());
    EXPECT_EQ(JSONSL_ERROR_SUCCESS, rv)
        << Util::jsonerr(static_cast<jsonsl_error_t>(rv));
    return ret;
}

void
OpTests::getAssignNewDoc(string& newdoc)
{
    newdoc = getNewDoc();
    op.set_doc(newdoc);
}

Error
OpTests::runOp(Command opcode, const char *path, const char *value, size_t nvalue)
{
    op.clear();
    if (value != NULL) {
        if (nvalue == 0) {
            nvalue = strlen(value);
        }
        op.set_value(value, nvalue);
    }
    op.set_code(opcode);
    op.set_result_buf(&res);
    return op.op_exec(path, strlen(path));
}

#include "big_json.inc.h"
TEST_F(OpTests, testOperations)
{
    string newdoc;
    op.set_doc(SAMPLE_big_json, strlen(SAMPLE_big_json));
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "name"));
    ASSERT_EQ("\"Allagash Brewing\"", Util::match_match(op.match()));
    ASSERT_EQ(Error::SUCCESS, runOp(Command::EXISTS, "name"));

    Error rv = runOp(Command::REMOVE, "address");
    ASSERT_TRUE(rv.success());

    getAssignNewDoc(newdoc);
    // Should return in KEY_ENOENT
    ASSERT_EQ(Error::PATH_ENOENT, runOp(Command::GET, "address"));

    // Insert something back, maybe :)
    rv = runOp(Command::DICT_ADD, "address", "\"123 Main St.\"");
    ASSERT_TRUE(rv.success());

    getAssignNewDoc(newdoc);
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "address"));
    ASSERT_EQ("\"123 Main St.\"", Util::match_match(op.match()));

    // Replace the value now:
    rv = runOp(Command::REPLACE, "address", "\"33 Marginal Rd.\"");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(newdoc);
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "address"));
    ASSERT_EQ("\"33 Marginal Rd.\"", Util::match_match(op.match()));

    // Get it back:
    op.set_doc(SAMPLE_big_json, strlen(SAMPLE_big_json));
    // add non-existent path
    rv = runOp(Command::DICT_ADD, "foo.bar.baz", "[1,2,3]");
    ASSERT_EQ(Error::PATH_ENOENT, rv);

    rv = runOp(Command::DICT_ADD_P, "foo.bar.baz", "[1,2,3]");
    ASSERT_TRUE(rv.success());
    getNewDoc();
}

// Mainly checks that we can perform generic DELETE and GET operations
// on array indices
TEST_F(OpTests, testGenericOps)
{
    op.set_doc(SAMPLE_big_json, strlen(SAMPLE_big_json));
    Error rv;
    string newdoc;

    rv = runOp(Command::REMOVE, "address[0]");
    ASSERT_TRUE(rv.success());

    getAssignNewDoc(newdoc);
    rv = runOp(Command::GET, "address[0]");
    ASSERT_EQ(Error::PATH_ENOENT, rv);

    rv = runOp(Command::REPLACE, "address",
        "[\"500 B St.\", \"Anytown\", \"USA\"]");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(newdoc);
    rv = runOp(Command::GET, "address[2]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("\"USA\"", Util::match_match(op.match()));

    rv = runOp(Command::REPLACE, "address[1]", "\"Sacramento\"");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(newdoc);

    rv = runOp(Command::GET, "address[1]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("\"Sacramento\"", Util::match_match(op.match()));
}

TEST_F(OpTests, testReplaceArrayDeep)
{
    // Create an array at max level.
    string deep;
    for (size_t ii = 0; ii < Limits::MAX_COMPONENTS - 1; ii++) {
        deep += "[";
    }
    deep += "1";
    for (size_t ii = 0; ii < Limits::MAX_COMPONENTS - 1; ii++) {
        deep += "]";
    }
    op.set_doc(deep);

    // Sanity check - should be able to access maximum depth.
    string one_minus_max_path;
    for (size_t ii = 0; ii < Limits::MAX_COMPONENTS - 2; ii++) {
        one_minus_max_path += "[0]";
    }
    string max_path(one_minus_max_path + "[0]");
    Error rv = runOp(Command::GET, one_minus_max_path.c_str());
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("[1]", Util::match_match(op.match()));

    // Should be able to replace the array element with a different one.
    rv = runOp(Command::REPLACE, max_path.c_str(), "2");
    ASSERT_TRUE(rv.success());
    string newdoc;
    getAssignNewDoc(newdoc);
    rv = runOp(Command::GET, one_minus_max_path.c_str());
    ASSERT_TRUE(rv.success());
    EXPECT_EQ("[2]", Util::match_match(op.match()));

    // Should be able to replace the last level array with a different
    // (larger) one.
    rv = runOp(Command::REPLACE, one_minus_max_path.c_str(), "[3,4]");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(newdoc);
    rv = runOp(SUBDOC_CMD_GET, one_minus_max_path.c_str());
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("[3,4]", Util::match_match(op.match()));

    // Should not be able to make it any deeper (already at maximum).
    rv = runOp(Command::REPLACE, one_minus_max_path.c_str(), "[[5]]");
    ASSERT_EQ(Error::VALUE_ETOODEEP, rv);
}

TEST_F(OpTests, testListOps)
{
    string doc = "{}";
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

    rv = runOp(Command::ARRAY_APPEND, "array", "{\"foo\":\"bar\"}");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runOp(Command::GET, "array[3].foo");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("\"bar\"", Util::match_match(op.match()));

    // Test the various POP commands
    rv = runOp(Command::REMOVE, "array[0]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("0", Util::match_match(op.match()));
    getAssignNewDoc(doc);

    rv = runOp(Command::GET, "array[0]");

    rv = runOp(Command::REMOVE, "array[-1]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("{\"foo\":\"bar\"}", Util::match_match(op.match()));
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

TEST_F(OpTests, testArrayMultivalue)
{
    string doc = "{\"array\":[4,5,6]}";
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

TEST_F(OpTests, testArrayOpsNested)
{
    const string array("[0,[1,[2]],{\"key\":\"val\"}]");
    op.set_doc(array);
    Error rv;

    rv = runOp(Command::REMOVE, "[1][1][0]");
    EXPECT_TRUE(rv.success());
    EXPECT_EQ("[0,[1,[]],{\"key\":\"val\"}]", getNewDoc());

    string array2;
    getAssignNewDoc(array2);
    rv = runOp(Command::REMOVE, "[1][1]");
    EXPECT_TRUE(rv.success());
    EXPECT_EQ("[0,[1],{\"key\":\"val\"}]", getNewDoc());
}

// Toplevel array with two elements.
TEST_F(OpTests, testArrayDelete)
{
    // Toplevel array deletions
    const string array("[1,2]");
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
    const string array2("[1]");
    op.set_doc(array2);

    rv = runOp(Command::REMOVE, "[0]");
    EXPECT_TRUE(rv.success());
    EXPECT_EQ("[]", getNewDoc());

    // Delete last element via [-1].
    rv = runOp(Command::REMOVE, "[-1]");
    EXPECT_TRUE(rv.success());
    EXPECT_EQ("[]", getNewDoc());
}

TEST_F(OpTests, testDictDelete)
{
    const string dict("{\"0\": 1,\"1\": 2.0}");
    op.set_doc(dict);
    Error rv;

    // Delete element
    rv = runOp(Command::REMOVE, "0");
    EXPECT_TRUE(rv.success());

    // Check it's gone.
    string doc;
    getAssignNewDoc(doc);
    rv = runOp(Command::EXISTS, "0");
    ASSERT_EQ(Error::PATH_ENOENT, rv);
}

TEST_F(OpTests, testUnique)
{
    string json = "{}";
    string doc;
    Error rv;

    op.set_doc(json);

    rv = runOp(Command::ARRAY_ADD_UNIQUE_P, "unique", "\"value\"");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runOp(Command::ARRAY_ADD_UNIQUE, "unique", "\"value\"");
    ASSERT_EQ(Error::DOC_EEXISTS, rv);

    rv = runOp(Command::ARRAY_ADD_UNIQUE, "unique", "1");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runOp(Command::ARRAY_ADD_UNIQUE, "unique", "\"1\"");
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
    ASSERT_EQ(Error::PATH_MISMATCH, rv) <<
            "Mismatch with array containing non-primitive elements";
}

TEST_F(OpTests, testUniqueToplevel)
{
    string json("[]");
    string doc;
    Error rv;

    op.set_doc(json);

    rv = runOp(Command::ARRAY_ADD_UNIQUE_P, "", "0");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runOp(Command::ARRAY_ADD_UNIQUE_P, "", "0");
    ASSERT_EQ(Error::DOC_EEXISTS, rv);
}

#ifndef INT64_MIN
#define INT64_MIN (-9223372036854775807LL-1)
#define INT64_MAX 9223372036854775807LL
#endif

TEST_F(OpTests, testNumeric)
{
    string doc = "{}";
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
    string dummy = std::to_string(INT64_MAX);
    rv = runOp(Command::COUNTER, "counter", dummy.c_str());
    ASSERT_TRUE(rv.success());
    ASSERT_EQ(std::to_string(INT64_MAX-99), Util::match_match(op.match()));
    getAssignNewDoc(doc);

    dummy = "-" + std::to_string(INT64_MAX-99);
    rv = runOp(Command::COUNTER, "counter", dummy.c_str());
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("0", Util::match_match(op.match()));
    getAssignNewDoc(doc);

    rv = runOp(Command::DICT_ADD_P, "counter2", "9999999999999999999999999999999");
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

TEST_F(OpTests, testBadNumFormat)
{
    string doc = "{}";
    op.set_doc(doc);

    ASSERT_EQ(Error::VALUE_EBADNUMBER, runOp(Command::COUNTER_P, "pth", "bad"));
    ASSERT_EQ(Error::VALUE_EBADNUMBER, runOp(Command::COUNTER_P, "pth", "3.14"));
    ASSERT_EQ(Error::VALUE_EBADNUMBER, runOp(Command::COUNTER_P, "pth", "-"));
    ASSERT_EQ(Error::VALUE_EBADNUMBER, runOp(Command::COUNTER_P, "pth", "43f"));
    ASSERT_EQ(Error::VALUE_EZERODELTA, runOp(Command::COUNTER_P, "pth", "0"));
}

TEST_F(OpTests, testNumericLimits)
{
    // Check we can increment from int64_t::max()-1 to max() successfully.
    const int64_t max = std::numeric_limits<int64_t>::max();
    const string one_minus_max("{\"counter\":" + std::to_string(max - 1) + "}");
    op.set_doc(one_minus_max);

    Error rv = runOp(Command::COUNTER, "counter", "1");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ(std::to_string(max), Util::match_match(op.match()));

    // Incrementing across the limit (max()-1 incremented by 2) should fail.
    op.set_doc(one_minus_max);

    rv = runOp(Command::COUNTER, "counter", "2");
    ASSERT_EQ(Error::DELTA_E2BIG, rv);

    // Same for int64_t::min() - 1 and decrement.
    const int64_t min = std::numeric_limits<int64_t>::min();
    const string one_plus_min("{\"counter\":" + std::to_string(min + 1) + "}");
    op.set_doc(one_plus_min);

    rv = runOp(Command::COUNTER, "counter", "-1");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ(std::to_string(min), Util::match_match(op.match()));

    // Decrementing across the limit (min()-1 decremented by 2) should fail.
    op.set_doc(one_plus_min);

    rv = runOp(Command::COUNTER, "counter", "-2");
    ASSERT_EQ(Error::DELTA_E2BIG, rv);
}

TEST_F(OpTests, testValueValidation)
{
    string json = "{}";
    string doc;
    Error rv;
    op.set_doc(doc);

    rv = runOp(Command::DICT_ADD_P, "foo.bar.baz", "INVALID");
    ASSERT_EQ(Error::VALUE_CANTINSERT, rv);

    rv = runOp(Command::DICT_ADD_P, "foo.bar.baz", "1,2,3,4");
    ASSERT_EQ(Error::VALUE_CANTINSERT, rv);

    // FIXME: Should we allow this? Could be more performant, but might also
    // be confusing!
    rv = runOp(Command::DICT_ADD_P, "foo.bar.baz", "1,\"k2\":2");
    ASSERT_TRUE(rv.success());

    // Dict key without a colon or value.
    rv = runOp(Command::DICT_ADD, "bad_dict", "{ \"foo\" }");
    ASSERT_EQ(Error::VALUE_CANTINSERT, rv);

    rv = runOp(Command::DICT_ADD, "bad_dict", "{ \"foo\": }");
    ASSERT_EQ(Error::VALUE_CANTINSERT, rv);

    // Dict without a colon or value.
    rv = runOp(Command::DICT_ADD_P, "bad_dict", "{ \"foo\" }");
    EXPECT_EQ(Error::VALUE_CANTINSERT, rv);

    // Dict without a colon.
    rv = runOp(Command::DICT_ADD_P, "bad_dict", "{ \"foo\": }");
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


TEST_F(OpTests, testNegativeIndex)
{
    string json = "[1,2,3,4,5,6]";
    op.set_doc(json);

    Error rv = runOp(Command::GET, "[-1]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("6", Util::match_match(op.match()));

    json = "[1,2,3,[4,5,6,[7,8,9]]]";
    op.set_doc(json);
    rv = runOp(Command::GET, "[-1].[-1].[-1]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("9", Util::match_match(op.match()));

    string doc;
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
    json = "{\"k1\": [\"first\", {\"k2\":[6,7,8]},\"last\"] }";
    op.set_doc(json);

    rv = runOp(Command::GET, "k1[-1]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("\"last\"", Util::match_match(op.match()));

    rv = runOp(Command::GET, "k1[1].k2[-1]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("8", Util::match_match(op.match()));
}

TEST_F(OpTests, testRootOps)
{
    string json = "[]";
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

TEST_F(OpTests, testMismatch)
{
    string doc = "{}";
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

TEST_F(OpTests, testWhitespace)
{
    string doc = "[ 1, 2, 3,       4        ]";
    op.set_doc(doc);
    Error rv;

    rv = runOp(Command::GET, "[-1]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("4", Util::match_match(op.match()));
}

TEST_F(OpTests, testTooDeep)
{
    std::string deep = "{\"array\":";
    for (size_t ii = 0; ii < (Limits::MAX_COMPONENTS+1) * 2; ii++) {
        deep += "[";
    }
    for (size_t ii = 0; ii < (Limits::MAX_COMPONENTS+1) * 2; ii++) {
        deep += "]";
    }

    op.set_doc(deep);

    Error rv = runOp(Command::GET, "dummy.path");
    ASSERT_EQ(Error::DOC_ETOODEEP, rv);

    // Try with a really deep path:
    std::string dp = "dummy";
    for (size_t ii = 0; ii < (Limits::MAX_COMPONENTS+1) * 2; ii++) {
        dp += ".dummy";
    }
    rv = runOp(Command::GET, dp.c_str());
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
    for (size_t depth = 2; depth < Limits::MAX_COMPONENTS -1; depth++) {
        one_less_max_path += std::string(".") + std::to_string(depth);
    }

    const std::string max_valid_path(one_less_max_path + "." +
                                     std::to_string(Limits::MAX_COMPONENTS-1));
    // Assert we can access elements at the max depth (before we start
    // attempting to add more).
    Error rv = runOp(Command::GET, max_valid_path.c_str());
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("{}", Util::match_match(op.match()));

    // Should be able to add an element as the same level as the max.
    const std::string equal_max_path(one_less_max_path + ".sibling_max");
    rv = runOp(Command::DICT_ADD, equal_max_path.c_str(), "\"also at max depth\"");
    EXPECT_TRUE(rv.success()) << rv;
    std::string newDoc;
    getAssignNewDoc(newDoc);

    // Attempts to add one level deeper should fail.
    std::string too_long_path(max_valid_path + ".too_long");
    rv = runOp(Command::DICT_ADD, too_long_path.c_str(), "\"past max depth\"");
    EXPECT_EQ(Error::PATH_E2BIG, rv);
}

TEST_F(OpTests, testArrayInsert) {
    string doc("[1,2,4,5]");
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
    ASSERT_EQ(Error::PATH_EINVAL, rv) << "Terminal negative index invalid for insert.";

    // Insert at out-of-bounds element
    doc = "[1,2,3]";
    op.set_doc(doc);
    rv = runOp(Command::ARRAY_INSERT, "[4]", "null");
    ASSERT_EQ(Error::PATH_ENOENT, rv) << "Fails with out-of-bound index";

    // Insert not using array syntax
    rv = runOp(Command::ARRAY_INSERT, "[0].anything", "null");
    ASSERT_EQ(Error::PATH_MISMATCH, rv) << "Using non-array parent in path fails";

    doc = "{}";
    op.set_doc(doc);
    // Insert with missing parent
    rv = runOp(Command::ARRAY_INSERT, "non_exist[0]", "null");
    ASSERT_EQ(Error::PATH_ENOENT, rv) << "Fails with missing parent";

    doc = "[]";
    op.set_doc(doc);
    rv = runOp(Command::ARRAY_INSERT, "[0]", "blah");
    ASSERT_EQ(Error::VALUE_CANTINSERT, rv) << "CANT_INSERT on invalid JSON value";

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
    string doc = "{}";
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

TEST_F(OpTests, testDeleteNestedArray)
{
    string doc = "[0,[10,20,[100]],{\"key\":\"value\"}]";
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
