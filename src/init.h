/* ------------------------------------------------------------------------
 *
 * init.h
 *		Initialization functions
 *
 * Copyright (c) 2015-2016, Postgres Professional
 *
 * ------------------------------------------------------------------------
 */

#ifndef PATHMAN_INIT_H
#define PATHMAN_INIT_H

#include "relation_info.h"

#include "postgres.h"
#include "storage/lmgr.h"
#include "utils/guc.h"
#include "utils/hsearch.h"
#include "utils/snapshot.h"


/*
 * pg_pathman's initialization state structure.
 */
typedef struct
{
	bool 	pg_pathman_enable;		/* GUC variable implementation */
	bool	initialization_needed;	/* do we need to perform init? */
} PathmanInitState;


extern HTAB				   *partitioned_rels;
extern HTAB				   *parent_cache;

/* pg_pathman's initialization state */
extern PathmanInitState 	pg_pathman_init_state;


/*
 * Check if pg_pathman is initialized.
 */
#define IsPathmanInitialized()	( !pg_pathman_init_state.initialization_needed )

/*
 * Check if pg_pathman is enabled.
 */
#define IsPathmanEnabled()		( pg_pathman_init_state.pg_pathman_enable )

/*
 * Check if pg_pathman is initialized & enabled.
 */
#define IsPathmanReady()		( IsPathmanInitialized() && IsPathmanEnabled() )

/*
 * Emergency disable mechanism.
 */
#define DisablePathman() \
	do { \
		pg_pathman_init_state.pg_pathman_enable = false; \
		pg_pathman_init_state.initialization_needed = true; \
	} while (0)


/*
 * Save and restore PathmanInitState.
 */
void save_pathman_init_state(PathmanInitState *temp_init_state);
void restore_pathman_init_state(const PathmanInitState *temp_init_state);

/*
 * Create main GUC variable.
 */
void init_main_pathman_toggle(void);

Size estimate_pathman_shmem_size(void);
void init_shmem_config(void);

bool load_config(void);
void unload_config(void);


void fill_prel_with_partitions(const Oid *partitions,
							   const uint32 parts_count,
							   PartRelationInfo *prel);

Oid *find_inheritance_children_array(Oid parentrelId,
									 LOCKMODE lockmode,
									 uint32 *size);

char *build_check_constraint_name_internal(Oid relid,
										   AttrNumber attno);

bool pathman_config_contains_relation(Oid relid,
									  Datum *values,
									  bool *isnull,
									  TransactionId *xmin);

bool read_pathman_params(Oid relid,
						 Datum *values,
						 bool *isnull);

#endif
