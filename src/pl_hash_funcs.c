/* ------------------------------------------------------------------------
 *
 * pl_hash_funcs.c
 *		Utility C functions for stored HASH procedures
 *
 * Copyright (c) 2016, Postgres Professional
 *
 * ------------------------------------------------------------------------
 */

#include "pathman.h"
#include "partition_creation.h"
#include "relation_info.h"
#include "utils.h"

#include "catalog/namespace.h"
#include "catalog/pg_type.h"
#include "utils/builtins.h"
#include "utils/typcache.h"
#include "utils/lsyscache.h"
#include "utils/builtins.h"
#include "utils/array.h"


static char **deconstruct_text_array(Datum array, int *array_size);


/* Function declarations */

PG_FUNCTION_INFO_V1( create_hash_partitions_internal );

PG_FUNCTION_INFO_V1( get_type_hash_func );
PG_FUNCTION_INFO_V1( get_hash_part_idx );

PG_FUNCTION_INFO_V1( build_hash_condition );


/*
 * Create HASH partitions implementation (written in C).
 */
Datum
create_hash_partitions_internal(PG_FUNCTION_ARGS)
{
/* Free allocated arrays */
#define DeepFreeArray(arr, arr_len) \
	do { \
		int arr_elem; \
		if (!arr) break; \
		for (arr_elem = 0; arr_elem < arr_len; arr_elem++) \
			pfree(arr[arr_elem]); \
		pfree(arr); \
	} while (0)

	Oid			parent_relid = PG_GETARG_OID(0);
	const char *partitioned_col_name = TextDatumGetCString(PG_GETARG_DATUM(1));
	Oid			partitioned_col_type;
	uint32		partitions_count = PG_GETARG_INT32(2),
				i;

	/* Partition names and tablespaces */
	char	  **partition_names			= NULL,
			  **tablespaces				= NULL;
	int			partition_names_size	= 0,
				tablespaces_size		= 0;
	RangeVar  **rangevars				= NULL;

	/* Check that there's no partitions yet */
	if (get_pathman_relation_info(parent_relid))
		elog(ERROR, "cannot add new HASH partitions");

	partitioned_col_type = get_attribute_type(parent_relid,
											  partitioned_col_name,
											  false);

	/* Extract partition names */
	if (!PG_ARGISNULL(3))
		partition_names = deconstruct_text_array(PG_GETARG_DATUM(3), &partition_names_size);

	/* Extract partition tablespaces */
	if (!PG_ARGISNULL(4))
		tablespaces = deconstruct_text_array(PG_GETARG_DATUM(4), &tablespaces_size);

	/* Validate size of 'partition_names' */
	if (partition_names && partition_names_size != partitions_count)
		elog(ERROR, "size of 'partition_names' must be equal to 'partitions_count'");

	/* Validate size of 'tablespaces' */
	if (tablespaces && tablespaces_size != partitions_count)
		elog(ERROR, "size of 'tablespaces' must be equal to 'partitions_count'");

	/* Convert partition names into RangeVars */
	if (partition_names)
	{
		rangevars = palloc(sizeof(RangeVar) * partition_names_size);
		for (i = 0; i < partition_names_size; i++)
		{
			List *nl = stringToQualifiedNameList(partition_names[i]);

			rangevars[i] = makeRangeVarFromNameList(nl);
		}
	}

	/* Finally create HASH partitions */
	for (i = 0; i < partitions_count; i++)
	{
		RangeVar   *partition_rv	= rangevars ? rangevars[i] : NULL;
		char 	   *tablespace		= tablespaces ? tablespaces[i] : NULL;

		/* Create a partition (copy FKs, invoke callbacks etc) */
		create_single_hash_partition_internal(parent_relid, i, partitions_count,
											  partitioned_col_type,
											  partition_rv, tablespace);
	}

	/* Free arrays */
	DeepFreeArray(partition_names, partition_names_size);
	DeepFreeArray(tablespaces, tablespaces_size);
	DeepFreeArray(rangevars, partition_names_size);

	PG_RETURN_VOID();
}

