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
	Sord       sord;
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
	ReadState* const state         = (ReadState*)handle;
	SerdNode         base_uri_node = *uri_node;
	SerdURI          base_uri;
	if (!serd_uri_parse(uri_node->buf, &base_uri)) {
		return false;
	}

	SerdURI abs_base_uri;
	if (!serd_uri_resolve(&base_uri, &state->base_uri, &abs_base_uri)) {
		fprintf(stderr, "error: failed to resolve new base URI\n");
		return false;
	}
	base_uri_node = serd_node_new_uri(&abs_base_uri, &base_uri);

	serd_node_free(&state->base_uri_node);
	state->base_uri_node = base_uri_node;
	state->base_uri      = base_uri;
	return true;
}

static bool
event_prefix(void*           handle,
             const SerdNode* name,
             const SerdNode* uri_node)
{
	ReadState* const state = (ReadState*)handle;
	if (!serd_uri_string_has_scheme(uri_node->buf)) {
		SerdURI uri;
		if (!serd_uri_parse(uri_node->buf, &uri)) {
			return false;
		}
		SerdURI abs_uri;
		if (!serd_uri_resolve(&uri, &state->base_uri, &abs_uri)) {
			return false;
		}
		SerdURI  base_uri;
		SerdNode base_uri_node = serd_node_new_uri(&abs_uri, &base_uri);
		serd_env_add(state->env, name, &base_uri_node);
		serd_node_free(&base_uri_node);
	} else {
		serd_env_add(state->env, name, uri_node);
	}
	return true;
}

static inline SordID
sord_node_from_serd_node(ReadState* state, const SerdNode* sn)
{
	switch (sn->type) {
	case SERD_NOTHING:
		return NULL;
	case SERD_LITERAL:
		return sord_get_literal(state->sord, true, NULL, sn->buf, NULL);
	case SERD_URI: {
		SerdURI uri;
		if (!serd_uri_parse(sn->buf, &uri)) {
			return NULL;
		}
		SerdURI abs_uri;
		if (!serd_uri_resolve(&uri, &state->base_uri, &abs_uri)) {
			return false;
		}
		SerdURI ignored;
		SerdNode abs_uri_node = serd_node_new_uri(&abs_uri, &ignored);
		SordID ret = sord_get_uri(state->sord, true, abs_uri_node.buf);
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
		SordID ret = sord_get_uri_counted(state->sord, true,
		                                  buf, uri_prefix.len + uri_suffix.len);
		free(buf);
		return ret;
	}
	case SERD_BLANK_ID:
	case SERD_ANON_BEGIN:
	case SERD_ANON:
		return sord_get_blank(state->sord, true, sn->buf);
	}
	return NULL;
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

	SordTuple tup;
	tup[0] = sord_node_from_serd_node(state, subject);
	tup[1] = sord_node_from_serd_node(state, predicate);

	SordNode object_node = sord_node_from_serd_node(state, object);

	if (object_datatype) {
		object_node->datatype = sord_node_from_serd_node(state, object_datatype);
	}
	if (object_lang) {
		object_node->lang = g_intern_string((const char*)object_lang->buf);
	}
	tup[2] = object_node;

	if (state->graph_uri_node) {
		assert(graph->type == SERD_NOTHING);
		tup[3] = state->graph_uri_node;
	} else {
		tup[3] = (graph && graph->buf)
			? sord_node_from_serd_node(state, graph)
			: NULL;
	}

	sord_add(state->sord, tup);

	return true;
}

SORD_API
bool
sord_read_file(Sord           sord,
               const uint8_t* input,
               const SordNode graph,
               const uint8_t* blank_prefix)
{
	const uint8_t* filename = NULL;
	if (serd_uri_string_has_scheme(input)) {
		// INPUT is an absolute URI, ensure it a file and chop scheme
		if (strncmp((const char*)input, "file:", 5)) {
			fprintf(stderr, "unsupported URI scheme `%s'\n", input);
			return 1;
		} else if (!strncmp((const char*)input, "file://", 7)) {
			filename = input + 7;
		} else {
			filename = input + 5;
		}
	} else {
		filename = input;
	}

	FILE* in_fd = fopen((const char*)filename,  "r");
	if (!in_fd) {
		fprintf(stderr, "failed to open file %s\n", filename);
		return 1;
	}

	const bool success = sord_read_file_handle(
		sord, in_fd, input, graph, blank_prefix);

	fclose(in_fd);
	return success;
}

SORD_API
bool
sord_read_file_handle(Sord           sord,
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

	ReadState state = { NULL, env, graph, base_uri_node, base_uri, sord };

	state.reader = serd_reader_new(
		SERD_TURTLE, &state,
		event_base, event_prefix, event_statement, NULL);

	if (blank_prefix) {
		serd_reader_set_blank_prefix(state.reader, blank_prefix);
	}

	const bool success = serd_reader_read_file(state.reader, fd, base_uri_str);

	serd_reader_free(state.reader);
	serd_env_free(state.env);
	serd_node_free(&state.base_uri_node);

	return success;
}
