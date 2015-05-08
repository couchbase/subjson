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

using std::string;
using std::cerr;
using std::endl;
using Subdoc::Path;
using Subdoc::Match;

#define JQ(s) "\"" s "\""

class MatchTests : public ::testing::Test {
protected:
    static const std::string json;
    static jsonsl_t jsn;
    static Path pth;
    static Match m;

    static void SetUpTestCase() { jsn = Match::jsn_alloc(); }
    static void TearDownTestCase() { Match::jsn_free(jsn); }
    virtual void SetUp() { m.clear(); }
};

Path MatchTests::pth;
Match MatchTests::m;
jsonsl_t MatchTests::jsn = NULL;
const std::string MatchTests::json = "{"
        JQ("key1") ":" JQ("val1") ","
        JQ("subdict") ":{" JQ("subkey1") ":" JQ("subval1") "},"
        JQ("sublist") ":[" JQ("elem1") "," JQ("elem2") "," JQ("elem3") "],"
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
    ASSERT_EQ("\"val1\"", t_subdoc::getMatchString(m));
    ASSERT_EQ("\"key1\"", t_subdoc::getMatchKey(m));
}

TEST_F(MatchTests, testNestedDict)
{
    pth.parse("subdict.subkey1");
    m.exec_match(json, pth, jsn);
    ASSERT_EQ(JSONSL_MATCH_COMPLETE, m.matchres);
    ASSERT_EQ(JSONSL_ERROR_SUCCESS, m.status);
    ASSERT_EQ("\"subval1\"", t_subdoc::getMatchString(m));
    ASSERT_EQ("\"subkey1\"", t_subdoc::getMatchKey(m));
}

TEST_F(MatchTests, testArrayIndex)
{
    pth.parse("sublist[1]");
    m.exec_match(json, pth, jsn);
    ASSERT_EQ(JSONSL_MATCH_COMPLETE, m.matchres);
    ASSERT_EQ(JSONSL_ERROR_SUCCESS, m.status);
    ASSERT_EQ("\"elem2\"", t_subdoc::getMatchString(m));
    ASSERT_EQ(0, m.has_key);
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
    ASSERT_EQ("[1,2,3,4,5,6,7,8,9,0]", t_subdoc::getMatchString(m));
}

TEST_F(MatchTests, testFinalComponentNotFound)
{
    pth.parse("empty.field");
    m.exec_match(json, pth, jsn);
    ASSERT_NE(0, m.immediate_parent_found);
    ASSERT_EQ(2, m.match_level);
    ASSERT_EQ("{}", t_subdoc::getParentString(m));
}

TEST_F(MatchTests, testOOBArrayIndex)
{
    pth.parse("sublist[4]");
    m.exec_match(json, pth, jsn);
    ASSERT_NE(0, m.immediate_parent_found);
    ASSERT_EQ(2, m.match_level);
    ASSERT_EQ("[\"elem1\",\"elem2\",\"elem3\"]", t_subdoc::getParentString(m));
}

TEST_F(MatchTests, testAllComponentsNotFound)
{
    pth.parse("non.existent.path");
    m.exec_match(json, pth, jsn);
    ASSERT_EQ(0, m.immediate_parent_found);
    ASSERT_EQ(1, m.match_level);
    ASSERT_EQ(json, t_subdoc::getParentString(m));
}

TEST_F(MatchTests, testSingletonComponentNotFound)
{
    pth.parse("toplevel");
    m.exec_match(json, pth, jsn);
    ASSERT_EQ(1, m.match_level);
    ASSERT_NE(0, m.immediate_parent_found);
    ASSERT_EQ(json, t_subdoc::getParentString(m));
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