/*
 * Returns hash function's OID for a specified type.
 */
Datum
get_type_hash_func(PG_FUNCTION_ARGS)
{
	TypeCacheEntry *tce;
	Oid 			type_oid = PG_GETARG_OID(0);

	tce = lookup_type_cache(type_oid, TYPECACHE_HASH_PROC);

	PG_RETURN_OID(tce->hash_proc);
}

/*
 * Wrapper for hash_to_part_index().
 */
Datum
get_hash_part_idx(PG_FUNCTION_ARGS)
{
	uint32	value = PG_GETARG_UINT32(0),
			part_count = PG_GETARG_UINT32(1);

	PG_RETURN_UINT32(hash_to_part_index(value, part_count));
}

/*
 * Build hash condition for a CHECK CONSTRAINT
 */
Datum
build_hash_condition(PG_FUNCTION_ARGS)
{
	Oid				atttype = PG_GETARG_OID(0);
	text		   *attname = PG_GETARG_TEXT_P(1);
	uint32			part_count = PG_GETARG_UINT32(2),
					part_idx = PG_GETARG_UINT32(3);

	TypeCacheEntry *tce;
	char		   *attname_cstring = text_to_cstring(attname);

	char		   *result;

	if (part_idx >= part_count)
		elog(ERROR, "'partition_index' must be lower than 'partitions_count'");

	tce = lookup_type_cache(atttype, TYPECACHE_HASH_PROC);

	/* Check that HASH function exists */
	if (!OidIsValid(tce->hash_proc))
		elog(ERROR, "no hash function for type %s", format_type_be(atttype));

	/* Create hash condition CSTRING */
	result = psprintf("%s.get_hash_part_idx(%s(%s), %u) = %u",
					  get_namespace_name(get_pathman_schema()),
					  get_func_name(tce->hash_proc),
					  attname_cstring,
					  part_count,
					  part_idx);

	PG_RETURN_TEXT_P(cstring_to_text(result));
}


/*
 * ------------------
 *  Helper functions
 * ------------------
 */

/* Convert Datum into CSTRING array */
static char **
deconstruct_text_array(Datum array, int *array_size)
{
	ArrayType  *array_ptr = DatumGetArrayTypeP(array);
	int16		elemlen;
	bool		elembyval;
	char		elemalign;

	Datum	   *elem_values;
	bool	   *elem_nulls;

	int			arr_size = 0;

	/* Check type invariant */
	Assert(ARR_ELEMTYPE(array_ptr) == TEXTOID);

	/* Check number of dimensions */
	if (ARR_NDIM(array_ptr) > 1)
		elog(ERROR, "'partition_names' and 'tablespaces' may contain only 1 dimension");

	get_typlenbyvalalign(ARR_ELEMTYPE(array_ptr),
						 &elemlen, &elembyval, &elemalign);

	deconstruct_array(array_ptr,
					  ARR_ELEMTYPE(array_ptr),
					  elemlen, elembyval, elemalign,
					  &elem_values, &elem_nulls, &arr_size);

	/* If there are actual values, convert them into CSTRINGs */
	if (arr_size > 0)
	{
		char  **strings = palloc(arr_size * sizeof(char *));
		int		i;

		for (i = 0; i < arr_size; i++)
		{
			if (elem_nulls[i])
				elog(ERROR, "'partition_names' and 'tablespaces' may not contain NULLs");

			strings[i] = TextDatumGetCString(elem_values[i]);
		}

		/* Return an array and it's size */
		*array_size = arr_size;
		return strings;
	}
	/* Else emit ERROR */
	else elog(ERROR, "'partition_names' and 'tablespaces' may not be empty");

	/* Keep compiler happy */
	return NULL;
}
