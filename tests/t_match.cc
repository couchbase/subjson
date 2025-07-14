/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
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

using std::string;
using std::cerr;
using std::endl;
using Subdoc::Path;
using Subdoc::Match;
using Subdoc::Util;

#define JQ(s) "\"" s "\""

class MatchTests : public ::testing::Test {
protected:
    static const std::string json;
    static jsonsl_t jsn;
    Path pth;
    Match m;

    static void SetUpTestCase() { jsn = Match::jsn_alloc(); }
    static void TearDownTestCase() { Match::jsn_free(jsn); }
    void SetUp() override {
        m.clear();
    }
};

jsonsl_t MatchTests::jsn = NULL;
const std::string MatchTests::json = "{"
        JQ("key1") ":" JQ("val1") ","
        JQ("subdict") ":{" JQ("subkey1") ":" JQ("subval1") "},"
        JQ("sublist") ":[" JQ("elem1") "," JQ("elem2") "," JQ("elem3") "],"
        JQ("nested_list") ":[  [" JQ("nested1") ",2,3,4,5,6,7,8,9,0]  ],"
        JQ("empty_list") ":[],"
        JQ("numbers") ":[1,2,3,4,5,6,7,8,9,0]" ","
        JQ("empty") ":{}" ","
        JQ("U\\u002DEscape") ":null"
       "}";

TEST_F(MatchTests, testToplevelDict)
{
    pth.parse("key1");
    m.exec_match(json, pth, jsn);
    ASSERT_EQ(JSONSL_MATCH_COMPLETE, m.matchres);
    ASSERT_EQ(JSONSL_ERROR_SUCCESS, m.status);
    ASSERT_EQ("\"val1\"", Util::match_match(m));
    ASSERT_EQ("\"key1\"", Util::match_key(m));
}

TEST_F(MatchTests, testNestedDict)
{
    pth.parse("subdict.subkey1");
    m.exec_match(json, pth, jsn);
    ASSERT_EQ(JSONSL_MATCH_COMPLETE, m.matchres);
    ASSERT_EQ(JSONSL_ERROR_SUCCESS, m.status);
    ASSERT_EQ("\"subval1\"", Util::match_match(m));
    ASSERT_EQ("\"subkey1\"", Util::match_key(m));
}

TEST_F(MatchTests, testArrayIndex)
{
    pth.parse("sublist[1]");
    m.exec_match(json, pth, jsn);
    ASSERT_EQ(JSONSL_MATCH_COMPLETE, m.matchres);
    ASSERT_EQ(JSONSL_ERROR_SUCCESS, m.status);
    ASSERT_EQ("\"elem2\"", Util::match_match(m));
    ASSERT_FALSE(m.has_key());
}

TEST_F(MatchTests, testMismatchArrayAsDict)
{
    pth.parse("key1[9]");
    m.exec_match(json, pth, jsn);
    ASSERT_EQ(JSONSL_MATCH_TYPE_MISMATCH, m.matchres);
}

TEST_F(MatchTests, testMismatchDictAsArray)
{
    pth.parse("subdict[0]");
    m.exec_match(json, pth, jsn);
    ASSERT_EQ(JSONSL_MATCH_TYPE_MISMATCH, m.matchres);
}

TEST_F(MatchTests, testMatchContainerValue)
{
    pth.parse("numbers");
    m.exec_match(json, pth, jsn);
    ASSERT_EQ(JSONSL_MATCH_COMPLETE, m.matchres);
    ASSERT_EQ("[1,2,3,4,5,6,7,8,9,0]", Util::match_match(m));
}

TEST_F(MatchTests, testFinalComponentNotFound)
{
    pth.parse("empty.field");
    m.exec_match(json, pth, jsn);
    ASSERT_NE(0, m.immediate_parent_found);
    ASSERT_EQ(2, m.match_level);
    ASSERT_EQ("{}", Util::match_parent(m));
}

TEST_F(MatchTests, testOOBArrayIndex)
{
    pth.parse("sublist[4]");
    m.exec_match(json, pth, jsn);
    ASSERT_NE(0, m.immediate_parent_found);
    ASSERT_EQ(2, m.match_level);
    ASSERT_EQ("[\"elem1\",\"elem2\",\"elem3\"]", Util::match_parent(m));
}

TEST_F(MatchTests, testAllComponentsNotFound)
{
    pth.parse("non.existent.path");
    m.exec_match(json, pth, jsn);
    ASSERT_EQ(0, m.immediate_parent_found);
    ASSERT_EQ(1, m.match_level);
    ASSERT_EQ(json, Util::match_parent(m));
}

TEST_F(MatchTests, testSingletonComponentNotFound)
{
    pth.parse("toplevel");
    m.exec_match(json, pth, jsn);
    ASSERT_EQ(1, m.match_level);
    ASSERT_NE(0, m.immediate_parent_found);
    ASSERT_EQ(json, Util::match_parent(m));
}

TEST_F(MatchTests, testUescape)
{
    // See if we can find the 'u-escape' here.
    m.clear();
    pth.parse("U-Escape");
    m.exec_match(json, pth, jsn);
    ASSERT_EQ(JSONSL_MATCH_COMPLETE, m.matchres);
    ASSERT_EQ("\"U\\u002DEscape\"", m.loc_key.to_string());
}

