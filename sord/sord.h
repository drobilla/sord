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
 * Public Sord API.
 */

#ifndef SORD_SORD_H
#define SORD_SORD_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#if defined _WIN32 || defined __CYGWIN__
	#define SORD_LIB_IMPORT __declspec(dllimport)
	#define SORD_LIB_EXPORT __declspec(dllexport)
#else
	#define SORD_LIB_IMPORT __attribute__ ((visibility("default")))
	#define SORD_LIB_EXPORT __attribute__ ((visibility("default")))
#endif

#ifdef SORD_SHARED // Building a shared library
	#ifdef SORD_INTERNAL // Building SORD (not using it)
		#define SORD_API SORD_LIB_EXPORT
	#else
		#define SORD_API SORD_LIB_IMPORT
	#endif
#else // Building a static library
	#define SORD_API
#endif

/** @defgroup sord Sord
 * A lightweight RDF model library.
 * Sord stores RDF (subject object predicate) triples, where triples may
 * have an added "context" field, i.e. (subject object predicate context).
 * @{
 */

typedef struct _Sord*     Sord;     ///< Tuple store
typedef struct _SordIter* SordIter; ///< Store iterator
typedef struct _SordNode* SordNode; ///< Node

typedef void* SordID;    ///< Integer ID of a Node (0 reserved for NULL)
typedef int   SordCount; ///< Count of nodes or triples


/** Tuple of IDs (statement), or a tuple pattern.
 * Nodes are ordered (S P O G).  The ID of the default graph is 0.
 */
typedef SordID SordTuple[4];

typedef enum {
	SORD_SUBJECT   = 0,
	SORD_PREDICATE = 1,
	SORD_OBJECT    = 2,
	SORD_GRAPH     = 3
} SordTupleIndex;

/** Type of a node */
typedef enum {
	SORD_URI     = 1, ///< URI
	SORD_BLANK   = 2, ///< Blank node identifier
	SORD_LITERAL = 3  ///< Literal (string with optional lang and/or type)
} SordNodeType;

/** @name Initialisation and Cleanup
 * @{
 */

/** Create a new store. */
SORD_API
Sord
sord_new(void);

/** Set a store option.
 * Options are RDF properties (with the store as the subject), with the
 * restriction that each has at most one value.
 * If @a value is NULL, the option is unset (i.e. the property is removed).
 *
 * Known options (with acceptable types):
 *  - http://drobilla.net/ns/sord#index-spo :: xsd:boolean
 *  - http://drobilla.net/ns/sord#index-sop :: xsd:boolean
 *  - http://drobilla.net/ns/sord#index-ops :: xsd:boolean
 *  - http://drobilla.net/ns/sord#index-osp :: xsd:boolean
 *  - http://drobilla.net/ns/sord#index-pso :: xsd:boolean
 *  - http://drobilla.net/ns/sord#index-pos :: xsd:boolean
 *
 * @param sord     Store
 * @param key      URI of this option (predicate)
 * @param value    String of option value (object)
 * @param type     Type of @a value
 * @param datatype Data type of @a value (if @a type is SORD_LITERAL), or NULL
 * @param lang     Language of @a value (if @a type is SORD_LITERAL), or NULL
 */
SORD_API
void
sord_set_option(Sord sord, const char* key, const char* value,
                SordNodeType type, const char* datatype, const char* lang);

/** Open store on disk. */
SORD_API
bool
sord_open(Sord sord);

/** Close store on disk. */
SORD_API
void
sord_close(Sord sord);

/** Close and free @a sord, leaving disk data intact. */
SORD_API
void
sord_free(Sord sord);

/** @} */
/** @name Node Loading
 * Searching for node IDs by value and loading nodes from disk by ID.
 * @{
 */

/** Set the free function for node opaque user data. */
SORD_API
void
sord_node_set_user_data_free_function(Sord sord, void (*f)(void*));

/** Dereference an ID, loading node data into memory.
 * The returned node is allocated memory owned by @a sord,
 * it can only be freed by the caller via sord_clear_cache.
 */
SORD_API
SordNode
sord_node_load(Sord sord, SordID id);

/** Set @a s, @a p, and @a o to the nodes in @a tup. */
SORD_API
void
sord_tuple_load(Sord      sord,
                SordTuple tup,
                SordNode* s,
                SordNode* p,
                SordNode* o);

/** Find a URI, creating a new one if necessary iff @a create is true.
 * Use sord_get_uri_counted instead if the length of @a str is known.
 */
SORD_API
SordID
sord_get_uri(Sord sord, bool create, const uint8_t* str);

/** Find a URI, creating a new one if necessary iff @a create is true. */
SORD_API
SordID
sord_get_uri_counted(Sord sord, bool create, const uint8_t* str, int str_len);

/** Find a blank, creating a new one if necessary iff @a create is true
 * Use sord_get_blank_counted instead if the length of @a str is known.
 */
