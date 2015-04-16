#include "subdoc-tests-common.h"

using std::string;
using std::cerr;
using std::endl;

class PathTests : public ::testing::Test {};

static void
getComponentString(const subdoc_PATH_st* nj, int ix, string& out) {
    const jsonsl_jpr_component_st *component = &nj->jpr_base.components[ix];
    out.assign(component->pstr, component->len);
}
static string
getComponentString(const subdoc_PATH_st *nj, int ix) {
    string tmp;
    getComponentString(nj, ix, tmp);
    return tmp;
}
static unsigned long
getComponentNumber(const subdoc_PATH_st *nj, int ix) {
    return nj->jpr_base.components[ix].idx;
}

TEST_F(PathTests, testBasic)
{
    subdoc_PATH_st *ss = subdoc_path_alloc();
    jsonsl_jpr_t jpr = &ss->jpr_base;

    const char *pth1 = "foo.bar.baz";
    ASSERT_EQ(0, subdoc_path_parse(ss, pth1, strlen(pth1)));
    ASSERT_EQ(4, ss->jpr_base.ncomponents);

    ASSERT_EQ(JSONSL_PATH_ROOT, jpr->components[0].ptype);
    ASSERT_EQ(JSONSL_PATH_STRING, jpr->components[1].ptype);
    ASSERT_EQ(JSONSL_PATH_STRING, jpr->components[2].ptype);
    ASSERT_EQ(JSONSL_PATH_STRING, jpr->components[3].ptype);


    ASSERT_EQ("foo", getComponentString(ss, 1));
    ASSERT_EQ("bar", getComponentString(ss, 2));
    ASSERT_EQ("baz", getComponentString(ss, 3));
    subdoc_path_free(ss);

    ss = subdoc_path_alloc();
    pth1 = "....";
    ASSERT_NE(0, subdoc_path_parse(ss, pth1, strlen(pth1)));
    subdoc_path_free(ss);
}

TEST_F(PathTests, testNumericIndices) {
    subdoc_PATH_st *ss = subdoc_path_alloc();
    const char *pth = "array[1].item[9]";
    jsonsl_jpr_t jpr = &ss->jpr_base;

    ASSERT_EQ(0, subdoc_path_parse(ss, pth, strlen(pth)));
    ASSERT_EQ(5, jpr->ncomponents);

    ASSERT_EQ(JSONSL_PATH_STRING, jpr->components[1].ptype);
    ASSERT_EQ("array", getComponentString(ss, 1));

    ASSERT_EQ(JSONSL_PATH_NUMERIC, jpr->components[2].ptype);
    ASSERT_EQ(1, jpr->components[2].is_arridx);
    ASSERT_EQ(1, getComponentNumber(ss, 2));

    ASSERT_EQ(JSONSL_PATH_STRING, jpr->components[3].ptype);
    ASSERT_EQ("item", getComponentString(ss, 3));

    ASSERT_EQ(JSONSL_PATH_NUMERIC, jpr->components[4].ptype);
    ASSERT_EQ(1, jpr->components[4].is_arridx);
    ASSERT_EQ(9, getComponentNumber(ss, 4));

    // Try again, using [] syntax
    pth = "foo[0][0][0]";
    ASSERT_EQ(0, subdoc_path_parse(ss, pth, strlen(pth)));
    ASSERT_EQ(5, ss->jpr_base.ncomponents);

    pth = "[1][2][3]";
    ASSERT_EQ(0, subdoc_path_parse(ss, pth, strlen(pth)));
    ASSERT_EQ(4, ss->jpr_base.ncomponents);

    subdoc_path_free(ss);
}

TEST_F(PathTests, testEscapes)
{
    subdoc_PATH_st *ss = subdoc_path_alloc();
    const char *pth;

    pth = "`simple`.`escaped`.`path`";
    ASSERT_EQ(0, subdoc_path_parse(ss, pth, strlen(pth)));
    ASSERT_EQ("simple", getComponentString(ss, 1));
    ASSERT_EQ("escaped", getComponentString(ss, 2));
    ASSERT_EQ("path", getComponentString(ss, 3));
    subdoc_path_free(ss);

    // Try something more complex
    ss = subdoc_path_alloc();
    pth = "escaped.`arr.idx`[9]";
    ASSERT_EQ(0, subdoc_path_parse(ss, pth, strlen(pth)));
    ASSERT_EQ("escaped", getComponentString(ss, 1));
    ASSERT_EQ("arr.idx", getComponentString(ss, 2));
    ASSERT_EQ(9, getComponentNumber(ss, 3));
    subdoc_path_free(ss);

    ss = subdoc_path_alloc();
    pth = "`BACKTICK``HAPPY`.`CAMPER`";
    ASSERT_EQ(0, subdoc_path_parse(ss, pth, strlen(pth)));
    ASSERT_EQ("BACKTICK`HAPPY", getComponentString(ss, 1));
    ASSERT_EQ("CAMPER", getComponentString(ss, 2));
    subdoc_path_free(ss);
}

TEST_F(PathTests, testNegativePath) {
    subdoc_PATH *ss = subdoc_path_alloc();
    const char *pth;

    pth = "foo[-1].[-1].[-1]";
    ASSERT_EQ(0, subdoc_path_parse(ss, pth, strlen(pth)));
    ASSERT_EQ(5, ss->jpr_base.ncomponents);
    ASSERT_TRUE(!!ss->components_s[2].is_neg);
    ASSERT_TRUE(!!ss->components_s[3].is_neg);
    ASSERT_TRUE(!!ss->components_s[4].is_neg);
    ASSERT_TRUE(!!ss->has_negix);

    pth = "foo[-2]";
    ASSERT_NE(0, subdoc_path_parse(ss, pth, strlen(pth)));

    subdoc_path_free(ss);
}
