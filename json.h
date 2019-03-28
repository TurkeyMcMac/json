#ifndef JSON_H_
#define JSON_H_

#include <stddef.h>

/* The persistent state between calls to json_read_item. Consider this an opaque
 * type. */
typedef struct {
	/* The memory allocation function, compatible with malloc. */
	void *(*alloc)(size_t);
	/* The memory deallocation function, compatible with free. */
	void  (*dealloc)(void *);
	/* The memory reallocation function, compatible with realloc. */
	void *(*resize)(void *, size_t);
	/* The buffer refilling function. See json_source at the bottom for
	 * details.*/
	int   (*refill)(char **buf, size_t *bufsiz, void *ctx);
	/* The data buffer given by refill. */
	char   *buf;
	/* The data buffer size given by refill. */
	size_t  bufsiz;
	/* The pointer passed to json_source. */
	void    *ctx;
	/* The index into the buffer of the next byte to be read. */
	size_t  head;
	/* The stack of list and map brackets. */
	char   *stack;
	/* The number of frames on the stack. */
	size_t  stacksiz;
	/* The size of the stack's memory area. */
	size_t  stackcap;
	/* Internal bitflags. See json.c for more details. */
	int     flags;
} json_reader;

/* A parsed JSON string value encoded as UTF-8. Beware that JSON strings may
 * include NUL, so treating this as a C string is technically incorrect. */
struct json_string {
	/* The UTF-8 data for this string. This was allocated by the function
	 * given to the parser. */
	char  *bytes;
	/* The size of this string in bytes. */
	size_t len;
};

/* The type of a JSON item, which can be found in the type field of
 * struct json_item. */
enum json_type {
	/* There are currently no items on the stack. All opening brackete have
	 * been matched with closing ones. If this is returned twice in a row,
	 * then the input source is depleted. */
	JSON_EMPTY,
	/* The NULL value was parsed. */
	JSON_NULL,
	/* The beginning of a map has been parsed. Subsequent items will be
	 * coupled with keys. */
	JSON_MAP,
	/* The end of a map has been parsed. Subsequent items will not be
	 * coupled with keys. */
	JSON_END_MAP,
	/* The beginning of a list has been parsed. */
	JSON_LIST,
	/* The end of a list has been parsed. */
	JSON_END_LIST,
	/* A string has been parsed. the val.str field has been set. */
	JSON_STRING,
	/* A number has been parsed. the val.num field has been set. */
	JSON_NUMBER,
	/* A boolean has been parsed. the val.boolean field has been set to 1
	 * for true or 0 for false. */
	JSON_BOOLEAN,

	/* ERRORS are signified by a negative return value from json_read_item,
	 * and the val.erridx field is set to indicate where in the buffer the
	 * error occurred (this is not always relevant.) If an error occurs, the
	 * parser may not be used any longer. */

	/* One of the given memory allocation functions has failed. */
	JSON_ERROR_MEMORY,
	/* A number is in an invalid format. */
	JSON_ERROR_NUMBER_FORMAT,
	/* A single-token value (not a string) was not recognized. */
	JSON_ERROR_TOKEN,
	/* There was a trailing comma in a map, or a key did not start with '"'.
	 */
	JSON_ERROR_EXPECTED_STRING,
	/* A map ended after a key was parsed with no colon. */
	JSON_ERROR_EXPECTED_COLON,
	/* An opening bracket was incorrectly matched with a closing bracket. */
	JSON_ERROR_BRACKETS,
	/* A string had no closing '"' before the end of the input. */
	JSON_ERROR_UNCLOSED_QUOTE,
	/* An escape sequence was invalid. */
	JSON_ERROR_ESCAPE,
	/* An unescaped ASCII control character other than DELETE (0x7F) was
	 * present in a string. */
	JSON_ERROR_CONTROL_CHAR,
	/* There was a trailing comma in a list, or a colon was followed by the
	 * end of a map. */
	JSON_ERROR_EXPECTED_VALUE
};

