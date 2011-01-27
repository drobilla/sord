/* Sord, a lightweight RDF model library.
 * Copyright 2010-2011 David Robillard <d@drobilla.net>
 *
 * Sord is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Sord is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
 * License for details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file
 * Sord Implementation.
 *
 * Tuples are represented as simple arrays of SordID, of length 4,
 * which represent statements (RDF triples) with an optional
 * context.  When contexts are not used, only the first 3 elements
 * are considered.
 */

// C99
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// GLib
#include <glib.h>

#include "sord-config.h"
#include "sord/sord.h"

#define SORD_LOG(prefix, ...) fprintf(stderr, "[Sord::" prefix "] " __VA_ARGS__)

#ifdef SORD_DEBUG_ITER
	#define SORD_ITER_LOG(...) SORD_LOG("iter", __VA_ARGS__)
#else
	#define SORD_ITER_LOG(...)
#endif
#ifdef SORD_DEBUG_SEARCH
	#define SORD_FIND_LOG(...) SORD_LOG("search", __VA_ARGS__)
#else
	#define SORD_FIND_LOG(...)
#endif
#ifdef SORD_DEBUG_WRITE
	#define SORD_WRITE_LOG(...) SORD_LOG("write", __VA_ARGS__)
#else
	#define SORD_WRITE_LOG(...)
#endif

#define NUM_ORDERS          12
#define STATEMENT_LEN       3
#define TUP_LEN             STATEMENT_LEN + 1
#define DEFAULT_ORDER       SPO
#define DEFAULT_GRAPH_ORDER GSPO

#define TUP_FMT         "(%d %d %d %d)"
#define TUP_FMT_ARGS(t) ((t)[0]), ((t)[1]), ((t)[2]), ((t)[3])

#define TUP_S 0
#define TUP_P 1
#define TUP_O 2
#define TUP_G 3

/** Triple ordering */
typedef enum {
	 SPO, ///<         Subject,   Predicate, Object
	 SOP, ///<         Subject,   Object,    Predicate
	 OPS, ///<         Object,    Predicate, Subject
	 OSP, ///<         Object,    Subject,   Predicate
	 PSO, ///<         Predicate, Subject,   Object
	 POS, ///<         Predicate, Object,    Subject
	GSPO, ///< Graph,  Subject,   Predicate, Object
	GSOP, ///< Graph,  Subject,   Object,    Predicate
	GOPS, ///< Graph,  Object,    Predicate, Subject
	GOSP, ///< Graph,  Object,    Subject,   Predicate
	GPSO, ///< Graph,  Predicate, Subject,   Object
	GPOS, ///< Graph,  Predicate, Object,    Subject
} SordOrder;

/** String name of each ordering (array indexed by SordOrder) */
static const char* const order_names[NUM_ORDERS] = {
	 "spo",  "sop",  "ops",  "osp",  "pso",  "pos",
	"gspo", "gsop", "gops", "gosp", "gpso", "gpos"
};

/** Tuples of indices for each order, from most to least significant
 * (array indexed by SordOrder)
 */
static const int orderings[NUM_ORDERS][TUP_LEN] = {
	{  0,1,2,3}, {  0,2,1,3}, {  2,1,0,3}, {  2,0,1,3}, {  1,0,2,3}, {  1,2,0,3},
	{3,0,1,2  }, {3,0,2,1  }, {3,2,1,0  }, {3,2,0,1  }, {3,1,0,2  }, {3,1,2,0  }
};

/** Store */
struct _Sord {
	GHashTable* names;    ///< URI or blank node identifier string => ID
	GHashTable* literals; ///< Literal => ID

	/** Index for each possible triple ordering (may or may not exist).
	 * If an index for e.g. SPO exists, it is a dictionary with
	 * (S P O) keys (as a SordTuplrID), and ignored values.
	 */
	GSequence* indices[NUM_ORDERS];

	void (*user_data_free)(void*); ///< Destructor for node user data

	SordCount n_tuples;
	SordCount n_nodes;
};

/** Mode for searching or iteration */
typedef enum {
	ALL,          ///< Iterate to end of store, returning all results, no filtering
	SINGLE,       ///< Iteration over a single element (exact search)
	RANGE,        ///< Iterate over range with equal prefix
	FILTER_RANGE, ///< Iterate over range with equal prefix, filtering
	FILTER_ALL    ///< Iterate to end of store, filtering
} SearchMode;

