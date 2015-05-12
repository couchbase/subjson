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
#define INCLUDE_SUBDOC_NTOHLL
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

class OpTests : public ::testing::Test {
protected:
  virtual void SetUp() {
      op.clear();
  }

  Operation op;

  string getNewDoc();
  void getAssignNewDoc(string& newdoc);
  Error runOp(Command, const char *path, const char *value = NULL, size_t nvalue = 0);
  Error runNumop(Command, const char *path, uint64_t delta);
};

string
OpTests::getNewDoc()
{
    string ret;
    for (size_t ii = 0; ii < op.doc_new_len; ii++) {
        const Loc& loc = op.doc_new[ii];
        ret.append(loc.at, loc.length);
    }

    // validate
    int rv = Validator::validate(ret, op.jsn);
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
    return op.op_exec(path, strlen(path));
}

Error
OpTests::runNumop(Command opcode, const char *path, uint64_t delta)
{
    uint64_t ntmp = htonll(delta);
    return runOp(opcode, path, (const char *)&ntmp, sizeof ntmp);
}

#include "big_json.inc.h"
TEST_F(OpTests, testOperations)
{
    string newdoc;
    op.set_doc(SAMPLE_big_json, strlen(SAMPLE_big_json));
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "name"));
    ASSERT_EQ("\"Allagash Brewing\"", Util::match_match(op.match));
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
    ASSERT_EQ("\"123 Main St.\"", Util::match_match(op.match));

    // Replace the value now:
    rv = runOp(Command::REPLACE, "address", "\"33 Marginal Rd.\"");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(newdoc);
    ASSERT_EQ(Error::SUCCESS, runOp(Command::GET, "address"));
    ASSERT_EQ("\"33 Marginal Rd.\"", Util::match_match(op.match));

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
    ASSERT_EQ("\"USA\"", Util::match_match(op.match));
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
    ASSERT_EQ("1", Util::match_match(op.match));

    rv = runOp(Command::ARRAY_PREPEND, "array", "0");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runOp(Command::GET, "array[0]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("0", Util::match_match(op.match));
    rv = runOp(Command::GET, "array[1]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("1", Util::match_match(op.match));

    rv = runOp(Command::ARRAY_APPEND, "array", "2");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runOp(Command::GET, "array[2]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("2", Util::match_match(op.match));

    rv = runOp(Command::ARRAY_APPEND, "array", "{\"foo\":\"bar\"}");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runOp(Command::GET, "array[3].foo");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("\"bar\"", Util::match_match(op.match));

    // Test the various POP commands
    rv = runOp(Command::REMOVE, "array[0]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("0", Util::match_match(op.match));
    getAssignNewDoc(doc);

    rv = runOp(Command::GET, "array[0]");

    rv = runOp(Command::REMOVE, "array[-1]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("{\"foo\":\"bar\"}", Util::match_match(op.match));
    getAssignNewDoc(doc);

    rv = runOp(Command::REMOVE, "array[-1]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("2", Util::match_match(op.match));
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
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runOp(Command::ARRAY_ADD_UNIQUE, "unique", "2");
    ASSERT_EQ(Error::PATH_MISMATCH, rv);
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
    rv = runNumop( Command::INCREMENT_P, "counter", 1);
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runNumop( Command::DECREMENT, "counter", 101);
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("-100", Util::match_match(op.match));
    getAssignNewDoc(doc);

    // Get it raw
    rv = runOp(Command::GET, "counter");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("-100", Util::match_match(op.match));

    rv = runNumop( Command::INCREMENT, "counter", 1);
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("-99", Util::match_match(op.match));
    getAssignNewDoc(doc);

    // Try with other things
    rv = runNumop( Command::DECREMENT, "counter", INT64_MIN);
    ASSERT_EQ(Error::DELTA_E2BIG, rv);

    rv = runNumop( Command::DECREMENT, "counter", INT64_MAX-99);
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runNumop( Command::INCREMENT, "counter", INT64_MAX);
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("0", Util::match_match(op.match));
    getAssignNewDoc(doc);

    rv = runOp(Command::DICT_ADD_P, "counter2", "9999999999999999999999999999999");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runNumop( Command::INCREMENT, "counter2", 1);
    ASSERT_EQ(Error::NUM_E2BIG, rv);

    rv = runOp(Command::DICT_ADD_P, "counter3", "3.14");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runNumop( Command::INCREMENT, "counter3", 1);
    ASSERT_EQ(Error::PATH_MISMATCH, rv);

    doc = "[]";
    op.set_doc(doc);
    rv = runNumop( Command::INCREMENT, "[0]", 42);
    ASSERT_EQ(Error::PATH_ENOENT, rv);

    // Try with a _P variant. Should still be the same
    rv = runNumop( Command::INCREMENT_P, "[0]", 42);
    ASSERT_EQ(Error::PATH_ENOENT, rv);

    rv = runOp(Command::ARRAY_APPEND, "", "-20");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(doc);

    rv = runNumop( Command::INCREMENT, "[0]", 1);
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("-19", Util::match_match(op.match));
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
    ASSERT_EQ("6", Util::match_match(op.match));

    json = "[1,2,3,[4,5,6,[7,8,9]]]";
    op.set_doc(json);
    rv = runOp(Command::GET, "[-1].[-1].[-1]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("9", Util::match_match(op.match));

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
    ASSERT_EQ("10", Util::match_match(op.match));

    // Intermixed paths:
    json = "{\"k1\": [\"first\", {\"k2\":[6,7,8]},\"last\"] }";
    op.set_doc(json);

    rv = runOp(Command::GET, "k1[-1]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("\"last\"", Util::match_match(op.match));

    rv = runOp(Command::GET, "k1[1].k2[-1]");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("8", Util::match_match(op.match));
}

TEST_F(OpTests, testRootOps)
{
    string json = "[]";
    op.set_doc(json);
    Error rv;

    rv = runOp(Command::GET, "");
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("[]", Util::match_match(op.match));

    rv = runOp(Command::ARRAY_APPEND, "", "null");
    ASSERT_TRUE(rv.success());
    getAssignNewDoc(json);

    rv = runOp(Command::GET, "");
    ASSERT_EQ("[null]", Util::match_match(op.match));

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
    ASSERT_EQ("4", Util::match_match(op.match));
}

TEST_F(OpTests, testTooDeep)
{
    std::string deep = "{\"array\":";
    for (size_t ii = 0; ii < COMPONENTS_ALLOC * 2; ii++) {
        deep += "[";
    }
    for (size_t ii = 0; ii < COMPONENTS_ALLOC * 2; ii++) {
        deep += "]";
    }

    op.set_doc(deep);

    Error rv = runOp(Command::GET, "dummy.path");
    ASSERT_EQ(Error::DOC_ETOODEEP, rv);

    // Try with a really deep path:
    std::string dp = "dummy";
    for (size_t ii = 0; ii < COMPONENTS_ALLOC * 2; ii++) {
        dp += ".dummy";
    }
    rv = runOp(Command::GET, dp.c_str());
    ASSERT_EQ(Error::PATH_E2BIG, rv);
}

TEST_F(OpTests, testTooDeepDict) {
    // Verify that we cannot create too deep a document with DICT_ADD
    // Should be able to do the maximum depth:
    std::string deep_dict("{");
    for (size_t ii = 1; ii < COMPONENTS_ALLOC - 1; ii++) {
        deep_dict += "\"" + std::to_string(ii) + "\": {";
    }
    for (size_t ii = 0; ii < COMPONENTS_ALLOC - 1; ii++) {
        deep_dict += "}";
    }
    op.set_doc(deep_dict);

    // Create base path at one less than the max.
    std::string one_less_max_path(std::to_string(1));
    for (int depth = 2; depth < COMPONENTS_ALLOC - 2; depth++) {
        one_less_max_path += std::string(".") + std::to_string(depth);
    }

    const std::string max_valid_path(one_less_max_path + "." +
                                     std::to_string(COMPONENTS_ALLOC - 2));
    // Assert we can access elements at the max depth (before we start
    // attempting to add more).
    Error rv = runOp(Command::GET, max_valid_path.c_str());
    ASSERT_TRUE(rv.success());
    ASSERT_EQ("{}", Util::match_match(op.match));

    // Should be able to add an element as the same level as the max.
    const std::string equal_max_path(one_less_max_path + ".sibling_max");
    rv = runOp(Command::DICT_ADD, equal_max_path.c_str(),
                      "\"also at max depth\"");
    EXPECT_TRUE(rv.success());
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
    rv = runOp(Command::ARRAY_INSERT, "[-1]", "4");
    ASSERT_TRUE(rv.success()) << "Insert at position [-1] OK";
    getAssignNewDoc(doc);
    ASSERT_EQ("[1,2,3,4,5]", doc) << "Insert at position [-1] matches";

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
