#include "subdoc-tests-common.h"

using std::string;
using std::cerr;
using std::endl;

#define JQ(s) "\"" s "\""
class SubdocPath {
public:
    SubdocPath() { pth = subdoc_path_alloc(); }
    ~SubdocPath() { subdoc_path_free(pth); }
    void reset() { subdoc_path_clear(pth); }
    subdoc_PATH* getPath() { return pth; }

    void parse(const char *s) {
        reset();
        int rv = subdoc_path_parse(pth, s, strlen(s));
        if (rv != 0) { throw string("Bad path!"); }
    }
private:
    subdoc_PATH *pth;
    SubdocPath(SubdocPath&);
};


class MatchTests : public ::testing::Test {
protected:
    static const char *json;
    static jsonsl_t jsn;
    static SubdocPath pth;
    static subdoc_MATCH m;

    static void SetUpTestCase() { jsn = subdoc_jsn_alloc(); }
    static void TearDownTestCase() { subdoc_jsn_free(jsn); }
    virtual void SetUp() { memset(&m, 0, sizeof m); }
};

SubdocPath MatchTests::pth;
subdoc_MATCH MatchTests::m;
jsonsl_t MatchTests::jsn = NULL;
const char * MatchTests::json = "{"
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
    subdoc_match_exec(json, strlen(json), pth.getPath(), jsn, &m);
    ASSERT_EQ(JSONSL_MATCH_COMPLETE, m.matchres);
    ASSERT_EQ(JSONSL_ERROR_SUCCESS, m.status);
    ASSERT_EQ("\"val1\"", t_subdoc::getMatchString(m));
    ASSERT_EQ("\"key1\"", t_subdoc::getMatchKey(m));
}

TEST_F(MatchTests, testNestedDict)
{
    pth.parse("subdict.subkey1");
    subdoc_match_exec(json, strlen(json), pth.getPath(), jsn, &m);
    ASSERT_EQ(JSONSL_MATCH_COMPLETE, m.matchres);
    ASSERT_EQ(JSONSL_ERROR_SUCCESS, m.status);
    ASSERT_EQ("\"subval1\"", t_subdoc::getMatchString(m));
    ASSERT_EQ("\"subkey1\"", t_subdoc::getMatchKey(m));
}

TEST_F(MatchTests, testArrayIndex)
{
    pth.parse("sublist[1]");
    subdoc_match_exec(json, strlen(json), pth.getPath(), jsn, &m);
    ASSERT_EQ(JSONSL_MATCH_COMPLETE, m.matchres);
    ASSERT_EQ(JSONSL_ERROR_SUCCESS, m.status);
    ASSERT_EQ("\"elem2\"", t_subdoc::getMatchString(m));
    ASSERT_EQ(0, m.has_key);
}

TEST_F(MatchTests, testMismatchArrayAsDict)
{
    pth.parse("key1[9]");
    subdoc_match_exec(json, strlen(json), pth.getPath(), jsn, &m);
    ASSERT_EQ(JSONSL_MATCH_TYPE_MISMATCH, m.matchres);
}

TEST_F(MatchTests, testMismatchDictAsArray)
{
    pth.parse("subdict[0]");
    subdoc_match_exec(json, strlen(json), pth.getPath(), jsn, &m);
    ASSERT_EQ(JSONSL_MATCH_TYPE_MISMATCH, m.matchres);
}

TEST_F(MatchTests, testMatchContainerValue)
{
    pth.parse("numbers");
    subdoc_match_exec(json, strlen(json), pth.getPath(), jsn, &m);
    ASSERT_EQ(JSONSL_MATCH_COMPLETE, m.matchres);
    ASSERT_EQ("[1,2,3,4,5,6,7,8,9,0]", t_subdoc::getMatchString(m));
}

TEST_F(MatchTests, testFinalComponentNotFound)
{
    pth.parse("empty.field");
    subdoc_match_exec(json, strlen(json), pth.getPath(), jsn, &m);
    ASSERT_NE(0, m.immediate_parent_found);
    ASSERT_EQ(2, m.match_level);
    ASSERT_EQ("{}", t_subdoc::getParentString(m));
}

TEST_F(MatchTests, testOOBArrayIndex)
{
    pth.parse("sublist[4]");
    subdoc_match_exec(json, strlen(json), pth.getPath(), jsn, &m);
    ASSERT_NE(0, m.immediate_parent_found);
    ASSERT_EQ(2, m.match_level);
    ASSERT_EQ("[\"elem1\",\"elem2\",\"elem3\"]", t_subdoc::getParentString(m));
}

TEST_F(MatchTests, testAllComponentsNotFound)
{
    pth.parse("non.existent.path");
    subdoc_match_exec(json, strlen(json), pth.getPath(), jsn, &m);
    ASSERT_EQ(0, m.immediate_parent_found);
    ASSERT_EQ(1, m.match_level);
    ASSERT_EQ(json, t_subdoc::getParentString(m));
}

TEST_F(MatchTests, testSingletonComponentNotFound)
{
    pth.parse("toplevel");
    memset(&m, 0, sizeof m);
    subdoc_match_exec(json, strlen(json), pth.getPath(), jsn, &m);
    ASSERT_EQ(1, m.match_level);
    ASSERT_NE(0, m.immediate_parent_found);
    ASSERT_EQ(json, t_subdoc::getParentString(m));
}

TEST_F(MatchTests, testUescape)
{
    // See if we can find the 'u-escape' here.
    m.clear();
    pth.parse("U-Escape");
    m.exec_match(json, strlen(json), pth.getPath(), jsn);
    ASSERT_EQ(JSONSL_MATCH_COMPLETE, m.matchres);
    ASSERT_STREQ("\"U\\u002DEscape\"", m.loc_key.to_string().c_str());
}