/** Iterator over some range of a store */
struct _SordIter {
	Sord           sord;              ///< Store this is an iterator for
	GSequenceIter* cur;               ///< Current DB cursor
	SordTuple      pat;               ///< Iteration pattern (in ordering order)
	int            ordering[TUP_LEN]; ///< Store ordering
	SearchMode     mode;              ///< Iteration mode
	int            n_prefix;          ///< Length of range prefix (RANGE, FILTER_RANGE)
	bool           end;               ///< True iff reached end
	bool           skip_graphs;       ///< True iff iteration should ignore graphs
};

/** Node */
struct _SordNode {
	SordNodeType type;       ///< SordNodeType
	size_t       n_bytes;    ///< Length of data in bytes (including terminator)
	SordCount    refs;       ///< Reference count (i.e. number of containing tuples)
	void*        user_data;  ///< Opaque user data
	SordNode     datatype;   ///< Literal data type (ID of a URI node, or 0)
	const char*  lang;       ///< Literal language (interned string)
	char*        buf;        ///< Value (string)
};

static unsigned
sord_literal_hash(const void* n)
{
	SordNode node = (SordNode)n;
	return g_str_hash(node->buf) + g_str_hash(node->lang);
}
		
static gboolean
sord_literal_equal(const void* a, const void* b)
{
	SordNode a_node = (SordNode)a;
	SordNode b_node = (SordNode)b;
	// FIXME: type, lang
	return g_str_equal(sord_node_get_string(a_node),
	                   sord_node_get_string(b_node));
}

static inline int
sord_node_compare(Sord sord, const SordNode a, const SordNode b)
{
	if (a->type != b->type)
		return a->type - b->type;

	switch ((SordNodeType)a->type) {
	case SORD_URI:
	case SORD_BLANK:
		return strcmp(a->buf, b->buf);
	case SORD_LITERAL:
		// TODO: lang, type
		return strcmp(sord_node_get_string(a), sord_node_get_string(b));
	}
	assert(false);
	return 0;
}

/** Compare two IDs (dereferencing if necessary).
 * The null ID, 0, is treated as a minimum (it is less than every other
 * possible ID, except itself).  This allows it to be used as a wildcard
 * when searching, ensuring the search will reach the minimum possible
 * key in the tree and iteration from that point will produce the entire
 * result set.
 */
static inline int
sord_id_compare(Sord sord, const SordID a, const SordID b)
{
	if (a == b || !a || !b) {
		return (const char*)a - (const char*)b;
	} else {
		SordNode a_node = sord_node_load(sord, a);
		SordNode b_node = sord_node_load(sord, b);
		const int ret = sord_node_compare(sord, a_node, b_node);
		return ret;
	}
}

/** Return true iff IDs are equivalent, or one is a wildcard */
static inline bool
sord_id_match(const SordID a, const SordID b)
{
	return !a || !b || (a == b);
}

static inline bool
sord_tuple_match_inline(const SordTuple x, const SordTuple y)
{
	return sord_id_match(x[0], y[0])
		&& sord_id_match(x[1], y[1])
		&& sord_id_match(x[2], y[2])
		&& sord_id_match(x[3], y[3]);
}

bool
sord_tuple_match(const SordTuple x, const SordTuple y)
{
	return sord_tuple_match_inline(x, y);
}

void
sord_tuple_load(Sord sord, SordTuple tup, SordNode* s, SordNode* p, SordNode* o)
{
	*s = sord_node_load(sord, tup[TUP_S]);
	*p = sord_node_load(sord, tup[TUP_P]);
	*o = sord_node_load(sord, tup[TUP_O]);
}

/** Compare two tuple IDs lexicographically.
 * NULL IDs (equal to 0) are treated as wildcards, always less than every
 * other possible ID, except itself.
 */
static int
sord_tuple_compare(const void* x_ptr, const void* y_ptr, void* user_data)
{
	Sord    const sord = (Sord)user_data;
	SordID* const x    = (SordID*)x_ptr;
	SordID* const y    = (SordID*)y_ptr;

	for (int i = 0; i < TUP_LEN; ++i) {
		const int cmp = sord_id_compare(sord, x[i], y[i]);
		if (cmp)
			return cmp;
	}

	return 0;
}

