#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#define ERROR_COMPLETELY_EMPTY  64
#define ERROR_CLI               65

void debug_print(int *indent, struct json_item *item)
{
	if (*indent > 0) printf("%*c", *indent * 2, ' ');
	if (item->key.bytes) {
		printf("\"%.*s\": ", (int)item->key.len, item->key.bytes);
	}
	switch (item->type) {
	case JSON_NULL:
		printf("null,\n");
		break;
	case JSON_MAP:
		printf("{\n");
		++*indent;
		break;
	case JSON_END_MAP:
		printf("\b\b},\n");
		--*indent;
		break;
	case JSON_LIST:
		printf("[\n");
		++*indent;
		break;
	case JSON_END_LIST:
		printf("\b\b],\n");
		--*indent;
		break;
	case JSON_STRING:
		printf("\"%.*s\",\n", (int)item->val.str.len,
			item->val.str.bytes);
		break;
	case JSON_NUMBER:
		printf("%f,\n", item->val.num);
		break;
	case JSON_BOOLEAN:
		printf("%s,\n", item->val.boolean ? "true" : "false");
		break;
	default:break;
	}
}

int main(int argc, char *argv[])
{
	int status = 0;
	char buf[BUFSIZ];
	int completely_empty = 1;
	int last_was_empty;
	int is_printing;
	int indent = 0;
	json_reader rdr;
	struct json_item item;
	FILE *file;
	if (argc != 2) {
		fprintf(stderr, "Usage: parse <file>\n");
		return ERROR_CLI;
	}
	is_printing = getenv("JSON_DEBUG_PRINT") != NULL;
	file = fopen(argv[1], "r");
	if (!file) {
		fprintf(stderr, "unable to open file '%s'\n", argv[1]);
		return ERROR_CLI;
	}
	json_alloc(&rdr, NULL, 8, malloc, free, realloc);
	json_source_file(&rdr, buf, sizeof(buf), file);
	item.type = JSON_EMPTY;
	for (;;) {
		last_was_empty = item.type == JSON_EMPTY;
		if (json_read_item(&rdr, &item) < 0) {
			status = item.type;
			if (is_printing) {
				char *buffer;
				size_t bufsiz;
				json_get_buf(&rdr, &buffer, &bufsiz);
				fprintf(stderr, "Error at: ");
				if (item.val.erridx >= bufsiz) {
					fprintf(stderr, "EOF\n");
				} else {
					char ch = buffer[item.val.erridx];
					fprintf(stderr,
						iscntrl(ch) || !isascii(ch) ?
							"%02X\n" : "'%c'\n",
						(unsigned char)ch);
				}
			}
			goto release;
		}
		if (item.type == JSON_EMPTY) {
			if (last_was_empty) break;
		} else {
			completely_empty = 0;
		}
		if (is_printing) debug_print(&indent, &item);
		free(item.key.bytes);
		if (item.type == JSON_STRING) free(item.val.str.bytes);
	}
	if (completely_empty) status = ERROR_COMPLETELY_EMPTY;
release:
	json_free(&rdr);
	fclose(file);
	return status;
}
