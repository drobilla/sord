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
	SordModel*  sord;
} State;

int
print_version()
{
	printf("sordi " SORD_VERSION " <http://drobilla.net/software/sord>\n");
	printf("Copyright 2011 David Robillard <http://drobilla.net>.\n"
	       "License: <http://www.opensource.org/licenses/isc-license>\n"
	       "This is free software; you are free to change and redistribute it."
	       "\nThere is NO WARRANTY, to the extent permitted by law.\n");
	return 0;
}

int
print_usage(const char* name, bool error)
{
	FILE* const os = error ? stderr : stdout;
	fprintf(os, "Usage: %s [OPTION]... INPUT [BASE_URI]\n", name);
	fprintf(os, "Load and re-serialise RDF syntax.\n");
	fprintf(os, "Use - for INPUT to read from standard input.\n\n");
	fprintf(os, "  -h           Display this help and exit\n");
	fprintf(os, "  -o SYNTAX    Output syntax (`turtle' or `ntriples')\n");
	fprintf(os, "  -s INPUT     Parse INPUT as string (terminates options)\n");
	fprintf(os, "  -v           Display version information and exit\n");
	return error ? 1 : 0;
}

static size_t
file_sink(const void* buf, size_t len, void* stream)
{
	FILE* file = (FILE*)stream;
	return fwrite(buf, 1, len, file);
}

int
main(int argc, char** argv)
{
	if (argc < 2) {
		return print_usage(argv[0], true);
	}

	FILE*          in_fd         = NULL;
	SerdSyntax     output_syntax = SERD_NTRIPLES;
	bool           from_file     = true;
	const uint8_t* in_name       = NULL;
	int a = 1;
	for (; a < argc && argv[a][0] == '-'; ++a) {
		if (argv[a][1] == '\0') {
			in_name = (const uint8_t*)"(stdin)";
			in_fd   = stdin;
			break;
		} else if (argv[a][1] == 'h') {
			return print_usage(argv[0], false);
		} else if (argv[a][1] == 'v') {
			return print_version();
		} else if (argv[a][1] == 's') {
			in_name = (const uint8_t*)"(string)";
			from_file = false;
			++a;
			break;
		} else if (argv[a][1] == 'o') {
			if (++a == argc) {
				fprintf(stderr, "Missing value for -o\n");
				return 1;
			}
			if (!strcmp(argv[a], "turtle")) {
				output_syntax = SERD_TURTLE;
			} else if (!strcmp(argv[a], "ntriples")) {
				output_syntax = SERD_NTRIPLES;
			} else {
				fprintf(stderr, "Unknown output format `%s'\n",  argv[a]);
				return 1;
			}
		} else {
			fprintf(stderr, "Unknown option `%s'\n", argv[a]);
			return print_usage(argv[0], true);
		}
	}

	const uint8_t* input = (const uint8_t*)argv[a++];
	if (from_file) {
		in_name = in_name ? in_name : input;
		if (!in_fd) {
			if (serd_uri_string_has_scheme(input)) {
				// INPUT is an absolute URI, ensure it a file and chop scheme
				if (strncmp((const char*)input, "file:", 5)) {
					fprintf(stderr, "Unsupported URI scheme `%s'\n", input);
					return 1;
#ifdef __WIN32__
				} else if (!strncmp((const char*)input, "file:///", 8)) {
					input += 8;
#else
				} else if (!strncmp((const char*)input, "file://", 7)) {
					input += 7;
#endif
				} else {
					input += 5;
				}
			}
			in_fd = fopen((const char*)input, "r");
			if (!in_fd) {
				fprintf(stderr, "Failed to open file %s\n", input);
				return 1;
			}
		}
	}

	const uint8_t* base_uri_str = NULL;
	SerdURI        base_uri;
	if (a < argc) {  // Base URI given on command line
		const uint8_t* const in_base_uri = (const uint8_t*)argv[a];
		if (serd_uri_parse((const uint8_t*)in_base_uri, &base_uri)) {
			fprintf(stderr, "Invalid base URI <%s>\n", argv[2]);
			return 1;
		}
		base_uri_str = in_base_uri;
	} else if (from_file) {  // Use input file URI
		base_uri_str = input;
	} else {
		base_uri_str = (const uint8_t*)"";
	}

	if (serd_uri_parse(base_uri_str, &base_uri)) {
		fprintf(stderr, "Invalid base URI <%s>\n", base_uri_str);
		return 1;
	}
	
	SordWorld* world = sord_world_new();
	SordModel* sord  = sord_new(world, SORD_SPO|SORD_OPS, false);
	SerdEnv*   env   = serd_env_new();

	bool success = false;
	if (from_file) {
		success = sord_read_file_handle(sord, env, in_fd, in_name,
		                                base_uri_str, NULL, NULL);
	} else {
		success = sord_read_string(sord, env, input, base_uri_str);
	}

	fprintf(stderr, "Loaded %zu statements\n", sord_num_quads(sord));

	SerdEnv* write_env = serd_env_new();
	SerdNode base_uri_node = serd_node_from_string(SERD_URI, base_uri_str);
	serd_env_set_base_uri(write_env, &base_uri_node);
	serd_env_get_base_uri(write_env, &base_uri);

	SerdStyle output_style = SERD_STYLE_RESOLVED;
	if (output_syntax == SERD_NTRIPLES) {
		output_style |= SERD_STYLE_ASCII;
	} else {
		output_style |= SERD_STYLE_CURIED | SERD_STYLE_ABBREVIATED;
	}

	SerdWriter* writer = serd_writer_new(
		output_syntax,
		output_style,
		write_env, &base_uri, file_sink, stdout);

	// Write @prefix directives
	serd_env_foreach(env,
	                 (SerdPrefixSink)serd_writer_set_prefix,
	                 writer);

	// Write statements
	sord_write_writer(sord, writer, NULL);

	serd_writer_finish(writer);
	serd_writer_free(writer);

	serd_env_free(env);
	serd_env_free(write_env);

	sord_free(sord);

	return success ? 0 : 1;
}
