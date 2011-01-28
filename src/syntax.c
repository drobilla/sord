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

#include <stdlib.h>
#include <string.h>

#include "serd/serd.h"

#include "sord-config.h"
#include "sord/sord.h"

typedef struct {
	SerdReader reader;
	SerdEnv    env;
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
	return sord_get_uri_counted(state->sord, true, (const char*)sn->buf, sn->n_chars);
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
	tup[2] = sord_node_from_serd_node(state, object);
	tup[3] = (graph && graph->buf)
		? sord_node_from_serd_node(state, graph)
		: NULL;
	
	sord_add(state->sord, tup);

	return true;
}

SORD_API
bool
sord_read_file(Sord sord, const uint8_t* input)
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

	FILE* in_fd = fopen((const char*)input,  "r");
	if (!in_fd) {
		fprintf(stderr, "failed to open file %s\n", input);
		return 1;
	}

	size_t   base_uri_n_bytes = 0;
	uint8_t* base_uri_str     = copy_string(input, &base_uri_n_bytes);
	SerdURI  base_uri;
	if (!serd_uri_parse(base_uri_str, &base_uri)) {
		fprintf(stderr, "invalid base URI `%s'\n", base_uri_str);
	}

	SerdEnv env = serd_env_new();

	const SerdNode base_uri_node = { SERD_URI,
	                                 base_uri_n_bytes,
	                                 base_uri_n_bytes - 1,  // FIXME: UTF-8
	                                 base_uri_str };

	ReadState state = { NULL, env, base_uri_node, base_uri, sord };

	state.reader = serd_reader_new(
		SERD_TURTLE, &state,
		event_base, event_prefix, event_statement, NULL);

	const bool success = serd_reader_read_file(state.reader, in_fd, input);

	serd_reader_free(state.reader);
	serd_env_free(state.env);
	serd_node_free(&state.base_uri_node);
	fclose(in_fd);

	return success;
}