TEST_F(MatchTests, testGetLastElement)
{
    pth.parse("sublist");
    m.get_last = 1;
    m.exec_match(json, pth, jsn);

    // Ensure the last element actually matches..
    ASSERT_EQ(3, m.match_level);
    ASSERT_EQ(JSONSL_MATCH_COMPLETE, m.matchres);
    ASSERT_EQ(JSONSL_T_STRING, m.type);
    ASSERT_EQ("\"elem3\"", Util::match_match(m));
    ASSERT_EQ(2U, m.position);
    ASSERT_EQ(2U, m.num_siblings);

    m.clear();
    pth.parse("nested_list");
    m.get_last = 1;
    m.exec_match(json, pth, jsn);
    ASSERT_EQ(3U, m.match_level);
    ASSERT_EQ(JSONSL_MATCH_COMPLETE, m.matchres);
    ASSERT_EQ(JSONSL_T_LIST, m.type);

    ASSERT_EQ("[" JQ("nested1") ",2,3,4,5,6,7,8,9,0]", Util::match_match(m));
    ASSERT_EQ(10U, m.num_children);
    ASSERT_EQ(0U, m.num_siblings);
}

TEST_F(MatchTests, testGetNumSiblings)
{
    pth.parse("nested_list[0][0]");
    // By default, siblings are not extracted
    ASSERT_EQ(Match::GET_MATCH_ONLY, m.extra_options);
    m.exec_match(json, pth, jsn);
    ASSERT_EQ(JSONSL_MATCH_COMPLETE, m.matchres);
    ASSERT_EQ(JSONSL_T_STRING, m.type); // String
    ASSERT_EQ(0U, m.num_siblings);

    m.clear();
    ASSERT_EQ(Match::GET_MATCH_ONLY, m.extra_options);
    m.extra_options = Match::GET_FOLLOWING_SIBLINGS;
    m.exec_match(json, pth, jsn);
    ASSERT_EQ(JSONSL_MATCH_COMPLETE, m.matchres);
    ASSERT_TRUE(m.num_siblings > 0);
    ASSERT_FALSE(m.is_only());
    ASSERT_TRUE(m.is_first());
}

TEST_F(MatchTests, testGetPosition)
{
    const char *klist[] = {
            "key1", "subdict", "sublist", "nested_list",
            "empty_list", "numbers", "empty"
    };
    size_t nkeys = sizeof klist / sizeof (const char *);

    for (size_t ii = 0; ii < nkeys; ++ii) {
        m.clear();
        pth.parse(klist[ii]);
        m.exec_match(json, pth, jsn);
        ASSERT_EQ(JSONSL_MATCH_COMPLETE, m.matchres);
        ASSERT_EQ(ii, m.position);
    }
}

// It's important that we test this at the Match level as well, even though
// tests are already present at the 'Operation' and 'Path' level.
TEST_F(MatchTests, testNegativeIndex)
{
    pth.parse("sublist[-1]");
    m.exec_match(json, pth, jsn);
    ASSERT_EQ(JSONSL_MATCH_COMPLETE, m.matchres);
    ASSERT_EQ(JSONSL_T_STRING, m.type);
    ASSERT_EQ(2U, m.num_siblings);
    ASSERT_EQ(3U, m.match_level);
    ASSERT_EQ("\"elem3\"", Util::match_match(m));

    // Multiple nested elements..
    pth.parse("nested_list[-1][-1]");
    m.exec_match(json, pth, jsn);
    ASSERT_EQ(JSONSL_MATCH_COMPLETE, m.matchres);
    ASSERT_EQ(JSONSL_T_SPECIAL, m.type);
    ASSERT_EQ("0", Util::match_match(m));
    ASSERT_EQ(4U, m.match_level);
}

TEST_F(MatchTests, testMatchUnique)
{
    pth.parse("empty_list");
    m.ensure_unique.assign("key", 3);
    m.exec_match(json, pth, jsn);
    ASSERT_EQ(JSONSL_MATCH_COMPLETE, m.matchres);
    ASSERT_EQ(JSONSL_T_LIST, m.type);
    ASSERT_EQ(2U, m.match_level);
    ASSERT_FALSE(m.unique_item_found);
    ASSERT_EQ("[]", Util::match_match(m));

    // Test with non-empty path, item found
    m.clear();
    pth.parse("numbers");
    m.ensure_unique.assign("1", 1);
    m.exec_match(json, pth, jsn);
    ASSERT_EQ(JSONSL_MATCH_COMPLETE, m.matchres);
    ASSERT_TRUE(m.unique_item_found);

    // test with non-empty path, item not found
    m.clear();
    pth.parse("numbers");
    m.ensure_unique.assign("42", 2);
    m.exec_match(json, pth, jsn);
    ASSERT_EQ(JSONSL_MATCH_COMPLETE, m.matchres);
    ASSERT_FALSE(m.unique_item_found);
    ASSERT_NE(0U, m.num_children);
    ASSERT_EQ("[1,2,3,4,5,6,7,8,9,0]", Util::match_match(m));

    // Test with path mismatch:
    m.clear();
    pth.parse("empty");
    m.ensure_unique.assign("foo", 3);
    m.exec_match(json, pth, jsn);
    ASSERT_EQ(JSONSL_MATCH_TYPE_MISMATCH, m.matchres);

    // Test with negative index, not found
    m.clear();
    pth.parse("nested_list[-1]");
    m.ensure_unique.assign("foo", 3);
    m.exec_match(json, pth, jsn);
    ASSERT_EQ(JSONSL_MATCH_COMPLETE, m.matchres);
    ASSERT_FALSE(m.unique_item_found);
    ASSERT_NE(0U, m.num_children);
}
