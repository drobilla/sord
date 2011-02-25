/*
  Copyright 2011 David Robillard <http://drobilla.net>

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in the
     documentation and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
  INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
  AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
  AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
  OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
  THE POSSIBILITY OF SUCH DAMAGE.
*/

/**
   @file sord.h API for Sord, a lightweight RDF model library.
*/

#ifndef SORD_SORD_H
#define SORD_SORD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "serd/serd.h"

#ifdef SORD_SHARED
	#if defined _WIN32 || defined __CYGWIN__
		#define SORD_LIB_IMPORT __declspec(dllimport)
		#define SORD_LIB_EXPORT __declspec(dllexport)
	#else
		#define SORD_LIB_IMPORT __attribute__ ((visibility("default")))
		#define SORD_LIB_EXPORT __attribute__ ((visibility("default")))
	#endif
	#ifdef SORD_INTERNAL
		#define SORD_API SORD_LIB_EXPORT
	#else
		#define SORD_API SORD_LIB_IMPORT
	#endif
#else
	#define SORD_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
   @defgroup sord Sord
   A lightweight RDF model library.

   Sord stores RDF (subject object predicate context) quads, where the context
   may be omitted (to represent triples in the default graph).
   @{
*/

typedef struct _SordWorld* SordWorld;  ///< Sord world (library state)
typedef struct _SordModel* SordModel;  ///< Quad store
typedef struct _SordIter*  SordIter;   ///< Store iterator
typedef struct _SordNode*  SordNode;   ///< Node

/**
   Quad of IDs (statement), or a quad pattern.

   Nodes are ordered (S P O G).  The ID of the default graph is 0.
*/
typedef SordNode SordQuad[4];

/**
   Index into a SordQuad.
*/
typedef enum {
	SORD_SUBJECT   = 0,  ///< Subject
	SORD_PREDICATE = 1,  ///< Predicate (a.k.a. "key")
	SORD_OBJECT    = 2,  ///< Object    (a.k.a. "value")
	SORD_GRAPH     = 3   ///< Graph     (a.k.a. "context")
} SordQuadIndex;

/**
   Type of a node.
*/
typedef enum {
	SORD_URI     = 1,  ///< URI
	SORD_BLANK   = 2,  ///< Blank node identifier
	SORD_LITERAL = 3   ///< Literal (string with optional lang or datatype)
} SordNodeType;

/**
   Indexing option.
*/
typedef enum {
	SORD_SPO = 1,       ///< Subject,   Predicate, Object
	SORD_SOP = 1 << 1,  ///< Subject,   Object,    Predicate
	SORD_OPS = 1 << 2,  ///< Object,    Predicate, Subject
	SORD_OSP = 1 << 3,  ///< Object,    Subject,   Predicate
	SORD_PSO = 1 << 4,  ///< Predicate, Subject,   Object
	SORD_POS = 1 << 5   ///< Predicate, Object,    Subject
} SordIndexOption;

/**
   @name World
   @{
*/

SORD_API
SordWorld
sord_world_new(void);

SORD_API
void
sord_world_free(SordWorld world);

/**
   @}
   @name Nodes
   A Node is a component of a Quad.  Nodes may be URIs, blank nodes, or
   (in the case of quad objects only) string literals.  Literal nodes may
   have an associate language or datatype (but not both).
   @{
*/

/**
   Find a URI, creating a new one if necessary iff @a create is true.

   Use sord_get_uri_counted instead if the length of @a str is known.
*/
SORD_API
SordNode
sord_new_uri(SordWorld world, const uint8_t* str);

/**
   Find a URI, creating a new one if necessary iff @a create is true.
*/
SORD_API
SordNode
sord_new_uri_counted(SordWorld      world,
                     const uint8_t* str,
                     size_t         str_len);

/**
   Find a blank, creating a new one if necessary iff @a create is true.

   Use sord_get_blank_counted instead if the length of @a str is known.
*/
SORD_API
SordNode
sord_new_blank(SordWorld world, const uint8_t* str);

/**
   Find a blank, creating a new one if necessary iff @a create is true.
*/
SORD_API
SordNode
sord_new_blank_counted(SordWorld      world,
                       const uint8_t* str,
                       size_t         str_len);

/**
   Find a literal, creating a new one if necessary iff @a create is true.

   Use sord_get_literal_counted instead if the length of @a str is known.
*/
SORD_API
SordNode
sord_new_literal(SordWorld      world,
                 SordNode       datatype,
                 const uint8_t* str,
                 const char*    lang);

/**
   Find a literal, creating a new one if necessary iff @a create is true.
*/
SORD_API
SordNode
sord_new_literal_counted(SordWorld      world,
                         SordNode       datatype,
                         const uint8_t* str,
                         size_t         str_len,
                         const char*    lang,
                         uint8_t        lang_len);

/**
   Copy a node.
*/
SORD_API
SordNode
sord_node_copy(SordNode node);

/**
   Free a node.
*/
SORD_API
void
sord_node_free(SordNode node);

/**
   Return the type of a node (SORD_URI, SORD_BLANK, or SORD_LITERAL).
*/
SORD_API
SordNodeType
sord_node_get_type(SordNode node);

/**
   Return the string value of a node.
*/
SORD_API
const uint8_t*
sord_node_get_string(SordNode node);

/**
   Return the string value of a node, and set @a len to its length.
*/
SORD_API
const uint8_t*
sord_node_get_string_counted(SordNode node, size_t* len);

/**
   Return the language of a literal node (or NULL).
*/
SORD_API
const char*
sord_node_get_language(SordNode node);

/**
   Return the datatype URI of a literal node (or NULL).
*/
SORD_API
SordNode
sord_node_get_datatype(SordNode node);

/**
   Return true iff @a a is equal to @a b.

   Note this is much faster than comparing the node's strings.
*/
SORD_API
bool
sord_node_equals(const SordNode a,
                 const SordNode b);

/** @} */
/** @name Model
 * @{
 */

/**
   Create a new store.

   @param world The world in which to make this store.

   @param indices SordIndexOption flags (e.g. SORD_SPO|SORD_OPS).  Be sure to
   enable an index where the most significant node(s) are not variables in your
   queries (e.g. to make (? P O) queries, enable either SORD_OPS or SORD_POS).

   @param graphs If true, store (and index) graph contexts.
*/
SORD_API
SordModel
sord_new(SordWorld world,
         unsigned  indices,
         bool      graphs);

/**
   Close and free @a sord.
*/
SORD_API
void
sord_free(SordModel model);

/**
   Get the world associated with @a model.
*/
SORD_API
SordWorld
sord_get_world(SordModel model);

/**
   Return the number of nodes stored in @a sord.

   Nodes are included in this count iff they are a part of a quad in @a sord.
*/
SORD_API
int
sord_num_nodes(SordWorld world);

/**
   Return the number of quads stored in @a sord.
*/
SORD_API
int
sord_num_quads(SordModel model);

/**
   Return an iterator to the start of the store.
*/
SORD_API
SordIter
sord_begin(SordModel model);

/**
   Return an iterator that will iterate over each graph URI.
*/
SORD_API
SordIter
sord_graphs_begin(SordModel model);

/**
   Search for a triple pattern.
   @return an iterator to the first match, or NULL if no matches found.
*/
SORD_API
SordIter
sord_find(SordModel model, const SordQuad pat);

/**
   Add a quad to the store.
*/
SORD_API
void
sord_add(SordModel model, const SordQuad quad);

/**
   Remove a quad from the store.

   This function invalidates all iterators to @a sord (use sord_remove_iter
   if this is undesirable)
*/
SORD_API
void
sord_remove(SordModel model, const SordQuad quad);

/**
   Remove a quad from the store by iterator.

   @a iter will be incremented to point at the next value.
*/
SORD_API
void
sord_remove_iter(SordModel model, SordIter iter);

/**
   Remove a graph from the store.
*/
SORD_API
void
sord_remove_graph(SordModel model, SordNode graph);

/**
   @}
   @name Iteration
   @{
*/

/**
   Set @a id to the quad pointed to by @a iter.
*/
SORD_API
void
sord_iter_get(SordIter iter, SordQuad quad);

/**
   Return the store pointed to by @a iter.
*/
SORD_API
SordModel
sord_iter_get_model(SordIter iter);

/**
   Increment @a iter to point to the next statement.
*/
SORD_API
bool
sord_iter_next(SordIter iter);

/**
   Return true iff @a iter is at the end of its range.
*/
SORD_API
bool
sord_iter_end(SordIter iter);

/**
   Free @a iter.
*/
SORD_API
void
sord_iter_free(SordIter iter);

/**
   @}
   @name Utilities
   @{
*/

/**
   Match two quads (using ID comparison only).

   This function is a straightforward and fast equivalence match with wildcard
   support (ID 0 is a wildcard). It does not actually read node data.
   @return true iff @a x and @a y match.
*/
SORD_API
bool
sord_quad_match(const SordQuad x, const SordQuad y);

/**
   @}
   @name Serialisation
   @{
*/

SORD_API
bool
sord_read_file(SordModel      model,
               const uint8_t* uri,
               const SordNode graph,
               const uint8_t* blank_prefix);

SORD_API
bool
sord_read_file_handle(SordModel      model,
                      FILE*          fd,
                      const uint8_t* base_uri,
                      const SordNode graph,
                      const uint8_t* blank_prefix);

SORD_API
bool
sord_read_string(SordModel      model,
                 const uint8_t* str,
                 const uint8_t* base_uri);

SORD_API
bool
sord_write_file(SordModel      model,
                SerdEnv        env,
                const uint8_t* uri,
                const SordNode graph,
                const uint8_t* blank_prefix);

SORD_API
bool
sord_write_file_handle(SordModel      model,
                       SerdEnv        env,
                       FILE*          fd,
                       const uint8_t* base_uri,
                       const SordNode graph,
                       const uint8_t* blank_prefix);

SORD_API
uint8_t*
sord_write_string(SordModel      model,
                  SerdEnv        env,
                  const uint8_t* base_uri);


/**
   @}
   @}
*/

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /** SORD_SORD_H */