static inline bool
sord_iter_next(SordIter iter)
{
	if (!iter->skip_graphs) {
		iter->cur = g_sequence_iter_next(iter->cur);
		return !g_sequence_iter_is_end(iter->cur);
	}

	const SordID*   key     = (const SordID*)g_sequence_get(iter->cur);
	const SordTuple initial = { key[0], key[1], key[2], key[3] };
	while (true) {
		iter->cur = g_sequence_iter_next(iter->cur);
		if (g_sequence_iter_is_end(iter->cur))
			return true;

		key = (const SordID*)g_sequence_get(iter->cur);
		for (int i = 0; i < 3; ++i)
			if (key[i] != initial[i])
				return false;
	}
	assert(false);
}

/** Seek forward as necessary until @a iter points at a match.
 * @return true iff iterator reached end of valid range.
 */
static inline bool
sord_iter_seek_match(SordIter iter)
{
	for (iter->end = true;
	     !g_sequence_iter_is_end(iter->cur);
	     sord_iter_next(iter)) {
		const SordID* const key = (const SordID*)g_sequence_get(iter->cur);
		if (sord_tuple_match_inline(key, iter->pat))
			return (iter->end = false);
	}
	return true;
}

/** Seek forward as necessary until @a iter points at a match, or the prefix
 * no longer matches iter->pat.
 * @return true iff iterator reached end of valid range.
 */
static inline bool
sord_iter_seek_match_range(SordIter iter)
{
	if (iter->end)
		return true;

	do {
		const SordID* key = (const SordID*)g_sequence_get(iter->cur);

		if (sord_tuple_match_inline(key, iter->pat))
			return false; // Found match

		for (int i = 0; i < iter->n_prefix; ++i) {
			if (!sord_id_match(key[i], iter->pat[i])) {
				iter->end = true; // Reached end of valid range
				return true;
			}
		}
	} while (!sord_iter_next(iter));

	return (iter->end = true); // Reached end
}

static SordIter
sord_iter_new(Sord sord, GSequenceIter* cur, const SordTuple pat,
              SordOrder order, SearchMode mode, int n_prefix)
{
	const int* ordering = orderings[order];

	SordIter iter = malloc(sizeof(struct _SordIter));
	iter->sord        = sord;
	iter->cur         = cur;
	iter->mode        = mode;
	iter->n_prefix    = n_prefix;
	iter->end         = false;
	iter->skip_graphs = order < GSPO;
	for (int i = 0; i < TUP_LEN; ++i) {
		iter->pat[i]      = pat[ordering[i]];
		iter->ordering[i] = ordering[i];
	}

	switch (iter->mode) {
	case ALL:
	case SINGLE:
	case RANGE:
		assert(sord_tuple_match_inline(
			       (const SordID*)g_sequence_get(iter->cur),
		           iter->pat));
		break;
	case FILTER_RANGE:
		sord_iter_seek_match_range(iter);
		break;
	case FILTER_ALL:
		sord_iter_seek_match(iter);
		break;
	}

#ifdef SORD_DEBUG_ITER
	SordTuple value;
	sord_iter_get(iter, value);
	SORD_ITER_LOG("New %p pat=" TUP_FMT " cur=" TUP_FMT " end=%d\n", (void*)iter,
	              TUP_FMT_ARGS(pat), TUP_FMT_ARGS(value), iter->end);
#endif
	return iter;
}

Sord
sord_iter_get_sord(SordIter iter)
{
	return iter->sord;
}

void
sord_iter_get(SordIter iter, SordTuple id)
{
	const SordID* key = (const SordID*)g_sequence_get(iter->cur);
	id[iter->ordering[0]] = key[0];
	id[iter->ordering[1]] = key[1];
	id[iter->ordering[2]] = key[2];
	id[iter->ordering[3]] = key[3];
}

