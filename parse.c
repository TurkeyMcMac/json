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
	int tab = 2;
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
		json_read_item(&rdr, &item);
		if (item.type > JSON_BOOLEAN) return item.type;
#if 0
		printf("%*.c", tab, ' ');
		if (item.key.bytes) {
			printf("\"%.*s\": ", (int)item.key.len, item.key.bytes);
		}
		switch (item.type) {
		case JSON_NULL:
			printf("null,\n");
			break;
		case JSON_MAP:
			printf("{\n");
			++tab;
			break;
		case JSON_END_MAP:
			printf("\b},\n");
			--tab;
			break;
		case JSON_LIST:
			printf("[\n");
			++tab;
			break;
		case JSON_END_LIST:
			printf("\b],\n");
			--tab;
			break;
		case JSON_STRING:
			printf("\"%.*s\",\n", (int)item.val.str.len,
				item.val.str.bytes);
			break;
		case JSON_NUMBER:
			printf("%f,\n", item.val.num);
			break;
		case JSON_BOOLEAN:
			printf("%s,\n", item.val.boolean ? "true" : "false");
			break;
		default:break;
		}
#endif
	} while (item.type != JSON_EMPTY);
	return 0;
}
