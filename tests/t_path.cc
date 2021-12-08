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
    ASSERT_EQ(4UL, ss.size());

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
    ASSERT_EQ(5UL, ss.size());

    ASSERT_EQ(JSONSL_PATH_STRING, ss[1].ptype);
    ASSERT_EQ("array", getComponentString(ss, 1));

    ASSERT_EQ(JSONSL_PATH_NUMERIC, ss[2].ptype);
    ASSERT_EQ(1UL, getComponentNumber(ss, 2));

    ASSERT_EQ(JSONSL_PATH_STRING, ss[3].ptype);
    ASSERT_EQ("item", getComponentString(ss, 3));

    ASSERT_EQ(JSONSL_PATH_NUMERIC, ss[4].ptype);
    ASSERT_EQ(9UL, getComponentNumber(ss, 4));

    // Try again, using [] syntax
    pth = "foo[0][0][0]";
    ASSERT_EQ(0, ss.parse(pth));
    ASSERT_EQ(5UL, ss.size());

    pth = "[1][2][3]";
    ASSERT_EQ(0, ss.parse(pth));
    ASSERT_EQ(4UL, ss.size());
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
    ASSERT_EQ(9UL, getComponentNumber(ss, 3));

    ss.clear();
    pth = "`BACKTICK``HAPPY`.`CAMPER`";
    ASSERT_EQ(0, ss.parse(pth));
    ASSERT_EQ("BACKTICK`HAPPY", getComponentString(ss, 1));
    ASSERT_EQ("CAMPER", getComponentString(ss, 2));

    // MB-30278: A subsequent parse of a path containing an escaped symbol
    // fails due to incorrect caching.
    ss.clear();
    pth = "trailing``";
    ASSERT_EQ(0, ss.parse(pth));
    ASSERT_EQ("trailing`", getComponentString(ss, 1));
}

TEST_F(PathTests, testNegativePath) {
    Path ss;
    string pth;

    pth = "foo[-1][-1][-1]";
    ASSERT_EQ(0, ss.parse(pth));
    ASSERT_EQ(5UL, ss.size());
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

TEST_F(PathTests, testUtf8Path) {
    Path ss;
    // \xc3\xba = ú
    string pth("F\xC3\xBAtbol");
    ASSERT_EQ(0, ss.parse(pth));
    ASSERT_EQ("F\xC3\xBAtbol", getComponentString(ss, 1));
}

TEST_F(PathTests, testGetRootType) {
    ASSERT_EQ(JSONSL_T_LIST, Util::get_root_type(Command::ARRAY_PREPEND, ""));
    ASSERT_EQ(JSONSL_T_OBJECT, Util::get_root_type(Command::ARRAY_PREPEND, "a"));
    ASSERT_EQ(JSONSL_T_UNKNOWN, Util::get_root_type(Command::DICT_UPSERT, ""));
    ASSERT_EQ(JSONSL_T_LIST, Util::get_root_type(Command::ARRAY_APPEND_P, ""));
    ASSERT_EQ(JSONSL_T_LIST, Util::get_root_type(Command::ARRAY_APPEND, "[1]"));
    ASSERT_EQ(JSONSL_T_LIST, Util::get_root_type(Command::ARRAY_PREPEND, "[-1]"));
}
