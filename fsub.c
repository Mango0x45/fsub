/*
 * BSD Zero Clause License
 *
 * Copyright (c) 2023 Thomas Voss
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#define PCRE2_CODE_UNIT_WIDTH 8
#define _POSIX_C_SOURCE 200809L

#include <sys/mman.h>
#include <sys/stat.h>

#include <err.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pcre2.h>

#define die(...)    err(EXIT_FAILURE, __VA_ARGS__)
#define streq(a, b) (strcmp(a, b) == 0)

int rv = EXIT_SUCCESS;
const char *argv0;
static PCRE2_UCHAR errbuf[256];

static int   loadfile(char **, char *);
static char *stdio_to_string(void);
static void  fsub(char *, pcre2_match_data *, pcre2_code *, char *);

int
main(int argc, char *argv[])
{
	int fd, fd2, ec, opt;
	char *needle, *haystack, *repl,
	     *argv0 = argv[0],
	     *optstr = "dim",
	     *progargs = "[-dim] pattern file [file ...]";
	uint32_t rx_opts = PCRE2_UTF;
	pcre2_code *rx;
	pcre2_match_data *mdata;
	PCRE2_SIZE eo;
	static struct option longopts[] = {
		{"dotall",      no_argument, NULL, 'd'},
		{"ignore-case", no_argument, NULL, 'i'},
		{"multiline",   no_argument, NULL, 'm'},
		{ NULL,         0,           NULL,  0 }
	};

	argv0 = argv[0];
	while ((opt = getopt_long(argc, argv, optstr, longopts, NULL)) != -1) {
		switch (opt) {
		case 'd':
			rx_opts |= PCRE2_DOTALL;
			break;
		case 'i':
			rx_opts |= PCRE2_CASELESS;
			break;
		case 'm':
			rx_opts |= PCRE2_MULTILINE;
			break;
		default:
			fprintf(stderr, "Usage: %s %s\n", argv0, progargs);
			exit(EXIT_FAILURE);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s %s\n", argv0, progargs);
		exit(EXIT_FAILURE);
	}

	needle = argv[0];
	fd = loadfile(&repl, argv[1]);
	rx = pcre2_compile((PCRE2_SPTR) needle, PCRE2_ZERO_TERMINATED,
	                   rx_opts, &ec, &eo, NULL);
	if (rx == NULL) {
		pcre2_get_error_message(ec, errbuf, sizeof(errbuf));
		fprintf(stderr,
		        "%s: Regex compilation failed at offset %d: %s\n",
		        argv[0], (int) eo, errbuf);
		exit(EXIT_FAILURE);
	}
	mdata = pcre2_match_data_create_from_pattern(rx, NULL);

	for (int i = 2; i < argc; i++) {
		if (streq(argv[i], "-")) {
			haystack = stdio_to_string();
			fsub(haystack, mdata, rx, repl);
			free(haystack);
		} else {
			fd2 = loadfile(&haystack, argv[i]);
			fsub(haystack, mdata, rx, repl);
			close(fd2);
		}
	}

	pcre2_match_data_free(mdata);
	pcre2_code_free(rx);
	close(fd);

	return rv;
}

int
loadfile(char **s, char *filename)
{
	int fd;
	struct stat sb;

	if ((fd = open(filename, O_RDONLY)) == -1)
		die("open: %s", filename);
	if (fstat(fd, &sb) == -1)
		die("fstat: %s", filename);
	*s = mmap(0, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (*s == MAP_FAILED)
		die("mmap: %s", filename);
	return fd;
}

char *
stdio_to_string(void)
{
	char *buf = NULL;
	size_t bs = 0;

	getdelim(&buf, &bs, EOF, stdin);

	return buf;
}

void
fsub(char *haystack, pcre2_match_data *mdata, pcre2_code *rx, char *repl)
{
	int rc;
	size_t pos = 0;
	PCRE2_SPTR haystack_start = (PCRE2_SPTR) haystack;
	PCRE2_SIZE *ovec;
	(void) repl;

	do {
		rc = pcre2_match(rx, haystack_start, PCRE2_ZERO_TERMINATED, pos,
		                 0, mdata, NULL);
		if (rc == PCRE2_ERROR_NOMATCH) {
			if (pos == 0)
				rv = EXIT_FAILURE;
			break;
		}
		if (rc < 0) {
			fprintf(stderr,
			        "%s: pcre2_match() failed with code %d\n",
			        argv0, rc);
			pcre2_match_data_free(mdata);
			pcre2_code_free(rx);
			exit(EXIT_FAILURE);
		}
		ovec = pcre2_get_ovector_pointer(mdata);
		printf("%.*s%s", (int) (ovec[0] - pos), haystack_start + pos,
		       repl);
		pos = ovec[1];
	} while (rc >= 0);
	fputs((char *) haystack_start + pos, stdout);
}