bool
sord_iter_increment(SordIter iter)
{
	if (iter->end)
		return true;

	const SordID* key;
	iter->end = sord_iter_next(iter);
	if (!iter->end) {
		switch (iter->mode) {
		case ALL:
			// At the end if the cursor is (assigned above)
			break;
		case SINGLE:
			iter->end = true;
			break;
		case RANGE:
			// At the end if the MSN no longer matches
			key = (const SordID*)g_sequence_get(iter->cur);
			assert(key);
			for (int i = 0; i < iter->n_prefix; ++i) {
				if (!sord_id_match(key[i], iter->pat[i])) {
					iter->end = true;
					break;
				}
			}
			break;
		case FILTER_RANGE:
			// Seek forward to next match, stopping if prefix changes
			sord_iter_seek_match_range(iter);
			break;
		case FILTER_ALL:
			// Seek forward to next match
			sord_iter_seek_match(iter);
			break;
		}
	}

	if (iter->end) {
		SORD_ITER_LOG("%p Reached end\n", (void*)iter);
		return true;
	} else {
#ifdef SORD_DEBUG_ITER
		SordTuple tup;
		sord_iter_get(iter, tup);
		SORD_ITER_LOG("%p Increment to " TUP_FMT "\n", (void*)iter, TUP_FMT_ARGS(tup));
#endif
		return false;
	}
}

bool
sord_iter_is_end(SordIter iter)
{
	return !iter || iter->end;
}

void
sord_iter_free(SordIter iter)
{
	SORD_ITER_LOG("%p Free\n", (void*)iter);
	if (iter) {
		free(iter);
	}
}

/** Return true iff @a sord has an index for @a order.
 * If @a graph_search is true, @a order will be modified to be the
 * corresponding order with a G prepended (so G will be the MSN).
 */
static inline bool
sord_has_index(Sord sord, SordOrder* order, int* n_prefix, bool graph_search)
{
	if (graph_search) {
		*order    += GSPO;
		*n_prefix += 1;
	}

	return sord->indices[*order];
}

/** Return the best available index for a pattern.
 * @param pat Pattern in standard (S P O G) order
 * @param mode Set to the (best) iteration mode for iterating over results
 * @param n_prefix Set to the length of the range prefix
 *        (for @a mode == RANGE and @a mode == FILTER_RANGE)
 */
static inline SordOrder
sord_best_index(Sord sord, const SordTuple pat, SearchMode* mode, int* n_prefix)
{
	const bool graph_search = (pat[TUP_G] != 0);

	const unsigned sig
		= (pat[0] ? 1 : 0) * 0x100
		+ (pat[1] ? 1 : 0) * 0x010
		+ (pat[2] ? 1 : 0) * 0x001;

	SordOrder good[2];

	// Good orderings that don't require filtering
	*mode     = RANGE;
	*n_prefix = 0;
	switch (sig) {
	case 0x000: *mode = ALL; return graph_search ? DEFAULT_GRAPH_ORDER : DEFAULT_ORDER;
	case 0x001: *mode = RANGE; good[0] = OPS; good[1] = OSP; *n_prefix = 1; break;
	case 0x010: *mode = RANGE; good[0] = POS; good[1] = PSO; *n_prefix = 1; break;
	case 0x011: *mode = RANGE; good[0] = OPS; good[1] = POS; *n_prefix = 2; break;
	case 0x100: *mode = RANGE; good[0] = SPO; good[1] = SOP; *n_prefix = 1; break;
	case 0x101: *mode = RANGE; good[0] = SOP; good[1] = OSP; *n_prefix = 2; break;
	case 0x110: *mode = RANGE; good[0] = SPO; good[1] = PSO; *n_prefix = 2; break;
	case 0x111: *mode = SINGLE; return graph_search ? DEFAULT_GRAPH_ORDER : DEFAULT_ORDER;
	}

	if (sord_has_index(sord, &good[0], n_prefix, graph_search)) {
		return good[0];
	} else if (sord_has_index(sord, &good[1], n_prefix, graph_search)) {
		return good[1];
	}

	// Not so good orderings that require filtering, but can
	// still be constrained to a range
	switch (sig) {
	case 0x011: *mode = FILTER_RANGE; good[0] = OSP; good[1] = PSO; *n_prefix = 1; break;
	case 0x101: *mode = FILTER_RANGE; good[0] = SPO; good[1] = OPS; *n_prefix = 1; break;
	case 0x110: *mode = FILTER_RANGE; good[0] = SOP; good[1] = POS; *n_prefix = 1; break;
	default: break;
	}

	if (*mode == FILTER_RANGE) {
		if (sord_has_index(sord, &good[0], n_prefix, graph_search)) {
			return good[0];
		} else if (sord_has_index(sord, &good[1], n_prefix, graph_search)) {
			return good[1];
		}
	}

	if (graph_search) {
		*mode = FILTER_RANGE;
		*n_prefix = 1;
		return DEFAULT_GRAPH_ORDER;
	} else {
		*mode = FILTER_ALL;
		return DEFAULT_ORDER;
	}
}

