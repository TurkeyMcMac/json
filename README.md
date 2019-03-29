This is my JSON parser. I want some practice making parsers, and JSON seemed
relatively easy and useful.

## API
The API is documented in `json.h`, but here's a quick example:
```c
static int refill(char **buf, size_t *bufsiz, void *ctx)
{
	size_t read;
	FILE *file = ctx;
	read = fread(*buf, 1, *bufsiz, file);
	if (read < *bufsiz) {
		*bufsiz = read;
		return feof(file) ? 0 : -JSON_ERROR_ERRNO;
	}
	return 1;
}

...

char buf[BUFSIZ];
json_reader rdr;
struct json_item item;

json_alloc(&rdr, 8, malloc, free, realloc);
json_source(&rdr, buf, sizeof(buf), file, refill);
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

### Test Failures
The currently failing tests do not seem very important to me to fix. If the user
wants this parser to be compliant, it is easy to keep it that way. Just parse
only one map/array and check that the input source is empty afterward.
