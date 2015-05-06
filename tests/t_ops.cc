#define INCLUDE_SUBDOC_NTOHLL
#include "subdoc-tests-common.h"

using std::string;
using std::cerr;
using std::endl;
using Subdoc::Operation;
using Subdoc::Match;
using Subdoc::Loc;
using Subdoc::Error;

class OpTests : public ::testing::Test {
protected:
  virtual void SetUp() {
      op.clear();
  }

  Operation op;
};

static string
getNewDoc(const Operation& op)
{
    string ret;
    for (size_t ii = 0; ii < op.doc_new_len; ii++) {
        const Loc& loc = op.doc_new[ii];
        ret.append(loc.at, loc.length);
    }

    // validate
    jsonsl_error_t rv = Match::validate(
        ret.c_str(), ret.size(), op.jsn, SUBDOC_VALIDATE_PARENT_NONE);
    EXPECT_EQ(JSONSL_ERROR_SUCCESS, rv) << t_subdoc::getJsnErrstr(rv);
    return ret;
}

static void
getAssignNewDoc(Operation& op, string& newdoc)
{
    newdoc = getNewDoc(op);
    op.set_doc(newdoc);
}

static Error
performNewOp(Operation& op, subdoc_OPTYPE opcode, const char *path, const char *value = NULL, size_t nvalue = 0)
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

static Error
performArith(Operation& op, subdoc_OPTYPE opcode, const char *path, uint64_t delta)
{
    uint64_t ntmp = htonll(delta);
    return performNewOp(op, opcode, path, (const char *)&ntmp, sizeof ntmp);
}

#include "big_json.inc.h"
TEST_F(OpTests, testOperations)
{
    string newdoc;
    op.set_doc(SAMPLE_big_json, strlen(SAMPLE_big_json));
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, performNewOp(op, SUBDOC_CMD_GET, "name"));
    ASSERT_EQ("\"Allagash Brewing\"", t_subdoc::getMatchString(op.match));
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, performNewOp(op, SUBDOC_CMD_EXISTS, "name"));

    subdoc_ERRORS rv = performNewOp(op, SUBDOC_CMD_DELETE, "address");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);

    getAssignNewDoc(op, newdoc);
    // Should return in KEY_ENOENT
    ASSERT_EQ(SUBDOC_STATUS_PATH_ENOENT, performNewOp(op, SUBDOC_CMD_GET, "address"));

    // Insert something back, maybe :)
    rv = performNewOp(op, SUBDOC_CMD_DICT_ADD, "address", "\"123 Main St.\"");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);

    getAssignNewDoc(op, newdoc);
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, performNewOp(op, SUBDOC_CMD_GET, "address"));
    ASSERT_EQ("\"123 Main St.\"", t_subdoc::getMatchString(op.match));

    // Replace the value now:
    rv = performNewOp(op, SUBDOC_CMD_REPLACE, "address", "\"33 Marginal Rd.\"");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, newdoc);
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, performNewOp(op, SUBDOC_CMD_GET, "address"));
    ASSERT_EQ("\"33 Marginal Rd.\"", t_subdoc::getMatchString(op.match));

    // Get it back:
    op.set_doc(SAMPLE_big_json, strlen(SAMPLE_big_json));
    // add non-existent path
    rv = performNewOp(op, SUBDOC_CMD_DICT_ADD, "foo.bar.baz", "[1,2,3]");
    ASSERT_EQ(SUBDOC_STATUS_PATH_ENOENT, rv);

    rv = performNewOp(op, SUBDOC_CMD_DICT_ADD_P, "foo.bar.baz", "[1,2,3]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getNewDoc(op);
}

