/* Sord, a lightweight RDF syntax library.
 * Copyright 2011 David Robillard <d@drobilla.net>
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

#include "serd/serd.h"
#include "sord/sord.h"
#include "sord-config.h"

typedef struct {
	SerdWriter writer;
	SerdEnv    env;
	SerdNode   base_uri_node;
	SerdURI    base_uri;
	Sord       sord;
} State;

int
print_version()
{
	printf("sordi " SORD_VERSION " <http://drobilla.net/software/serd>\n");
	printf("Copyright (C) 2011 David Robillard <http://drobilla.net>.\n"
	       "\nLicense: GNU LGPL version 3 or later "
	       "<http://gnu.org/licenses/lgpl.html>.\n"
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
	size_t      n_bytes = 0;
	const char* buf     = sord_node_get_string_counted(n, &n_bytes);
	SerdNode    sn      = { SERD_NOTHING, n_bytes, n_bytes - 1, (const uint8_t*)buf };
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

	Sord sord = sord_new();
	sord_open(sord);

	bool success = sord_read_file(sord, input);

	printf("loaded %u statements\n", sord_num_nodes(sord));

	SerdURI base_uri;
	if (!serd_uri_parse(input, &base_uri)) {
		fprintf(stderr, "bad input URI `%s'\n", input);
		return 1;
	}

	SerdEnv    env    = serd_env_new();
	SerdWriter writer = serd_writer_new(SERD_TURTLE, SERD_STYLE_ABBREVIATED,
	                                    env, &base_uri, file_sink, stdout);

	// Query
	SordTuple pat = { 0, 0, 0, 0 };
	SordIter  iter = sord_find(sord, pat);
	for (; !sord_iter_end(iter); sord_iter_next(iter)) {
		SordTuple tup;
		sord_iter_get(iter, tup);
		SordNode s, p, o;
		sord_tuple_load(sord, tup, &s, &p, &o);
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