Sord
sord_new()
{
	Sord sord = (Sord)malloc(sizeof(struct _Sord));
	sord->names    = g_hash_table_new_full(g_str_hash, g_str_equal, 0, 0);
	sord->literals = g_hash_table_new_full(sord_literal_hash, sord_literal_equal, 0, 0);
	sord->user_data_free = NULL;

	for (unsigned i = 0; i < NUM_ORDERS; ++i)
		sord->indices[i] = NULL;

	return sord;
}

void
sord_free(Sord sord)
{
	if (!sord)
		return;

	g_hash_table_unref(sord->names);
	g_hash_table_unref(sord->literals);
	for (unsigned i = 0; i < NUM_ORDERS; ++i)
		if (sord->indices[i])
			g_sequence_free(sord->indices[i]);

	free(sord);
}

void
sord_set_option(Sord sord, const char* key, const char* value,
                SordNodeType type, const char* datatype, const char* lang)
{
	const char* const prefix     = "http://drobilla.net/ns/sord#";
	const size_t      prefix_len =  strlen(prefix);
	if (strncmp(key, prefix, prefix_len)) {
		fprintf(stderr, "Unknown option %s\n", key);
		return;
	}

	const char* option        = key + prefix_len;
	const bool  value_is_true = !strcmp(value, "true") || !strcmp(value, "1") || !strcmp(value, "yes");
	if (!strcmp(option, "index-all")) {
		for (int i = 0; i < NUM_ORDERS; ++i) {
			sord->indices[i] = g_sequence_new(NULL);
		}
	} else if (!strncmp(option, "index-", 6) && value_is_true) {
		for (int i = 0; i < NUM_ORDERS; ++i) {
			if (!strcmp(option + 6, order_names[i])) {
				sord->indices[i] = g_sequence_new(NULL);
				return;
			}
		}
	} else {
		fprintf(stderr, "Unknown option %s\n", key);
	}
}

bool
sord_open(Sord sord)
{
	sord->n_tuples = sord->n_nodes = 0;

	bool no_indices = true;
	for (unsigned i = 0; i < NUM_ORDERS; ++i) {
		if (sord->indices[i]) {
			no_indices = false;
			break;
		}
	}

	if (no_indices) {
		// Use default indexing, avoids O(n) in all cases
		sord->indices[SPO]  = g_sequence_new(NULL);
		sord->indices[OPS]  = g_sequence_new(NULL);
		sord->indices[PSO]  = g_sequence_new(NULL);
		sord->indices[GSPO] = g_sequence_new(NULL); // XXX: default?  do on demand?
	}

	if (!sord->indices[DEFAULT_ORDER])
		sord->indices[DEFAULT_ORDER] = g_sequence_new(NULL);

	return true;
}

static void
sord_drop_node(Sord sord, SordID id)
{
	// FIXME: leak?
}

int
sord_num_tuples(Sord sord)
{
	return sord->n_tuples;
}

int
sord_num_nodes(Sord sord)
{
	return sord->n_nodes;
}

void
sord_node_set_user_data_free_function(Sord sord, void (*f)(void*))
{
	sord->user_data_free = f;
}

SordIter
sord_begin(Sord sord)
{
	if (sord_num_tuples(sord) == 0) {
		return NULL;
	} else {
		GSequenceIter* cur = g_sequence_get_begin_iter(sord->indices[DEFAULT_ORDER]);
		SordTuple pat = { 0, 0, 0, 0 };
		return sord_iter_new(sord, cur, pat, DEFAULT_ORDER, ALL, 0);
	}
}

SordIter
sord_graphs_begin(Sord read)
{
	return NULL;
}

static inline GSequenceIter*
index_search(Sord sord, GSequence* db, const SordTuple search_key)
{
	return  g_sequence_search(
		db, (void*)search_key, sord_tuple_compare, sord);
}