// Mainly checks that we can perform generic DELETE and GET operations
// on array indices
TEST_F(OpTests, testGenericOps)
{
    op.set_doc(SAMPLE_big_json, strlen(SAMPLE_big_json));
    subdoc_ERRORS rv;
    string newdoc;

    rv = performNewOp(op, SUBDOC_CMD_DELETE, "address[0]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);

    getAssignNewDoc(op, newdoc);
    rv = performNewOp(op, SUBDOC_CMD_GET, "address[0]");
    ASSERT_EQ(SUBDOC_STATUS_PATH_ENOENT, rv);

    rv = performNewOp(op, SUBDOC_CMD_REPLACE, "address",
        "[\"500 B St.\", \"Anytown\", \"USA\"]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, newdoc);
    rv = performNewOp(op, SUBDOC_CMD_GET, "address[2]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("\"USA\"", t_subdoc::getMatchString(op.match));
}

TEST_F(OpTests, testListOps)
{
    string doc = "{}";
    op.set_doc(doc);

    subdoc_ERRORS rv = performNewOp(op, SUBDOC_CMD_DICT_UPSERT, "array", "[]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    // Test append:
    rv = performNewOp(op, SUBDOC_CMD_ARRAY_APPEND, "array", "1");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    rv = performNewOp(op, SUBDOC_CMD_GET, "array[0]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("1", t_subdoc::getMatchString(op.match));

    rv = performNewOp(op, SUBDOC_CMD_ARRAY_PREPEND, "array", "0");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    rv = performNewOp(op, SUBDOC_CMD_GET, "array[0]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("0", t_subdoc::getMatchString(op.match));
    rv = performNewOp(op, SUBDOC_CMD_GET, "array[1]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("1", t_subdoc::getMatchString(op.match));

    rv = performNewOp(op, SUBDOC_CMD_ARRAY_APPEND, "array", "2");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    rv = performNewOp(op, SUBDOC_CMD_GET, "array[2]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("2", t_subdoc::getMatchString(op.match));

    rv = performNewOp(op, SUBDOC_CMD_ARRAY_APPEND, "array", "{\"foo\":\"bar\"}");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    rv = performNewOp(op, SUBDOC_CMD_GET, "array[3].foo");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("\"bar\"", t_subdoc::getMatchString(op.match));

    // Test the various POP commands
    rv = performNewOp(op, SUBDOC_CMD_DELETE, "array[0]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("0", t_subdoc::getMatchString(op.match));
    getAssignNewDoc(op, doc);

    rv = performNewOp(op, SUBDOC_CMD_GET, "array[0]");

    rv = performNewOp(op, SUBDOC_CMD_DELETE, "array[-1]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("{\"foo\":\"bar\"}", t_subdoc::getMatchString(op.match));
    getAssignNewDoc(op, doc);

    rv = performNewOp(op, SUBDOC_CMD_DELETE, "array[-1]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("2", t_subdoc::getMatchString(op.match));
}

TEST_F(OpTests, testArrayOpsNested)
{
    const string array("[0,[1,[2]],{\"key\":\"val\"}]");
    op.set_doc(array);
    Error rv;

    rv = performNewOp(op, SUBDOC_CMD_DELETE, "[1][1][0]");
    EXPECT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    EXPECT_EQ("[0,[1,[]],{\"key\":\"val\"}]", getNewDoc(op));

    string array2;
    getAssignNewDoc(op, array2);
    rv = performNewOp(op, SUBDOC_CMD_DELETE, "[1][1]");
    EXPECT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    EXPECT_EQ("[0,[1],{\"key\":\"val\"}]", getNewDoc(op));
}

TEST_F(OpTests, testUnique)
{
    string json = "{}";
    string doc;
    Error rv;

    op.set_doc(json);

    rv = performNewOp(op, SUBDOC_CMD_ARRAY_ADD_UNIQUE_P, "unique", "\"value\"");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    rv = performNewOp(op, SUBDOC_CMD_ARRAY_ADD_UNIQUE, "unique", "\"value\"");
    ASSERT_EQ(SUBDOC_STATUS_DOC_EEXISTS, rv);

    rv = performNewOp(op, SUBDOC_CMD_ARRAY_ADD_UNIQUE, "unique", "1");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    rv = performNewOp(op, SUBDOC_CMD_ARRAY_ADD_UNIQUE, "unique", "\"1\"");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    rv = performNewOp(op, SUBDOC_CMD_ARRAY_ADD_UNIQUE, "unique", "[]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    rv = performNewOp(op, SUBDOC_CMD_ARRAY_ADD_UNIQUE, "unique", "2");
    ASSERT_EQ(SUBDOC_STATUS_PATH_MISMATCH, rv);
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
    rv = performArith(op, SUBDOC_CMD_INCREMENT_P, "counter", 1);
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    rv = performArith(op, SUBDOC_CMD_DECREMENT, "counter", 101);
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("-100", t_subdoc::getMatchString(op.match));
    getAssignNewDoc(op, doc);

    // Get it raw
    rv = performNewOp(op, SUBDOC_CMD_GET, "counter");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("-100", t_subdoc::getMatchString(op.match));

    rv = performArith(op, SUBDOC_CMD_INCREMENT, "counter", 1);
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("-99", t_subdoc::getMatchString(op.match));
    getAssignNewDoc(op, doc);

    // Try with other things
    rv = performArith(op, SUBDOC_CMD_DECREMENT, "counter", INT64_MIN);
    ASSERT_EQ(SUBDOC_STATUS_DELTA_E2BIG, rv);

    rv = performArith(op, SUBDOC_CMD_DECREMENT, "counter", INT64_MAX-99);
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    rv = performArith(op, SUBDOC_CMD_INCREMENT, "counter", INT64_MAX);
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("0", t_subdoc::getMatchString(op.match));
    getAssignNewDoc(op, doc);

    rv = performNewOp(op, SUBDOC_CMD_DICT_ADD_P, "counter2", "9999999999999999999999999999999");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    rv = performArith(op, SUBDOC_CMD_INCREMENT, "counter2", 1);
    ASSERT_EQ(SUBDOC_STATUS_NUM_E2BIG, rv);

    rv = performNewOp(op, SUBDOC_CMD_DICT_ADD_P, "counter3", "3.14");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    rv = performArith(op, SUBDOC_CMD_INCREMENT, "counter3", 1);
    ASSERT_EQ(SUBDOC_STATUS_PATH_MISMATCH, rv);

    doc = "[]";
    op.set_doc(doc);
    rv = performArith(op, SUBDOC_CMD_INCREMENT, "[0]", 42);
    ASSERT_EQ(SUBDOC_STATUS_PATH_ENOENT, rv);

    // Try with a _P variant. Should still be the same
    rv = performArith(op, SUBDOC_CMD_INCREMENT_P, "[0]", 42);
    ASSERT_EQ(SUBDOC_STATUS_PATH_ENOENT, rv);

    rv = performNewOp(op, SUBDOC_CMD_ARRAY_APPEND, "", "-20");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    rv = performArith(op, SUBDOC_CMD_INCREMENT, "[0]", 1);
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("-19", t_subdoc::getMatchString(op.match));
}

TEST_F(OpTests, testValueValidation)
{
    string json = "{}";
    string doc;
    Error rv;
    op.set_doc(doc);

    rv = performNewOp(op, SUBDOC_CMD_DICT_ADD_P, "foo.bar.baz", "INVALID");
    ASSERT_EQ(SUBDOC_STATUS_VALUE_CANTINSERT, rv);

    rv = performNewOp(op, SUBDOC_CMD_DICT_ADD_P, "foo.bar.baz", "1,2,3,4");
    ASSERT_EQ(SUBDOC_STATUS_VALUE_CANTINSERT, rv);

    // FIXME: Should we allow this? Could be more performant, but might also
    // be confusing!
    rv = performNewOp(op, SUBDOC_CMD_DICT_ADD_P, "foo.bar.baz", "1,\"k2\":2");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);

    // Dict key without a colon or value.
    rv = performNewOp(op, SUBDOC_CMD_DICT_ADD, "bad_dict", "{ \"foo\" }");
    ASSERT_EQ(SUBDOC_STATUS_VALUE_CANTINSERT, rv);

    rv = performNewOp(op, SUBDOC_CMD_DICT_ADD, "bad_dict", "{ \"foo\": }");
    ASSERT_EQ(SUBDOC_STATUS_VALUE_CANTINSERT, rv);

    // Dict without a colon or value.
    rv = performNewOp(op, SUBDOC_CMD_DICT_ADD_P, "bad_dict", "{ \"foo\" }");
    EXPECT_EQ(SUBDOC_STATUS_VALUE_CANTINSERT, rv);

    // Dict without a colon.
    rv = performNewOp(op, SUBDOC_CMD_DICT_ADD_P, "bad_dict", "{ \"foo\": }");
    EXPECT_EQ(SUBDOC_STATUS_VALUE_CANTINSERT, rv);

    // null with incorrect name.
    rv = performNewOp(op, SUBDOC_CMD_DICT_ADD_P, "bad_null", "nul");
    EXPECT_EQ(SUBDOC_STATUS_VALUE_CANTINSERT, rv);

    // invalid float (more than one decimal point).
    rv = performNewOp(op, SUBDOC_CMD_DICT_ADD_P, "bad_float1", "2.0.0");
    EXPECT_EQ(SUBDOC_STATUS_VALUE_CANTINSERT, rv);

    // invalid float (no digit after the '.').
    rv = performNewOp(op, SUBDOC_CMD_DICT_ADD_P, "bad_float2", "2.");
    EXPECT_EQ(SUBDOC_STATUS_VALUE_CANTINSERT, rv);

    // invalid float (no exponential after the 'e').
    rv = performNewOp(op, SUBDOC_CMD_DICT_ADD_P, "bad_float3", "2.0e");
    EXPECT_EQ(SUBDOC_STATUS_VALUE_CANTINSERT, rv);

    // invalid float (no digits after the exponential sign).
    rv = performNewOp(op, SUBDOC_CMD_DICT_ADD_P, "bad_float4", "2.0e+");
    EXPECT_EQ(SUBDOC_STATUS_VALUE_CANTINSERT, rv);
}


TEST_F(OpTests, testNegativeIndex)
{
    string json = "[1,2,3,4,5,6]";
    op.set_doc(json);

    subdoc_ERRORS rv = performNewOp(op, SUBDOC_CMD_GET, "[-1]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("6", t_subdoc::getMatchString(op.match));

    json = "[1,2,3,[4,5,6,[7,8,9]]]";
    op.set_doc(json);
    rv = performNewOp(op, SUBDOC_CMD_GET, "[-1].[-1].[-1]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("9", t_subdoc::getMatchString(op.match));

    string doc;
    rv = performNewOp(op, SUBDOC_CMD_DELETE, "[-1].[-1].[-1]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);

    // Can we PUSH values with a negative index?
    rv = performNewOp(op, SUBDOC_CMD_ARRAY_APPEND, "[-1].[-1]", "10");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, doc);


    rv = performNewOp(op, SUBDOC_CMD_GET, "[-1].[-1].[-1]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("10", t_subdoc::getMatchString(op.match));

    // Intermixed paths:
    json = "{\"k1\": [\"first\", {\"k2\":[6,7,8]},\"last\"] }";
    op.set_doc(json);

    rv = performNewOp(op, SUBDOC_CMD_GET, "k1[-1]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("\"last\"", t_subdoc::getMatchString(op.match));

    rv = performNewOp(op, SUBDOC_CMD_GET, "k1[1].k2[-1]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("8", t_subdoc::getMatchString(op.match));
}

TEST_F(OpTests, testRootOps)
{
    string json = "[]";
    op.set_doc(json);
    subdoc_ERRORS rv;

    rv = performNewOp(op, SUBDOC_CMD_GET, "");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("[]", t_subdoc::getMatchString(op.match));

    rv = performNewOp(op, SUBDOC_CMD_ARRAY_APPEND, "", "null");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    getAssignNewDoc(op, json);

    rv = performNewOp(op, SUBDOC_CMD_GET, "");
    ASSERT_EQ("[null]", t_subdoc::getMatchString(op.match));

    // Deleting root element should be CANTINSERT
    rv = performNewOp(op, SUBDOC_CMD_DELETE, "");
    ASSERT_EQ(SUBDOC_STATUS_VALUE_CANTINSERT, rv);
}

TEST_F(OpTests, testMismatch)
{
    string doc = "{}";
    op.set_doc(doc);
    subdoc_ERRORS rv;

    rv = performNewOp(op, SUBDOC_CMD_ARRAY_APPEND, "", "null");
    ASSERT_EQ(SUBDOC_STATUS_PATH_MISMATCH, rv);

    doc = "[]";
    op.set_doc(doc);
    rv = performNewOp(op, SUBDOC_CMD_DICT_UPSERT, "", "blah");
    ASSERT_EQ(SUBDOC_STATUS_VALUE_CANTINSERT, rv);

    rv = performNewOp(op, SUBDOC_CMD_DICT_UPSERT, "key", "blah");
    ASSERT_EQ(SUBDOC_STATUS_VALUE_CANTINSERT, rv);

    doc = "[null]";
    op.set_doc(doc);
    rv = performNewOp(op, SUBDOC_CMD_DICT_UPSERT, "", "blah");
    ASSERT_EQ(SUBDOC_STATUS_VALUE_CANTINSERT, rv);

    rv = performNewOp(op, SUBDOC_CMD_DICT_UPSERT, "key", "blah");
    ASSERT_EQ(SUBDOC_STATUS_VALUE_CANTINSERT, rv);

    rv = performNewOp(op, SUBDOC_CMD_ARRAY_APPEND_P, "foo.bar", "null");
    ASSERT_EQ(SUBDOC_STATUS_PATH_MISMATCH, rv);
}

TEST_F(OpTests, testWhitespace)
{
    string doc = "[ 1, 2, 3,       4        ]";
    op.set_doc(doc);
    subdoc_ERRORS rv;

    rv = performNewOp(op, SUBDOC_CMD_GET, "[-1]");
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("4", t_subdoc::getMatchString(op.match));
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

    uint16_t rv = performNewOp(op, SUBDOC_CMD_GET, "dummy.path");
    ASSERT_EQ(SUBDOC_STATUS_DOC_ETOODEEP, rv);

    // Try with a really deep path:
    std::string dp = "dummy";
    for (size_t ii = 0; ii < COMPONENTS_ALLOC * 2; ii++) {
        dp += ".dummy";
    }
    rv = performNewOp(op, SUBDOC_CMD_GET, dp.c_str());
    ASSERT_EQ(SUBDOC_STATUS_PATH_E2BIG, rv);
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
    uint16_t rv = performNewOp(op, SUBDOC_CMD_GET, max_valid_path.c_str());
    ASSERT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    ASSERT_EQ("{}", t_subdoc::getMatchString(op.match));

    // Should be able to add an element as the same level as the max.
    const std::string equal_max_path(one_less_max_path + ".sibling_max");
    rv = performNewOp(op, SUBDOC_CMD_DICT_ADD, equal_max_path.c_str(),
                      "\"also at max depth\"");
    EXPECT_EQ(SUBDOC_STATUS_SUCCESS, rv);
    std::string newDoc;
    getAssignNewDoc(op, newDoc);

    // Attempts to add one level deeper should fail.
    std::string too_long_path(max_valid_path + ".too_long");
    std::cerr << "DEBUG doc: " << newDoc << std::endl;
    std::cerr << "DEBUG path:" << too_long_path << std::endl;
    rv = performNewOp(op, SUBDOC_CMD_DICT_ADD, too_long_path.c_str(),
                      "\"past max depth\"");
    EXPECT_EQ(SUBDOC_STATUS_PATH_E2BIG, rv);
}
