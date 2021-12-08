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