/* The type-specific data in a json item. Many types have no associated field in
 * this union. */
union json_data {
	/* A parsed string (corresponding to JSON_STRING.) */
	struct json_string str;
	/* A parsed number (corresponding to JSON_NUMBER.) */
	double             num;
	/* A parsed boolean (corresponding to JSON_BOOLEAN.) A 1 is true, while
	 * a 0 is false. */
	int                boolean;
	/* The index into the data buffer where an error occurred (corresponding
	 * to any JSON_ERROR_* type.) */
	size_t             erridx;
};

/* An item in the stream of JSON. */
struct json_item {
	/* The key associated with this item. If a map is not currently being
	 * parsed, then key.bytes == NULL and key.len == 0. */
	struct json_string key;
	/* The type of this item. */
	enum json_type     type;
	/* The data specific to this item and its type. */
	union json_data    val;
};

/* FUNCTIONS
 * The followning functions manage the lifecycle of a json_reader. Unless
 * otherwise specified, no pointer parameter can be NULL.
 *
 * INITIALIZATION
 * Initialization is broken into three functions:
 *  1. json_alloc (example: json_alloc(&reader, 8, malloc, free, realloc))
 *  2. json_source (example: json_source(&reader, file, my_refill_file))
 *  3. json_init (always just json_init(&reader))
 * See below for details.
 *
 * PARSING
 * The only function is json_read_item(&reader, &result), but this is the heart
 * of the library.
 *
 * DEALLOCATION
 * The only function is the simple json_free(&reader). */


/* Set the allocation routines for a parser.
 * PARAMETERS:
 *  1. reader: The parser to modify.
 *  2. stacksiz: the initial size of the internal stack.
 *  3. alloc: The allocation function, compatible with malloc.
 *  4. dealloc: The deallocation function, compatible with free.
 *  5. dealloc: The reallocation function, compatible with realloc.
 * RETURN VALUE: 0 on success, or -1 if the given allocation function failed to
 * allocate the stack of size stacksiz. */
int json_alloc(json_reader *reader, size_t stacksiz,
	void *(*alloc)(size_t),
	void  (*dealloc)(void *),
	void *(*resize)(void *, size_t));

/* Set the input source for a parser.
 * PARAMETERS:
 *  1. reader: The parser to modify.
 *  2. ctx: The context passed to the refilling function each call.
 *  3. refill: The function used to refill a data buffer. This is mainly geared
 *             reading files.
 *         PARAMETERS:
 *          1. buf: the pointer to the internal pointer to the buffer. This will
 *                  never be NULL, but if the pointed-to value is NULL, then a
 *                  new buffer should be allocated using the given allocation
 *                  routine beforehand, perhaps with the size pointed to in
 *                  bufsiz. The function is free to change the buffer pointer.
 *          2. bufsiz: The pointer to the size of the buffer. This is the size
 *                     of the buffer, and its value can be changed.
 *          3. ctx: The pointer which was passed to json_source.
 *         RETURN VALUE: A negative number less than 256 indicates an error,
 *         which will be reported when the current call to json_read_item
 *         returns. A positive number indicates that the source still has more
 *         to give beyond the data which was just read. Zero means that the
 *         source is depleted. */
void json_source(json_reader *reader, void *ctx,
	int (*refill)(char **buf, size_t *bufsiz, void *ctx));

/* Initialize internal state for the given parser. This must be called AFTER the
 * other two initialization functions. */
void json_init(json_reader *reader);

/* Read the next item from the input source.
 * PARAMETERS:
 *  1. reader: The parser being used.
 *  2. result: The place to put the result. See struct json_item,
 *             enum json_type, and union json_data.
 * RETURN VALUE: 0 for success, or a negative for failure. In the case of
 * failure, result->type will be set to some JSON_ERROR_* or whatever a failed
 * call the the given refill function returned. */
int json_read_item(json_reader *reader, struct json_item *result);

/* Deallocate all memory associated with the given parser. */
void json_free(json_reader *reader);

#endif /* JSON_H_ */
