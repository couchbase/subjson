#ifndef SUBDOC_OPERATIONS_H
#define SUBDOC_OPERATIONS_H
#include "subdoc-api.h"
#include "loc.h"
#include "path.h"
#include "match.h"
#include "subdoc-util.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Private; malloc'd because this block is pretty big (several k) */
    subdoc_PATH *path;
    /* cached JSON parser */
    jsonsl_t jsn;

    subdoc_MATCH match;
    /* opcode */
    subdoc_OPTYPE optype;

    /* Location of original document */
    subdoc_LOC doc_cur;
    /* Location of the user's "Value" (if applicable) */
    subdoc_LOC user_in;
    /* Location of the fragments consisting of the _new_ value */
    subdoc_LOC doc_new[8];
    /* Number of fragments active */
    size_t doc_new_len;
} subdoc_OPERATION;

#ifdef __cplusplus
namespace Subdoc { typedef subdoc_OPERATION Operation; }
#endif

subdoc_OPERATION *
subdoc_op_alloc(void);

void
subdoc_op_clear(subdoc_OPERATION *);

void
subdoc_op_free(subdoc_OPERATION*);

static inline void
SUBDOC_OP_SETVALUE(subdoc_OPERATION *op, const char *val, size_t nval)
{
    op->user_in.at = val;
    op->user_in.length = nval;
}

static inline void
SUBDOC_OP_SETDOC(subdoc_OPERATION *op, const char *doc, size_t ndoc)
{
    op->doc_cur.at = doc;
    op->doc_cur.length = ndoc;
}

static inline void
SUBDOC_OP_SETCODE(subdoc_OPERATION *op, subdoc_OPTYPE code)
{
    op->optype = code;
}

subdoc_ERRORS
subdoc_op_exec(subdoc_OPERATION *op, const char *pth, size_t npth);

const char *
subdoc_strerror(subdoc_ERRORS rc);

#ifdef __cplusplus
}
#endif
#endif
