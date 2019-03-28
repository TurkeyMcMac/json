This is my JSON parser. I want some practice making parsers, and JSON seemed
relatively easy and useful.

## API
The API is documented in `json.h`, but here's a quick example:
```c
static int refill(char **buf, size_t *bufsiz, void *ctx)
{
	int retval = 1;
	size_t read;
	FILE *file = ctx;
	if (!*buf) {
		*bufsiz = BUFSIZ;
		*buf = malloc(*bufsiz);
		if (!*buf) return -JSON_ERROR_MEMORY;
	}
	read = fread(*buf, 1, *bufsiz, file);
	if (read < *bufsiz) {
		retval = feof(file) ? 0 : -JSON_ERROR_ERRNO;
		*bufsiz = read;
	}
	return retval;
}

...

json_reader rdr;
struct json_item item;

json_alloc(&rdr, 8, malloc, free, realloc);
json_source(&rdr, file, refill);
json_init(&rdr);
do {
	if (json_read_item(&rdr, &item) < 0) {
		/* Handle the error, the type of which is in item.type. */
	} else {
		/* Append this item using item.type, item.key, and item.val. */
	}
} while (item.type != JSON_EMPTY);
```

## Tests
I got the tests from [this repository](https://github.com/nst/JSONTestSuite),
which can be found under the `tests` directory here, or the `test_parsing`
directory in the original project. Many thanks to Nicolas Seriot for providing
those tests publicly.
