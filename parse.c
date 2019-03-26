#include "json.h"
#include <stdio.h>
#include <stdlib.h>

#define ERROR_COMPLETELY_EMPTY  64
#define ERROR_CLI               65

int refill(char **buf, size_t *size, void *ctx)
{
	FILE *file = ctx;
	if (!*buf) {
		*size = BUFSIZ;
		if (!(*buf = malloc(*size))) return -1;
	}
	*size = fread(*buf, 1, *size, file);
	return !feof(file);
}

int main(int argc, char *argv[])
{
	int completely_empty = 1;
	int last_was_empty;
	struct json_reader rdr;
	struct json_item item;
	FILE *file;
	if (argc != 2) {
		fprintf(stderr, "Usage: parse <file>\n");
		return ERROR_CLI;
	}
	file = fopen(argv[1], "r");
	if (!file) {
		fprintf(stderr, "unable to open file '%s'\n", argv[1]);
		return ERROR_CLI;
	}
	json_alloc(&rdr, 8, malloc, free, realloc);
	json_source(&rdr, file, refill);
	json_init(&rdr);
	item.type = JSON_EMPTY;
	for (;;) {
		last_was_empty = item.type == JSON_EMPTY;
		if (json_read_item(&rdr, &item) < 0) {
			return item.type;
		}
		if (item.type == JSON_EMPTY) {
			if (last_was_empty) break;
		} else {
			completely_empty = 0;
		}
	}
	if (completely_empty) return ERROR_COMPLETELY_EMPTY;
	return 0;
}
