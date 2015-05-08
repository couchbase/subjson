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
    Error op_exec(const std::string& s) { return op_exec(s.c_str(), s.size()); }

    void set_value(const char *s, size_t n) { user_in.assign(s, n); }
    void set_value(const std::string& s) { set_value(s.c_str(), s.size()); }

    void set_doc(const char *s, size_t n) { doc_cur.assign(s, n); }
    void set_doc(const std::string& s) { set_doc(s.c_str(), s.size()); }

    void set_code(uint8_t code) { optype = subdoc_OPTYPE(code); }

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

const char *
subdoc_strerror(subdoc_ERRORS rc);


#endif