static inline GSequenceIter*
index_lower_bound(Sord sord, GSequence* db, const SordTuple search_key)
{
	GSequenceIter* i = g_sequence_search(
		db, (void*)search_key, sord_tuple_compare, sord);

	/* i is now at the position where search_key would be inserted,
	   but this is not necessarily a match, and we need the leftmost...
	*/

	if (g_sequence_iter_is_begin(i)) {
		return i;
	} else if (g_sequence_iter_is_end(i)) {
		i = g_sequence_iter_prev(i);
	}

	if (!sord_tuple_match_inline(search_key, g_sequence_get(i))) {
		// No match, but perhaps immediately after a match
		GSequenceIter* const prev = g_sequence_iter_prev(i);
		if (!sord_tuple_match_inline(search_key, g_sequence_get(prev))) {
			return i; // No match (caller must check)
		} else {
			i = prev;
		}
	}

	/* i now points to some matching element,
	   but not necessarily the first...
	*/
	assert(sord_tuple_match_inline(search_key, g_sequence_get(i)));

	while (!g_sequence_iter_is_begin(i)) {
		if (sord_tuple_match_inline(search_key, g_sequence_get(i))) {
			GSequenceIter* const prev = g_sequence_iter_prev(i);
			if (sord_tuple_match_inline(search_key, g_sequence_get(prev))) {
				i = prev;
				continue;
			}
		}
		break;
	}

	return i;
}

SordIter
sord_find(Sord sord, const SordTuple pat)
{
	if (!pat[0] && !pat[1] && !pat[2] && !pat[3])
		return sord_begin(sord);

	SearchMode          mode;
	int                 prefix_len;
	const SordOrder     index_order = sord_best_index(sord, pat, &mode, &prefix_len);
	const int* const    ordering    = orderings[index_order];

	SORD_FIND_LOG("Find " TUP_FMT "  index=%s  mode=%d  prefix_len=%d ordering=%d%d%d%d\n",
			TUP_FMT_ARGS(pat), order_names[index_order], mode, prefix_len,
			ordering[0], ordering[1], ordering[2], ordering[3]);

	// It's easiest to think about this algorithm in terms of (S P O) ordering,
	// assuming (A B C) == (S P O).  For other orderings this is not actually
	// the case, but it works the same way.
	const SordID a = pat[ordering[0]]; // Most Significant Node (MSN)
	const SordID b = pat[ordering[1]]; // ...
	const SordID c = pat[ordering[2]]; // ...
	const SordID d = pat[ordering[3]]; // Least Significant Node (LSN)

	if (a && b && c)
		mode = SINGLE; // No duplicate tuples (Sord is a set)

	SordTuple            search_key = { a, b, c, d };
	GSequence* const     db         = sord->indices[index_order];
	GSequenceIter* const cur        = index_lower_bound(sord, db, search_key);
	const SordID* const  key        = (const SordID*)g_sequence_get(cur);
	if (!key || ( (mode == RANGE || mode == SINGLE)
	              && !sord_tuple_match_inline(search_key, key) )) {
		SORD_FIND_LOG("No match found\n");
		return NULL;
	}

	return sord_iter_new(sord, cur, pat, index_order, mode, prefix_len);
}

static SordID
sord_lookup_name(Sord sord, const char* str, int str_len)
{
	return g_hash_table_lookup(sord->names, str);
}

static SordNode
sord_new_node(SordNodeType type, const char* data, size_t n_bytes)
{
	SordNode node = malloc(sizeof(struct _SordNode));
	node->type      = type;
	node->n_bytes   = n_bytes;
	node->refs      = 0;
	node->user_data = 0;
	node->datatype  = 0;
	node->lang      = 0;
	node->buf       = g_strdup(data); // TODO: add no-copy option
	return node;
}

static SordNode
sord_new_literal_node(Sord sord, SordNode datatype,
                      const char* str,  int str_len,
                      const char* lang, uint8_t lang_len)
{
	SordNode node = sord_new_node(SORD_LITERAL, str, str_len + 1);
	node->datatype = datatype;
	node->lang     = g_intern_string(str);
	return node;
}

static SordNode
sord_lookup_literal(Sord sord, SordNode type,
                    const char* str,  int     str_len,
                    const char* lang, uint8_t lang_len)
{
	SordNode node = sord_new_literal_node(sord, type, str, str_len, lang, lang_len);
	SordNode id   = g_hash_table_lookup(sord->literals, node);
	free(node);
	if (id) {
		return id;
	} else {
		return 0;
	}
}

