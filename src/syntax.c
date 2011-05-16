/*
  Copyright 2011 David Robillard <http://drobilla.net>

  Permission to use, copy, modify, and/or distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THIS SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include "serd/serd.h"

#include "sord-config.h"
#include "sord_internal.h"

typedef struct {
	SerdReader* reader;
	SerdEnv*    env;
	SordNode*   graph_uri_node;
	SordWorld*  world;
	SordModel*  sord;
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

static SerdStatus
event_base(void*           handle,
           const SerdNode* uri_node)
{
	ReadState* const state = (ReadState*)handle;

	return serd_env_set_base_uri(state->env, uri_node);
}

static SerdStatus
event_prefix(void*           handle,
             const SerdNode* name,
             const SerdNode* uri_node)
{
	ReadState* const state = (ReadState*)handle;

	return serd_env_set_prefix(state->env, name, uri_node);
}

static SerdStatus
event_statement(void*           handle,
                const SerdNode* graph,
                const SerdNode* subject,
                const SerdNode* predicate,
                const SerdNode* object,
                const SerdNode* object_datatype,
                const SerdNode* object_lang)
{
	ReadState* const state = (ReadState*)handle;

	SordNode* s = sord_node_from_serd_node(state->world, state->env,
	                                       subject, NULL, NULL);
	SordNode* p = sord_node_from_serd_node(state->world, state->env,
	                                       predicate, NULL, NULL);
	SordNode* o = sord_node_from_serd_node(state->world, state->env,
	                                       object, object_datatype, object_lang);

	SordNode* g = NULL;
	if (state->graph_uri_node) {
		assert(graph->type == SERD_NOTHING);
		g = sord_node_copy(state->graph_uri_node);
	} else {
		g = (graph && graph->buf)
			? sord_node_from_serd_node(state->world, state->env,
			                           graph, NULL, NULL)
			: NULL;
	}

	const SordQuad tup = { s, p, o, g };
	sord_add(state->sord, tup);

	sord_node_free(state->world, s);
	sord_node_free(state->world, p);
	sord_node_free(state->world, o);
	sord_node_free(state->world, g);

	return SERD_SUCCESS;
}

static const uint8_t*
sord_file_uri_to_path(const uint8_t* uri)
{
	const uint8_t* filename = NULL;
	if (serd_uri_string_has_scheme(uri)) {
		// Absolute URI, ensure it a file and chop scheme
		if (strncmp((const char*)uri, "file:", 5)) {
			fprintf(stderr, "Unsupported URI scheme `%s'\n", uri);
			return NULL;
#ifdef __WIN32__
		} else if (!strncmp((const char*)uri, "file:///", 8)) {
			filename = uri + 8;
#else
		} else if (!strncmp((const char*)uri, "file://", 7)) {
			filename = uri + 7;
#endif
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
sord_read_file(SordModel*     model,
               SerdEnv*       env,
               const uint8_t* uri,
               const uint8_t* base_uri,
               SordNode*      graph,
               const uint8_t* blank_prefix)
{
	if (!base_uri) {
		base_uri = uri;
	}

	const uint8_t* const path = sord_file_uri_to_path(uri);
	if (!path) {
		fprintf(stderr, "Unable to read non-file URI <%s>\n", uri);
		return false;
	}

	FILE* const fd = fopen((const char*)path, "r");
	if (!fd) {
		fprintf(stderr, "Failed to open file %s\n", path);
		return false;
	}

	const bool ret = sord_read_file_handle(
		model, env, fd, path, base_uri, graph, blank_prefix);
	fclose(fd);
	return ret;
}

SORD_API
bool
sord_read_file_handle(SordModel*     model,
                      SerdEnv*       env,
                      FILE*          fd,
                      const uint8_t* name,
                      const uint8_t* base_uri_str_in,
                      SordNode*      graph,
                      const uint8_t* blank_prefix)
{
	size_t   base_uri_n_bytes = 0;
	uint8_t* base_uri_str     = copy_string(base_uri_str_in, &base_uri_n_bytes);

	SerdURI  base_uri;
	if (serd_uri_parse(base_uri_str, &base_uri)) {
		fprintf(stderr, "Invalid base URI <%s>\n", base_uri_str);
	}

	SerdNode base_uri_node = serd_node_from_string(SERD_URI, base_uri_str);
	serd_env_set_base_uri(env, &base_uri_node);

	ReadState state = { NULL, env, graph,
	                    sord_get_world(model), model };

	state.reader = serd_reader_new(
		SERD_TURTLE, &state,
		event_base, event_prefix, event_statement, NULL);

	if (blank_prefix) {
		serd_reader_set_blank_prefix(state.reader, blank_prefix);
	}

	const SerdStatus ret = serd_reader_read_file(state.reader, fd, name);

	serd_reader_free(state.reader);
	free(base_uri_str);

	return (ret == SERD_SUCCESS);
}

SORD_API
bool
sord_read_string(SordModel*     model,
                 SerdEnv*       env,
                 const uint8_t* str,
                 const uint8_t* base_uri_str_in)
{
	size_t   base_uri_n_bytes = 0;
	uint8_t* base_uri_str     = copy_string(base_uri_str_in, &base_uri_n_bytes);

	SerdURI  base_uri;
	if (serd_uri_parse(base_uri_str, &base_uri)) {
		fprintf(stderr, "Invalid base URI <%s>\n", base_uri_str);
	}

	SerdNode base_uri_node = serd_node_from_string(SERD_URI, base_uri_str);
	serd_env_set_base_uri(env, &base_uri_node);

	ReadState state = { NULL, env, NULL,
	                    sord_get_world(model), model };

	state.reader = serd_reader_new(
		SERD_TURTLE, &state,
		event_base, event_prefix, event_statement, NULL);

	const SerdStatus status = serd_reader_read_string(state.reader, str);

	serd_reader_free(state.reader);
	free(base_uri_str);

	return (status == SERD_SUCCESS);
}

SORD_API
bool
sord_write_file(SordModel*     model,
                SerdEnv*       env,
                const uint8_t* uri,
                SordNode*      graph,
                const uint8_t* blank_prefix)
{
	const uint8_t* const path = sord_file_uri_to_path(uri);
	if (!path) {
		return false;
	}

	FILE* const fd = fopen((const char*)path, "w");
	if (!fd) {
		fprintf(stderr, "Failed to open file %s\n", path);
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

static void
write_statement(SordModel* sord, SerdWriter* writer, SordQuad tup,
                const SordNode* anon_subject)
{
	const SordNode* s  = tup[SORD_SUBJECT];
	const SordNode* p  = tup[SORD_PREDICATE];
	const SordNode* o  = tup[SORD_OBJECT];
	const SordNode* d  = sord_node_get_datatype(o);
	const SerdNode* ss = sord_node_to_serd_node(s);
	const SerdNode* sp = sord_node_to_serd_node(p);
	const SerdNode* so = sord_node_to_serd_node(o);
	const SerdNode* sd = sord_node_to_serd_node(d);

	const char* lang_str = sord_node_get_language(o);
	size_t      lang_len = lang_str ? strlen(lang_str) : 0;
	SerdNode    language = SERD_NODE_NULL;
	if (lang_str) {
		language.type    = SERD_LITERAL;
		language.n_bytes = lang_len;
		language.n_chars = lang_len;
		language.buf     = (const uint8_t*)lang_str;
	};

	SerdNode subject = *ss;
	if (anon_subject) {
		assert(s == anon_subject);
		subject.type = SERD_ANON;
	} else if (sord_node_is_inline_object(s)) {
		return;
	}

	if (sord_node_is_inline_object(o)) {
		SerdNode anon = *so;
		anon.type = SERD_ANON_BEGIN;
		serd_writer_write_statement(
			writer, NULL, &subject, sp, &anon, sd, &language);
		SordQuad  sub_pat  = { o, 0, 0, 0 };
		SordIter* sub_iter = sord_find(sord, sub_pat);
		for (; !sord_iter_end(sub_iter); sord_iter_next(sub_iter)) {
			SordQuad sub_tup;
			sord_iter_get(sub_iter, sub_tup);
			write_statement(sord, writer, sub_tup, o);
		}
		sord_iter_free(sub_iter);
		serd_writer_end_anon(writer, so);
	} else if (!sord_node_is_inline_object(s) || s == anon_subject) {
		serd_writer_write_statement(
			writer, NULL, &subject, sp, so, sd, &language);
	}
}

bool
sord_write_writer(SordModel*  model,
                  SerdWriter* writer,
                  SordNode*   graph)
{
	SordQuad  pat  = { 0, 0, 0, graph };
	SordIter* iter = sord_find(model, pat);
	for (; !sord_iter_end(iter); sord_iter_next(iter)) {
		SordQuad tup;
		sord_iter_get(iter, tup);
		write_statement(model, writer, tup, NULL);
	}
	sord_iter_free(iter);
	return true;
}

static SerdWriter*
make_writer(SerdEnv*       env,
            const uint8_t* base_uri_str_in,
            SerdSink       sink,
            void*          stream)
{
	size_t   base_uri_n_bytes = 0;
	uint8_t* base_uri_str     = copy_string(base_uri_str_in, &base_uri_n_bytes);
	SerdURI  base_uri;
	if (serd_uri_parse(base_uri_str, &base_uri)) {
		fprintf(stderr, "Invalid base URI <%s>\n", base_uri_str);
	}

	SerdWriter* writer = serd_writer_new(SERD_TURTLE,
	                                     SERD_STYLE_ABBREVIATED|SERD_STYLE_CURIED,
	                                     env,
	                                     &base_uri,
	                                     sink,
	                                     stream);

	serd_env_foreach(env,
	                 (SerdPrefixSink)serd_writer_set_prefix,
	                 writer);

	return writer;
}

SORD_API
bool
sord_write_file_handle(SordModel*     model,
                       SerdEnv*       env,
                       FILE*          fd,
                       const uint8_t* base_uri_str_in,
                       SordNode*      graph,
                       const uint8_t* blank_prefix)
{
	SerdWriter* writer = make_writer(env, base_uri_str_in, file_sink, fd);
	sord_write_writer(model, writer, graph);
	serd_writer_free(writer);
	return true;
}

struct SerdBuffer {
	uint8_t* buf;
	size_t   len;
};

static size_t
string_sink(const void* buf, size_t len, void* stream)
{
	struct SerdBuffer* out = (struct SerdBuffer*)stream;
	out->buf = realloc(out->buf, out->len + len);
	memcpy(out->buf + out->len, buf, len);
	out->len += len;
	return len;
}

SORD_API
uint8_t*
sord_write_string(SordModel*     model,
                  SerdEnv*       env,
                  const uint8_t* base_uri)
{
	struct SerdBuffer buf = { NULL, 0 };
	SerdWriter* writer = make_writer(env, base_uri, string_sink, &buf);
	sord_write_writer(model, writer, NULL);
	serd_writer_free(writer);
	string_sink("", 1, &buf);
	return buf.buf;
}
