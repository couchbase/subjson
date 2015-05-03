#include "subdoc-c.h"

#if !defined(SUBDOC_OPERATIONS_H) && defined(__cplusplus)
#define SUBDOC_OPERATIONS_H
#include "subdoc-api.h"
#include "loc.h"
#include "path.h"
#include "match.h"
#include "subdoc-util.h"

namespace Subdoc {
class Operation {
public:
    /* malloc'd because this block is pretty big (several k) */
    Path *path;
    /* cached JSON parser */
    jsonsl_t jsn;

    Match match;

    /* opcode */
    subdoc_OPTYPE optype;

    /* Location of original document */
    Loc doc_cur;
    /* Location of the user's "Value" (if applicable) */
    Loc user_in;
    /* Location of the fragments consisting of the _new_ value */
    Loc doc_new[8];
    /* Number of fragments active */
    size_t doc_new_len;

    Operation();
    void clear();
    ~Operation();
    Error op_exec(const char *pth, size_t npth);

private:
    std::string bkbuf;
    std::string numbuf;

    Error do_match_common();
    Error do_get();
    Error do_store_dict();
    Error do_mkdir_p(int mode);
    Error find_first_element();
    Error find_last_element();
    Error insert_singleton_element();
    Error do_list_op();
    Error do_arith_op();
};
}

typedef Subdoc::Operation subdoc_OPERATION;
typedef Subdoc::Loc subdoc_LOC;
typedef Subdoc::Path subdoc_PATH, subdoc_PATH_st;
typedef Subdoc::Operation subdoc_OPERATION;
typedef Subdoc::Match subdoc_MATCH;

#define subdoc_path_alloc() new Subdoc::Path()
#define subdoc_path_clear(pth) (pth)->clear()
#define subdoc_path_parse(pth, s, n) (pth)->parse(s, n)
#define subdoc_path_free(pth) delete (pth)

#define subdoc_jsn_alloc Subdoc::Match::jsn_alloc
#define subdoc_jsn_free Subdoc::Match::jsn_free

#define subdoc_match_exec(s, n, pth, jsn, m) (m)->exec_match(s, n, pth, jsn)
#define subdoc_validate Subdoc::Match::validate

#define subdoc_op_clear(op) (op)->clear()
#define subdoc_op_exec(op, pth, npth) (op)->op_exec(pth, npth)

static JSONSL_INLINE void
SUBDOC_OP_SETVALUE(subdoc_OPERATION *op, const char *val, size_t nval)
{
    op->user_in.at = val;
    op->user_in.length = nval;
}

static JSONSL_INLINE void
SUBDOC_OP_SETDOC(subdoc_OPERATION *op, const char *doc, size_t ndoc)
{
    op->doc_cur.at = doc;
    op->doc_cur.length = ndoc;
}

static JSONSL_INLINE void
SUBDOC_OP_SETCODE(subdoc_OPERATION *op, subdoc_OPTYPE code)
{
    op->optype = code;
}

const char *
subdoc_strerror(subdoc_ERRORS rc);


#endif
