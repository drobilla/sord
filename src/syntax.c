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

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include "serd/serd.h"

#include "sord-config.h"
#include "sord_internal.h"

typedef struct {
	SerdReader reader;
	SerdEnv    env;
	SordNode   graph_uri_node;
	SerdNode   base_uri_node;
	SerdURI    base_uri;
	SordWorld  world;
	SordModel  sord;
} ReadState;

static uint8_t*
copy_string(const uint8_t* str, size_t* n_bytes)
{
	const size_t   len = strlen((const char*)str);
	uint8_t* const ret = malloc(len + 1);
	memcpy(ret, str, len + 1);
	*n_bytes = len + 1;
	return ret;
}

static bool
event_base(void*           handle,
           const SerdNode* uri_node)
{
	ReadState* const state = (ReadState*)handle;

	// Resolve base URI and create a new node and URI for it
	SerdURI  base_uri;
	SerdNode base_uri_node = serd_node_new_uri_from_node(
		uri_node, &state->base_uri, &base_uri);

	if (base_uri_node.buf) {
		// Replace the current base URI
		serd_node_free(&state->base_uri_node);
		state->base_uri_node = base_uri_node;
		state->base_uri      = base_uri;
		return true;
	}
	return false;
}

static bool
event_prefix(void*           handle,
             const SerdNode* name,
             const SerdNode* uri_node)
{
	ReadState* const state = (ReadState*)handle;
	if (serd_uri_string_has_scheme(uri_node->buf)) {
		// Set prefix to absolute URI
		serd_env_add(state->env, name, uri_node);
	} else {
		// Resolve relative URI and create a new node and URI for it
		SerdURI  abs_uri;
		SerdNode abs_uri_node = serd_node_new_uri_from_node(
			uri_node, &state->base_uri, &abs_uri);

		if (!abs_uri_node.buf) {
			return false;
		}

		// Set prefix to resolved (absolute) URI
		serd_env_add(state->env, name, &abs_uri_node);
		serd_node_free(&abs_uri_node);
	}
	return true;
}

static inline SordNode
sord_node_from_serd_node(ReadState* state, const SerdNode* sn,
                         const SerdNode* datatype, const SerdNode* lang)
{
	switch (sn->type) {
	case SERD_NOTHING:
		return NULL;
	case SERD_LITERAL:
		return sord_new_literal(
			state->world,
			sord_node_from_serd_node(state, datatype, NULL, NULL),
			sn->buf,
			g_intern_string((const char*)lang->buf));
	case SERD_URI: {
		SerdURI  abs_uri;
		SerdNode abs_uri_node = serd_node_new_uri_from_node(
			sn, &state->base_uri, &abs_uri);
		SordNode ret = sord_new_uri(state->world, abs_uri_node.buf);
		serd_node_free(&abs_uri_node);
		return ret;
	}
	case SERD_CURIE: {
		SerdChunk uri_prefix;
		SerdChunk uri_suffix;
		if (!serd_env_expand(state->env, sn, &uri_prefix, &uri_suffix)) {
			fprintf(stderr, "ERROR: failed to expand qname `%s'\n", sn->buf);
			return NULL;
		}
		const size_t uri_len = uri_prefix.len + uri_suffix.len;
		uint8_t*     buf     = malloc(uri_len + 1);
		memcpy(buf,                  uri_prefix.buf, uri_prefix.len);
		memcpy(buf + uri_prefix.len, uri_suffix.buf, uri_suffix.len);
		buf[uri_len] = '\0';
		SordNode ret = sord_new_uri_counted(state->world,
		                                    buf, uri_prefix.len + uri_suffix.len);
		free(buf);
		return ret;
	}
	case SERD_BLANK_ID:
	case SERD_ANON_BEGIN:
	case SERD_ANON:
		return sord_new_blank(state->world, sn->buf);
	}
	return NULL;
}

static inline void
sord_node_to_serd_node(const SordNode node, SerdNode* out)
{
	if (!node) {
		*out = SERD_NODE_NULL;
		return;
	}
	switch (node->type) {
	case SORD_URI:
		out->type = SERD_URI;
		break;
	case SORD_BLANK:
		out->type = SERD_BLANK_ID;
		break;
	case SORD_LITERAL:
		out->type = SERD_LITERAL;
		break;
	}
	size_t len;
	out->buf = sord_node_get_string_counted(node, &len);
	out->n_bytes = len;
	out->n_chars = len - 1; // FIXME: UTF-8
}

static bool
event_statement(void*           handle,
                const SerdNode* graph,
                const SerdNode* subject,
                const SerdNode* predicate,
                const SerdNode* object,
                const SerdNode* object_datatype,
                const SerdNode* object_lang)
{
	ReadState* const state = (ReadState*)handle;

	SordQuad tup;
	tup[0] = sord_node_from_serd_node(state, subject, NULL, NULL);
	tup[1] = sord_node_from_serd_node(state, predicate, NULL, NULL);
	tup[2] = sord_node_from_serd_node(state, object,
	                                  object_datatype, object_lang);

	if (state->graph_uri_node) {
		assert(graph->type == SERD_NOTHING);
		tup[3] = state->graph_uri_node;
	} else {
		tup[3] = (graph && graph->buf)
			? sord_node_from_serd_node(state, graph, NULL, NULL)
			: NULL;
	}

	sord_add(state->sord, tup);

	return true;
}