SordNode
sord_node_load(Sord sord, SordID id)
{
	return (SordNode)id;
}

SordNodeType
sord_node_get_type(SordNode ref)
{
	return ref->type;
}

const char*
sord_node_get_string(SordNode ref)
{
	return ref->buf;
}

void
sord_node_set_user_data(SordNode ref, void* user_data)
{
	ref->user_data = user_data;
}

void*
sord_node_get_user_data(SordNode ref)
{
	return ref->user_data;
}

const char*
sord_literal_get_lang(SordNode ref)
{
	return ref->lang;
}

SordNode
sord_literal_get_datatype(SordNode ref)
{
	return ref->datatype;
}

static void
sord_add_node(Sord sord, SordNode node)
{
	node->refs = 0;
	++sord->n_nodes;
}

SordID
sord_get_uri_counted(Sord sord, bool create, const char* str, int str_len)
{
	SordID id = sord_lookup_name(sord, str, str_len);
	if (id || !create)
		return id;

	id = sord_new_node(SORD_URI, str, str_len + 1);

	assert(id);
	g_hash_table_insert(sord->names, (void*)g_strdup(str), (void*)id);
	sord_add_node(sord, id);
	
	return id;
}

SordID
sord_get_uri(Sord sord, bool create, const char* str)
{
	return sord_get_uri_counted(sord, create, str, strlen(str));
}

SordID
sord_get_blank_counted(Sord sord, bool create, const char* str, int str_len)
{
	SordID id = sord_lookup_name(sord, str, str_len);
	if (id || !create)
		return id;

	id = sord_new_node(SORD_BLANK, str, str_len + 1);

	assert(id);
	g_hash_table_insert(sord->names, (void*)g_strdup(str), (void*)id);
	sord_add_node(sord, id);
	
	return id;
}

SordID
sord_get_blank(Sord sord, bool create, const char* str)
{
	return sord_get_blank_counted(sord, create, str, strlen(str));
}

SordID
sord_get_literal_counted(Sord sord, bool create, SordID type,
                         const char* str,  int     str_len,
                         const char* lang, uint8_t lang_len)
{
	SordID id = sord_lookup_literal(sord, type, str, str_len, lang, lang_len);
	if (id || !create)
		return id;

	id = sord_new_literal_node(sord, type, str, str_len, lang, lang_len);

	g_hash_table_insert(sord->literals, (void*)id, id);
	sord_add_node(sord, id);
	
	return id;
}

SordID
sord_get_literal(Sord sord, bool create, SordID type, const char* str, const char* lang)
{
	return sord_get_literal_counted(sord, create, type, str, strlen(str),
			lang, lang ? strlen(lang) : 0);
}

static void
sord_add_tuple_ref(Sord sord, const SordID id)
{
	if (id) {
		SordNode node = sord_node_load(sord, id);
		++node->refs;
	}
}

static void
sord_drop_tuple_ref(Sord sord, const SordID id)
{
	if (id) {
		SordNode node = sord_node_load(sord, id);
		if (--node->refs == 0) {
			sord_drop_node(sord, id);
		}
	}
}

static inline bool
sord_add_to_index(Sord sord, const SordTuple tup, SordOrder order)
{
	assert(sord->indices[order]);
	const int* const ordering = orderings[order];
	const SordTuple key = {
		tup[ordering[0]], tup[ordering[1]], tup[ordering[2]], tup[ordering[3]]
	};
	GSequenceIter* const cur = index_search(sord, sord->indices[order], key);
	if (!g_sequence_iter_is_end(cur)
	    && !sord_tuple_compare(g_sequence_get(cur), key, sord)) {
		return false;  // Tuple already stored in this index
	}

	// FIXME: memory leak?
	SordID* key_copy = malloc(sizeof(SordTuple));
	memcpy(key_copy, key, sizeof(SordTuple));
	g_sequence_insert_before(cur, key_copy);
	return true;
}

void
sord_add(Sord sord, const SordTuple tup)
{
	SORD_WRITE_LOG("Add " TUP_FMT "\n", TUP_FMT_ARGS(tup));
	assert(tup[0] && tup[1] && tup[2]);

	for (unsigned i = 0; i < NUM_ORDERS; ++i) {
		if (sord->indices[i]) {
			if (!sord_add_to_index(sord, tup, i)) {
				assert(i == 0); // Assuming index coherency
				return; // Tuple already stored, do nothing
			}
		}
	}

	for (int i = 0; i < TUP_LEN; ++i)
		sord_add_tuple_ref(sord, tup[i]);

	++sord->n_tuples;
	assert(sord->n_tuples == g_sequence_get_length(sord->indices[SPO]));
}

