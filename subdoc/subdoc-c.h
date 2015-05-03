#ifndef SUBDOC_C_H
#define SUBDOC_C_H

#ifdef __cplusplus
namespace Subdoc { class Operation; }
typedef Subdoc::Operation subdoc_OPERATION;

extern "C" {
#else
typedef struct subdoc_OPERATION_st subdoc_OPERATION;
#endif

subdoc_OPERATION * subdoc_op_alloc(void);
void subdoc_op_free(subdoc_OPERATION *op);

#ifdef __cplusplus
}
#endif
#endif