SORD_API
SordID
sord_get_blank(Sord sord, bool create, const uint8_t* str);

/** Find a blank, creating a new one if necessary iff @a create is true. */
SORD_API
SordID
sord_get_blank_counted(Sord sord, bool create, const uint8_t* str, int str_len);

/** Find a literal, creating a new one if necessary iff @a create is true.
 * Use sord_get_literal_counted instead if the length of @a str is known.
 */
SORD_API
SordID
sord_get_literal(Sord sord, bool create, SordID type,
                 const uint8_t* str, const char* lang);

/** Find a literal, creating a new one if necessary iff @a create is true. */
SORD_API
SordID
sord_get_literal_counted(Sord sord, bool create, SordID type,
                         const uint8_t* str,  int     str_len,
                         const char*    lang, uint8_t lang_len);


/** @} */
/** @name Node Values
 * Investigating loaded (in-memory) node values.
 * @{
 */

/** Return the type of a node (SORD_URI, SORD_BLANK, or SORD_LITERAL). */
SORD_API
SordNodeType
sord_node_get_type(SordNode node);

/** Return the string value of a node. */
SORD_API
const uint8_t*
sord_node_get_string(SordNode node);

SORD_API
const uint8_t*
sord_node_get_string_counted(SordNode node, size_t* len);

/** Set an opaque user pointer on a node.
 * Sord does not interpret the user pointer in any way, but it will be
 * preserved on the node until the cache is cleared.
 */
SORD_API
void
sord_node_set_user_data(SordNode node, void* user_data);

/** Get the opaque user pointer on a node. */
SORD_API
void*
sord_node_get_user_data(SordNode node);

/** Get the language of a literal node. */
SORD_API
const char*
sord_literal_get_lang(SordNode node);

/** Get the datatype URI of a literal node. */
SORD_API
SordNode
sord_literal_get_datatype(SordNode node);

SORD_API
bool
sord_node_equals(const SordNode a, const SordNode b);

/** @} */
/** @name Read Operations
 * @{
 */

/** Return the number of nodes stored in @a sord.
 * Nodes are included in this count iff they are a part of a tuple in @a sord.
 */
SORD_API
int
sord_num_nodes(Sord read);

/** Return the number of tuples stored in @a sord. */
SORD_API
int
sord_num_tuples(Sord read);

/** Return an iterator to the start of the store. */
SORD_API
SordIter
sord_begin(Sord read);

/** Return an iterator that will iterate over each graph URI. */
SORD_API
SordIter
sord_graphs_begin(Sord read);

/** Search for a triple pattern.
 * @return an iterator to the first match, or NULL if no matches found
 */
SORD_API
SordIter
sord_find(Sord sord, const SordTuple pat);


/** @} */
/** @name Write Operations
 * @{
 */

/** Add a tuple to the store. */
SORD_API
void
sord_add(Sord sord, const SordTuple tup);

/** Remove a tuple from the store.
 * This function invalidates all iterators to @a sord (use sord_remove_iter
 * if this is undesirable)
 */
SORD_API
void
sord_remove(Sord sord, const SordTuple tup);

/** Remove a tuple from the store by iterator.
 * @a iter will be incremented to point at the next value.
 */
SORD_API
void
sord_remove_iter(Sord sord, SordIter iter);

/** Remove a graph from the store. */
SORD_API
void
sord_remove_graph(Sord sord, SordID graph);

/** @} */
/** @name Iteration
 * @{
 */

/** Set @a id to the tuple pointed to by @a iter. */
SORD_API
void
sord_iter_get(SordIter iter, SordTuple tup);

/** Return the store pointed to by @a iter. */
SORD_API
Sord
sord_iter_get_sord(SordIter iter);

/** Increment @a iter to point to the next statement. */
SORD_API
bool
sord_iter_next(SordIter iter);

/** Return true iff @a iter is at the end of its range. */
SORD_API
bool
sord_iter_end(SordIter iter);

/** Free @a iter. */
SORD_API
void
sord_iter_free(SordIter iter);

/** @} */
/** @name Utilities
 * @{
 */

/** Match two tuples (using ID comparison only).
 * This function is a straightforward and fast equivalence match with wildcard
 * support (ID 0 is a wildcard).  It never performs any database access.
 * @return true iff @a x and @a y match.
 */
SORD_API
bool
sord_tuple_match(const SordTuple x, const SordTuple y);

/** @} */
/** @name Serialisation
 * @{
 */

SORD_API
bool
sord_read_file(Sord           sord,
               const uint8_t* uri,
               const SordNode graph,
               const uint8_t* blank_prefix);

SORD_API
bool
sord_read_file_handle(Sord           sord,
                      FILE*          fd,
                      const uint8_t* base_uri,
                      const SordNode graph,
                      const uint8_t* blank_prefix);

/** @} */

/** @} */

#endif // SORD_SORD_H