static const uint8_t*
sord_file_uri_to_path(const uint8_t* uri)
{
	const uint8_t* filename = NULL;
	if (serd_uri_string_has_scheme(uri)) {
		// Absolute URI, ensure it a file and chop scheme
		if (strncmp((const char*)uri, "file:", 5)) {
			fprintf(stderr, "unsupported URI scheme `%s'\n", uri);
			return NULL;
		} else if (!strncmp((const char*)uri, "file://", 7)) {
			filename = uri + 7;
		} else {
			filename = uri + 5;
		}
	} else {
		filename = uri;
	}
	return filename;
}

SORD_API
bool
sord_read_file(SordModel      model,
               const uint8_t* uri,
               const SordNode graph,
               const uint8_t* blank_prefix)
{
	const uint8_t* const path = sord_file_uri_to_path(uri);
	if (!path) {
		return false;
	}

	FILE* const fd = fopen((const char*)path,  "r");
	if (!fd) {
		fprintf(stderr, "failed to open file %s\n", path);
		return false;
	}

	const bool ret = sord_read_file_handle(model, fd, uri, graph, blank_prefix);
	fclose(fd);
	return ret;
}

SORD_API
bool
sord_read_file_handle(SordModel      sord,
                      FILE*          fd,
                      const uint8_t* base_uri_str_in,
                      const SordNode graph,
                      const uint8_t* blank_prefix)
{
	size_t   base_uri_n_bytes = 0;
	uint8_t* base_uri_str     = copy_string(base_uri_str_in, &base_uri_n_bytes);
	SerdURI  base_uri;
	if (!serd_uri_parse(base_uri_str, &base_uri)) {
		fprintf(stderr, "invalid base URI `%s'\n", base_uri_str);
	}

	SerdEnv env = serd_env_new();

	const SerdNode base_uri_node = { SERD_URI,
	                                 base_uri_n_bytes,
	                                 base_uri_n_bytes - 1,  // FIXME: UTF-8
	                                 base_uri_str };

	ReadState state = { NULL, env, graph,
	                    base_uri_node, base_uri,
	                    sord_get_world(sord), sord };

	state.reader = serd_reader_new(
		SERD_TURTLE, &state,
		event_base, event_prefix, event_statement, NULL);

	if (blank_prefix) {
		serd_reader_set_blank_prefix(state.reader, blank_prefix);
	}

	const bool success = serd_reader_read_file(state.reader, fd, base_uri_str);

	serd_reader_free(state.reader);
	serd_node_free(&state.base_uri_node);

	return success;
}

SORD_API
bool
sord_write_file(SordModel      model,
                SerdEnv        env,
                const uint8_t* uri,
                const SordNode graph,
                const uint8_t* blank_prefix)
{
	const uint8_t* const path = sord_file_uri_to_path(uri);
	if (!path) {
		return false;
	}

	FILE* const fd = fopen((const char*)path,  "w");
	if (!fd) {
		fprintf(stderr, "failed to open file %s\n", path);
		return false;
	}

	const bool ret = sord_write_file_handle(model, env, fd, uri, graph, blank_prefix);
	fclose(fd);
	return ret;
}

static size_t
file_sink(const void* buf, size_t len, void* stream)
{
	FILE* file = (FILE*)stream;
	return fwrite(buf, 1, len, file);
}

SORD_API
bool
sord_write_file_handle(SordModel      model,
                       SerdEnv        env,
                       FILE*          fd,
                       const uint8_t* base_uri_str_in,
                       const SordNode graph,
                       const uint8_t* blank_prefix)
{
	size_t   base_uri_n_bytes = 0;
	uint8_t* base_uri_str     = copy_string(base_uri_str_in, &base_uri_n_bytes);
	SerdURI  base_uri;
	if (!serd_uri_parse(base_uri_str, &base_uri)) {
		fprintf(stderr, "invalid base URI `%s'\n", base_uri_str);
	}

	SerdWriter writer = serd_writer_new(SERD_TURTLE,
	                                    SERD_STYLE_ABBREVIATED|SERD_STYLE_CURIED,
	                                    env,
	                                    &base_uri,
	                                    file_sink,
	                                    fd);

	serd_env_foreach(env,
	                 (SerdPrefixSink)serd_writer_set_prefix,
	                 writer);

	SerdNode s_graph;
	sord_node_to_serd_node(graph, &s_graph);
	for (SordIter i = sord_begin(model); !sord_iter_end(i); sord_iter_next(i)) {
		SordQuad quad;
		sord_iter_get(i, quad);

		SerdNode subject;
		SerdNode predicate;
		SerdNode object;
		SerdNode datatype;
		sord_node_to_serd_node(quad[SORD_SUBJECT],   &subject);
		sord_node_to_serd_node(quad[SORD_PREDICATE], &predicate);
		sord_node_to_serd_node(quad[SORD_OBJECT],    &object);
		
		sord_node_to_serd_node(sord_node_get_datatype(quad[SORD_OBJECT]), &datatype);
		const char* lang_str = sord_node_get_language(quad[SORD_OBJECT]);
		size_t      lang_len = lang_str ? strlen(lang_str) : 0;
		
		SerdNode language = SERD_NODE_NULL;
		if (lang_str) {
			language.type    = SERD_LITERAL;
			language.n_bytes = lang_len + 1;
			language.n_chars = lang_len; // FIXME: UTF-8
			language.buf     = (const uint8_t*)lang_str;
		};
			    
		serd_writer_write_statement(writer, &s_graph,
		                            &subject, &predicate, &object,
		                            &datatype, &language);
	}

	serd_writer_free(writer);
	free(base_uri_str);

	return true;
}
