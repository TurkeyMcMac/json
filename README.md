This is my JSON parser. I want some practice making parsers, and JSON seemed
relatively easy and useful.

## API
The API is documented in `json.h`, but here's a quick example:
```c
/* Compile with -DJSON_WITH_STDIO */

FILE *file = ...;
char buf[BUFSIZ];
json_reader rdr;
struct json_item item;

json_alloc(&rdr, 8, malloc, free, realloc);
json_source_file(&rdr, buf, sizeof(buf), file);
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

## Dependencies
Currently the library depends on the standard library for string manipulation
and character classification. It depends on the standard math library for the
pow function. It does not require the user to use the standard library
allocation, although that is the easiest method.

The tests require Perl 5 or so to run, and I haven't tested the scripts on
non-unix systems. run-tests specifically makes heavy use of POSIX system calls.
