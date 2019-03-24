#ifndef JSON_H_
#define JSON_H_

#include <stddef.h>

struct json_reader {
	void         *(*alloc)(size_t);
	void          (*dealloc)(void *);
	void         *(*resize)(void *, size_t);
	int           (*refill)(char **buf, size_t *bufsiz, void *ctx);
	char           *buf;
	size_t          bufsiz;
	void           *ctx;
	size_t        	head;
	unsigned char  *stack;
	size_t        	stacksiz;
	size_t        	stackcap;
	int             flags;
};

struct json_string {
	unsigned char  *bytes;
	size_t        	len;
};

enum json_type {
	JSON_NULL,
	JSON_MAP,
	JSON_END_MAP,
	JSON_LIST,
	JSON_END_LIST,
	JSON_STRING,
	JSON_NUMBER,
	JSON_BOOLEAN,
	JSON_ERROR_MEMORY,
	JSON_ERROR_NUMBER_FORMAT,
	JSON_ERROR_TOKEN,
	JSON_ERROR_EXPECTED_STRING,
	JSON_ERROR_EXPECTED_COLON,
	JSON_ERROR_BRACKETS,
	JSON_ERROR_UNCLOSED_QUOTE,
	JSON_ERROR_HEX
};

union json_data {
	struct json_string str;
	double             num;
	int                boolean;
	size_t             erridx;
};

struct json_item {
	struct json_string key;
	enum json_type     type;
	union json_data    val;
};

int json_alloc(struct json_reader *reader, size_t stacksiz,
	void *(*alloc)(size_t),
	void  (*dealloc)(void *),
	void *(*resize)(void *, size_t));

void json_source(struct json_reader *reader, void *ctx,
	int (*refill)(char **buf, size_t *bufsiz, void *ctx));

void json_init(struct json_reader *reader);

int json_read_item(struct json_reader *reader, struct json_item *result);

void json_free(struct json_reader *reader);

#endif /* JSON_H_ */
