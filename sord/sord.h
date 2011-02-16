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

/** @file sord.h
 * Public Sord API.
 */

#ifndef SORD_SORD_H
#define SORD_SORD_H

#include <stdbool.h>
#include <stdint.h>

#if defined _WIN32 || defined __CYGWIN__
	#define SORD_LIB_IMPORT __declspec(dllimport)
	#define SORD_LIB_EXPORT __declspec(dllexport)
#else
	#define SORD_LIB_IMPORT __attribute__ ((visibility("default")))
	#define SORD_LIB_EXPORT __attribute__ ((visibility("default")))
#endif

#ifdef SORD_SHARED  // Building a shared library
	#ifdef SORD_INTERNAL  // Building SORD (not using it)
		#define SORD_API SORD_LIB_EXPORT
	#else
		#define SORD_API SORD_LIB_IMPORT
	#endif
#else  // Building a static library
	#define SORD_API
#endif

/** @defgroup sord Sord
 * A lightweight RDF model library.
 * Sord stores RDF (subject object predicate) triples, where triples may
 * have an added "context" field, i.e. (subject object predicate context).
 * @{
 */

typedef struct _Sord*     Sord;      ///< Quad store
typedef struct _SordIter* SordIter;  ///< Store iterator
typedef struct _SordNode* SordNode;  ///< Node

/** Quad of IDs (statement), or a quad pattern.
 * Nodes are ordered (S P O G).  The ID of the default graph is 0.
 */
typedef SordNode SordQuad[4];

/** Index into a SordQuad. */
typedef enum {
	SORD_SUBJECT   = 0,  ///< Subject
	SORD_PREDICATE = 1,  ///< Predicate (a.k.a. "key")
	SORD_OBJECT    = 2,  ///< Object    (a.k.a. "value")
	SORD_GRAPH     = 3   ///< Graph     (a.k.a. "context")
} SordQuadIndex;

/** Type of a node. */
typedef enum {
	SORD_URI     = 1,  ///< URI
	SORD_BLANK   = 2,  ///< Blank node identifier
	SORD_LITERAL = 3   ///< Literal (string with optional lang and/or type)
} SordNodeType;

/** Indexing option. */
typedef enum {
	SORD_SPO = 1,       ///< Subject,   Predicate, Object
	SORD_SOP = 1 << 1,  ///< Subject,   Object,    Predicate
	SORD_OPS = 1 << 2,  ///< Object,    Predicate, Subject
	SORD_OSP = 1 << 3,  ///< Object,    Subject,   Predicate
	SORD_PSO = 1 << 4,  ///< Predicate, Subject,   Object
	SORD_POS = 1 << 5   ///< Predicate, Object,    Subject
} SordIndexOption;

/** @name Initialisation and Cleanup
 * @{
 */

/** Create a new store.
 * @param indices SordIndexOption flags (e.g. SORD_SPO|SORD_OPS).
 *        Be sure to choose indices such that there is an index where the most
 *        significant node(s) are not variables for your queries.  For example,
 *        if you are going to make (? P O) queries, you should enable either
 *        SORD_OPS or SORD_POS.
 * @param graphs If true, store (and index) graph contexts.
 */
SORD_API
Sord
sord_new(unsigned indices, bool graphs);

/** Close and free @a sord. */
SORD_API
void
sord_free(Sord sord);

/** @} */
/** @name Nodes
 * A Node is a component of a Quad.  Nodes may be URIs, blank nodes, or
 * (in the case of quad objects only) string literals.  Literal nodes may
 * have an associate language or datatype (but not both).
 * @{
 */

/** Find a URI, creating a new one if necessary iff @a create is true.
 * Use sord_get_uri_counted instead if the length of @a str is known.
 */
SORD_API
SordNode
sord_new_uri(Sord sord, const uint8_t* str);

/** Find a URI, creating a new one if necessary iff @a create is true. */
SORD_API
SordNode
sord_new_uri_counted(Sord sord, const uint8_t* str, int str_len);

/** Find a blank, creating a new one if necessary iff @a create is true
 * Use sord_get_blank_counted instead if the length of @a str is known.
 */
SORD_API
SordNode
sord_new_blank(Sord sord, const uint8_t* str);

/** Find a blank, creating a new one if necessary iff @a create is true. */
SORD_API
SordNode
sord_new_blank_counted(Sord sord, const uint8_t* str, int str_len);

/** Find a literal, creating a new one if necessary iff @a create is true.
 * Use sord_get_literal_counted instead if the length of @a str is known.
 */
SORD_API
SordNode
sord_new_literal(Sord sord, SordNode datatype,
                 const uint8_t* str, const char* lang);

/** Find a literal, creating a new one if necessary iff @a create is true. */
SORD_API
SordNode
sord_new_literal_counted(Sord sord, SordNode datatype,
                         const uint8_t* str,  int     str_len,
                         const char*    lang, uint8_t lang_len);

/** Return the type of a node (SORD_URI, SORD_BLANK, or SORD_LITERAL). */
SORD_API
SordNodeType
sord_node_get_type(SordNode node);

/** Return the string value of a node. */
SORD_API
const uint8_t*
sord_node_get_string(SordNode node);

/** Return the string value of a node, and set @a len to its length. */
SORD_API
const uint8_t*
sord_node_get_string_counted(SordNode node, size_t* len);

/** Return the language of a literal node (or NULL). */
SORD_API
const char*
sord_node_get_language(SordNode node);

/** Return the datatype URI of a literal node (or NULL). */
SORD_API
SordNode
sord_node_get_datatype(SordNode node);

/** Return true iff @a a is equal to @a b.
 * Note this is much faster than comparing the node's strings.
 */
SORD_API
bool
sord_node_equals(const SordNode a, const SordNode b);

/** @} */
/** @name Read Operations
 * @{
 */

/** Return the number of nodes stored in @a sord.
 * Nodes are included in this count iff they are a part of a quad in @a sord.
 */
SORD_API
int
sord_num_nodes(Sord read);

/** Return the number of quads stored in @a sord. */
SORD_API
int
sord_num_quads(Sord read);

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
sord_find(Sord sord, const SordQuad pat);


/** @} */
/** @name Write Operations
 * @{
 */

/** Add a quad to the store. */
SORD_API
void
sord_add(Sord sord, const SordQuad tup);

/** Remove a quad from the store.
 * This function invalidates all iterators to @a sord (use sord_remove_iter
 * if this is undesirable)
 */
SORD_API
void
sord_remove(Sord sord, const SordQuad tup);

/** Remove a quad from the store by iterator.
 * @a iter will be incremented to point at the next value.
 */
SORD_API
void
sord_remove_iter(Sord sord, SordIter iter);

/** Remove a graph from the store. */
SORD_API
void
sord_remove_graph(Sord sord, SordNode graph);

/** @} */
/** @name Iteration
 * @{
 */

/** Set @a id to the quad pointed to by @a iter. */
SORD_API
void
sord_iter_get(SordIter iter, SordQuad tup);

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

/** Match two quads (using ID comparison only).
 * This function is a straightforward and fast equivalence match with wildcard
 * support (ID 0 is a wildcard).  It never performs any database access.
 * @return true iff @a x and @a y match.
 */
SORD_API
bool
sord_quad_match(const SordQuad x, const SordQuad y);

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
