#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define ERROR_CLI  255

int refill(char **buf, size_t *size, void *ctx)
{
	FILE *file = ctx;
	if (!*buf) {
		*size = rand() % BUFSIZ + 1;
		if (!(*buf = malloc(*size))) return -1;
	}
	*size = fread(*buf, 1, *size, file);
	return !feof(file);
}

int main(int argc, char *argv[])
{
	struct json_reader rdr;
	struct json_item item;
	FILE *file;
	if (argc != 2) {
		fprintf(stderr, "Usage: parse <file>\n");
		return ERROR_CLI;
	}
	srand(getpid());
	file = fopen(argv[1], "r");
	if (!file) {
		fprintf(stderr, "unable to open file '%s'\n", argv[1]);
		return ERROR_CLI;
	}
	json_alloc(&rdr, 8, malloc, free, realloc);
	json_source(&rdr, file, refill);
	json_init(&rdr);
	do {
		if (json_read_item(&rdr, &item) < 0) {
			return item.type;
		}
	} while (item.type != JSON_EMPTY);
	return 0;
}
