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
using Subdoc::Util;
using Subdoc::Command;

class PathTests : public ::testing::Test {};

static void
getComponentString(const Path& nj, int ix, string& out) {
    const auto& comp = nj.get_component(ix);
    out.assign(comp.pstr, comp.len);
}
static string
getComponentString(const Path& nj, int ix) {
    string tmp;
    getComponentString(nj, ix, tmp);
    return tmp;
}
static unsigned long
getComponentNumber(const Path& nj, int ix) {
    return nj.get_component(ix).idx;
}

TEST_F(PathTests, testBasic)
{
    Path ss;

    string pth1("foo.bar.baz");
    ASSERT_EQ(0, ss.parse(pth1));
    ASSERT_EQ(4, ss.size());

    ASSERT_EQ(JSONSL_PATH_ROOT, ss[0].ptype);
    ASSERT_EQ(JSONSL_PATH_STRING, ss[1].ptype);
    ASSERT_EQ(JSONSL_PATH_STRING, ss[2].ptype);
    ASSERT_EQ(JSONSL_PATH_STRING, ss[3].ptype);


    ASSERT_EQ("foo", getComponentString(ss, 1));
    ASSERT_EQ("bar", getComponentString(ss, 2));
    ASSERT_EQ("baz", getComponentString(ss, 3));

    ss.clear();
    pth1 = "....";
    ASSERT_NE(0, ss.parse(pth1));
}

TEST_F(PathTests, testNumericIndices) {
    Path ss;
    string pth = "array[1].item[9]";

    ASSERT_EQ(0, ss.parse(pth));
    ASSERT_EQ(5, ss.size());

    ASSERT_EQ(JSONSL_PATH_STRING, ss[1].ptype);
    ASSERT_EQ("array", getComponentString(ss, 1));

    ASSERT_EQ(JSONSL_PATH_NUMERIC, ss[2].ptype);
    ASSERT_EQ(1, getComponentNumber(ss, 2));

    ASSERT_EQ(JSONSL_PATH_STRING, ss[3].ptype);
    ASSERT_EQ("item", getComponentString(ss, 3));

    ASSERT_EQ(JSONSL_PATH_NUMERIC, ss[4].ptype);
    ASSERT_EQ(9, getComponentNumber(ss, 4));

    // Try again, using [] syntax
    pth = "foo[0][0][0]";
    ASSERT_EQ(0, ss.parse(pth));
    ASSERT_EQ(5, ss.size());

    pth = "[1][2][3]";
    ASSERT_EQ(0, ss.parse(pth));
    ASSERT_EQ(4, ss.size());
}

TEST_F(PathTests, testEscapes)
{
    Path ss;
    string pth;

    pth = "`simple`.`escaped`.`path`";
    ASSERT_EQ(0, ss.parse(pth));
    ASSERT_EQ("simple", getComponentString(ss, 1));
    ASSERT_EQ("escaped", getComponentString(ss, 2));
    ASSERT_EQ("path", getComponentString(ss, 3));

    // Try something more complex
    ss.clear();
    pth = "escaped.`arr.idx`[9]";
    ASSERT_EQ(0, ss.parse(pth));
    ASSERT_EQ("escaped", getComponentString(ss, 1));
    ASSERT_EQ("arr.idx", getComponentString(ss, 2));
    ASSERT_EQ(9, getComponentNumber(ss, 3));

    ss.clear();
    pth = "`BACKTICK``HAPPY`.`CAMPER`";
    ASSERT_EQ(0, ss.parse(pth));
    ASSERT_EQ("BACKTICK`HAPPY", getComponentString(ss, 1));
    ASSERT_EQ("CAMPER", getComponentString(ss, 2));
}

TEST_F(PathTests, testNegativePath) {
    Path ss;
    string pth;

    pth = "foo[-1][-1][-1]";
    ASSERT_EQ(0, ss.parse(pth));
    ASSERT_EQ(5, ss.size());
    ASSERT_TRUE(!!ss.components_s[2].is_neg);
    ASSERT_TRUE(!!ss.components_s[3].is_neg);
    ASSERT_TRUE(!!ss.components_s[4].is_neg);
    ASSERT_TRUE(!!ss.has_negix);

    ss.clear();
    pth = "foo[-2]";
    ASSERT_NE(0, ss.parse(pth));
}

TEST_F(PathTests, testInvalidSequence) {
    Path ss;
    string pth;

    #if 0 /* TODO */
    pth = "[1].[2].[3]";
    ASSERT_NE(0, ss.parse(pth));
    #endif

    pth = "hello[0]world";
    ASSERT_NE(0, ss.parse(pth));

    pth = "[not][a][number]";
    ASSERT_NE(0, ss.parse(pth));
}

TEST_F(PathTests, testJsonEscapes) {
    Path ss;
    string pth;

    // Reject any path not considered valid as a JSON string. N1QL escapes
    // are *only* for n1ql syntax tokens (like ., ], [, `)
    pth = "\"invalid.path";
    ASSERT_NE(0, ss.parse(pth));

    pth = "\\" "\"quoted.path";
    ASSERT_EQ(0, ss.parse(pth));
}

TEST_F(PathTests, testGetRootType) {
    ASSERT_EQ(JSONSL_T_LIST, Util::get_root_type(Command::ARRAY_PREPEND, ""));
    ASSERT_EQ(JSONSL_T_OBJECT, Util::get_root_type(Command::ARRAY_PREPEND, "a"));
    ASSERT_EQ(JSONSL_T_UNKNOWN, Util::get_root_type(Command::DICT_UPSERT, ""));
    ASSERT_EQ(JSONSL_T_LIST, Util::get_root_type(Command::ARRAY_APPEND_P, ""));
    ASSERT_EQ(JSONSL_T_LIST, Util::get_root_type(Command::ARRAY_APPEND, "[1]"));
    ASSERT_EQ(JSONSL_T_LIST, Util::get_root_type(Command::ARRAY_PREPEND, "[-1]"));
}
