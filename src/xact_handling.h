/* ------------------------------------------------------------------------
 *
 * xact_handling.h
 *		Transaction-specific locks and other functions
 *
 * Copyright (c) 2016, Postgres Professional
 *
 * ------------------------------------------------------------------------
 */

#ifndef XACT_HANDLING_H
#define XACT_HANDLING_H


#include "pathman.h"

#include "postgres.h"


/*
 * Transaction locks.
 */
LockAcquireResult xact_lock_partitioned_rel(Oid relid, bool nowait);
void xact_unlock_partitioned_rel(Oid relid);

LockAcquireResult xact_lock_rel_exclusive(Oid relid, bool nowait);
void xact_unlock_rel_exclusive(Oid relid);

/*
 * Utility checks.
 */
bool xact_bgw_conflicting_lock_exists(Oid relid);
bool xact_is_level_read_committed(void);
bool xact_is_transaction_stmt(Node *stmt);
bool xact_is_set_transaction_stmt(Node *stmt);
bool xact_object_is_visible(TransactionId obj_xmin);

void prevent_relation_modification_internal(Oid relid);


#endif /* XACT_HANDLING_H */
