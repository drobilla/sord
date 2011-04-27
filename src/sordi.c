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

#include "serd/serd.h"
#include "sord/sord.h"
#include "sord-config.h"

typedef struct {
	SerdWriter* writer;
	SerdEnv*    env;
	SerdNode    base_uri_node;
	SerdURI     base_uri;
	SordModel   sord;
} State;

int
print_version()
{
	printf("sordi " SORD_VERSION " <http://drobilla.net/software/sord>\n");
	printf("Copyright 2011-2011 David Robillard <http://drobilla.net>.\n"
	       "\nLicense: Simplified BSD License.\n"
	       "This is free software; you are free to change and redistribute it."
	       "\nThere is NO WARRANTY, to the extent permitted by law.\n");
	return 0;
}

int
print_usage(const char* name, bool error)
{
	FILE* const os = error ? stderr : stdout;
	fprintf(os, "Usage: %s [OPTION]... INPUT [BASE_URI]\n", name);
	fprintf(os, "Load and re-serialise RDF syntax.\n\n");
	fprintf(os, "  -h           Display this help and exit\n");
	fprintf(os, "  -o SYNTAX    Output syntax (`turtle' or `ntriples')\n");
	fprintf(os, "  -v           Display version information and exit\n");
	return error ? 1 : 0;
}

static size_t
file_sink(const void* buf, size_t len, void* stream)
{
	FILE* file = (FILE*)stream;
	return fwrite(buf, 1, len, file);
}

static inline SerdNode
serd_node_from_sord_node(const SordNode n)
{
	size_t         n_bytes = 0;
	const uint8_t* buf     = sord_node_get_string_counted(n, &n_bytes);
	SerdNode       sn      = { (const uint8_t*)buf, n_bytes, n_bytes - 1, SERD_NOTHING };
	// FIXME: UTF-8
	switch (sord_node_get_type(n)) {
	case SORD_URI:
		sn.type = SERD_URI;
		break;
	case SORD_BLANK:
		sn.type = SERD_BLANK_ID;
		break;
	case SORD_LITERAL:
		sn.type = SERD_LITERAL;
		break;
	}
	return sn;
}

int
main(int argc, char** argv)
{
	if (argc < 2) {
		return print_usage(argv[0], true);
	}

	SerdSyntax output_syntax = SERD_NTRIPLES;
	int a = 1;
	for (; a < argc && argv[a][0] == '-'; ++a) {
		if (argv[a][1] == 'h') {
			return print_usage(argv[0], false);
		} else if (argv[a][1] == 'v') {
			return print_version();
		} else if (argv[a][1] == 'o') {
			if (++a == argc) {
				fprintf(stderr, "missing value for -o\n");
				return 1;
			}
			if (!strcmp(argv[a], "turtle")) {
				output_syntax = SERD_TURTLE;
			} else if (!strcmp(argv[a], "ntriples")) {
				output_syntax = SERD_NTRIPLES;
			} else {
				fprintf(stderr, "unknown output format `%s'\n",  argv[a]);
				return 1;
			}
		} else {
			fprintf(stderr, "unknown option `%s'\n", argv[a]);
			return print_usage(argv[0], true);
		}
	}

	const uint8_t* input = (const uint8_t*)argv[a++];

	SordWorld world = sord_world_new();
	SordModel sord = sord_new(world, SORD_SPO|SORD_OPS, false);

	bool success = sord_read_file(sord, input, NULL, NULL);

	printf("loaded %u statements\n", sord_num_nodes(world));

	SerdURI base_uri;
	if (!serd_uri_parse(input, &base_uri)) {
		fprintf(stderr, "bad input URI `%s'\n", input);
		return 1;
	}

	SerdEnv*    env    = serd_env_new();
	SerdWriter* writer = serd_writer_new(SERD_TURTLE, SERD_STYLE_ABBREVIATED,
	                                     env, &base_uri, file_sink, stdout);

	// Query
	SordQuad pat = { 0, 0, 0, 0 };
	SordIter iter = sord_find(sord, pat);
	for (; !sord_iter_end(iter); sord_iter_next(iter)) {
		SordQuad tup;
		sord_iter_get(iter, tup);
		SordNode s = tup[SORD_SUBJECT];
		SordNode p = tup[SORD_PREDICATE];
		SordNode o = tup[SORD_OBJECT];
		SerdNode ss = serd_node_from_sord_node(s);
		SerdNode sp = serd_node_from_sord_node(p);
		SerdNode so = serd_node_from_sord_node(o);
		serd_writer_write_statement(
			writer, NULL, &ss, &sp, &so, NULL, NULL);
	}

	serd_writer_finish(writer);
	serd_writer_free(writer);

	serd_env_free(env);

	sord_free(sord);

	return success ? 0 : 1;
}