void
sord_remove(Sord sord, const SordTuple tup)
{
	SORD_WRITE_LOG("Remove " TUP_FMT "\n", TUP_FMT_ARGS(tup));
		
	for (unsigned i = 0; i < NUM_ORDERS; ++i) {
		if (sord->indices[i]) {
			const int* const ordering = orderings[i];
			const SordTuple key = {
				tup[ordering[0]], tup[ordering[1]], tup[ordering[2]], tup[ordering[3]]
			};
			GSequenceIter* const cur = index_search(sord, sord->indices[i], key);
			if (!g_sequence_iter_is_end(cur)) {
				g_sequence_remove(cur);
			} else {
				assert(i == 0); // Assuming index coherency
				return; // Tuple not found, do nothing
			}
		}
	}

	for (int i = 0; i < TUP_LEN; ++i)
		sord_drop_tuple_ref(sord, tup[i]);

	--sord->n_tuples;
}

void
sord_remove_iter(Sord sord, SordIter iter)
{
	SordTuple tup;
	sord_iter_get(iter, tup);

	SORD_WRITE_LOG("Remove " TUP_FMT "\n", TUP_FMT_ARGS(tup));
	
	// TODO: Directly remove the iterator's index (avoid one search)

	for (unsigned i = 0; i < NUM_ORDERS; ++i) {
		if (sord->indices[i]) {
			const int* const ordering = orderings[i];
			const SordTuple key = {
				tup[ordering[0]], tup[ordering[1]], tup[ordering[2]], tup[ordering[3]]
			};
			GSequenceIter* const cur = index_search(sord, sord->indices[i], key);
			if (!g_sequence_iter_is_end(cur)) {
				g_sequence_remove(cur);
			} else {
				assert(i == 0); // Assuming index coherency
			}
		}
	}
	
	for (int i = 0; i < TUP_LEN; ++i)
		sord_drop_tuple_ref(sord, tup[i]);

	--sord->n_tuples;

	iter->end = g_sequence_iter_is_end(iter->cur);
}

void
sord_remove_graph(Sord sord, SordID graph)
{
	#if 0
	if (!sord->indices[GSPO])
		return;

	// Remove all tuples in graph from non-graph indices
	BDBCUR*         cur        = tcbdbcurnew(sord->indices[GSPO]);
	const SordTuple search_key = { graph, 0, 0, 0 };
	int             key_size   = sizeof(SordTuple);
	tcbdbcurjump(cur, &search_key, key_size);
	do {
		const SordID* key = (const SordID*)tcbdbcurkey3(cur, &key_size);
		if (!key || key[0] != graph)
			break;

		for (unsigned i = 0; i < GSPO; ++i) {
			if (sord->indices[i]) {
				const int* const ordering = orderings[i];
				const SordTuple tup = { key[1], key[2], key[3], key[0] }; // Key in SPOG order
				const SordTuple subkey = {
					tup[ordering[0]], tup[ordering[1]], tup[ordering[2]], tup[ordering[3]]
				};
				if (!tcbdbout(sord->indices[i], &subkey, sizeof(SordTuple)))
					fprintf(stderr, "Failed to remove key " TUP_FMT "\n", TUP_FMT_ARGS(subkey));
			}
		}

		--sord->n_tuples;
	} while (tcbdbcurnext(cur));

	// Remove all tuples in graph from graph indices
	for (unsigned i = GSPO; i < NUM_ORDERS; ++i) {
		if (sord->indices[i]) {
			BDBCUR* cur = tcbdbcurnew(sord->indices[i]);
			tcbdbcurjump(cur, &search_key, key_size);
			while (true) {
				const SordID* key = (const SordID*)tcbdbcurkey3(cur, &key_size);
				if (!key || key[0] != graph) {
					break;
				} else if (i == GSPO) {
					for (int i = 0; i < TUP_LEN; ++i) {
						sord_drop_tuple_ref(sord, key[i]);
					}
				}
				if (!tcbdbcurout(cur))
					break;
			}
		}
	}
	#endif
}
