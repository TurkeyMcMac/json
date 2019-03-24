#include <ctype.h>
#include <math.h>
#include <string.h>

struct json_reader {
	void         *(*alloc)(size_t);
	void          (*free)(void *);
	void         *(*resize)(void *, size_t);
	int           (*refill)(char **buf, size_t *bufsiz, void *ctx);
	char           *buf;
	size_t          bufsiz;
	void           *ctx;
	size_t        	head;
	char           *string;
	char           *escaping;
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

enum frame {
	FRAME_EMPTY,
	FRAME_LIST,
	FRAME_MAP
};

/* Flags for reader::flags */
#define SOURCE_DEPLETED 0x0100

static void set_error(struct json_reader *reader, enum json_type err)
{
	reader->flags |= err;
}

static void clear_error(struct json_reader *reader)
{
	reader->flags &= ~0xFF;
}

static int has_error(struct json_reader *reader)
{
	return (reader->flags & 0xFF) != 0;
}

static void carry_error(struct json_reader *from, struct json_item *to)
{
	to->type = from->flags & 0xFF;
	to->val.erridx = from->head;
}

static void *alloc(struct json_reader *reader, size_t size)
{
	void *ptr = reader->alloc(size);
	if (!ptr) set_error(reader, JSON_ERROR_MEMORY);
	return ptr;
}

static int push_byte(struct json_reader *reader, unsigned char **bytes,
	size_t *len, size_t *cap, int ch)
{
	if (*len >= *cap) {
		size_t new_cap = *cap + *cap / 2;
		unsigned char *new_bytes = reader->resize(*bytes, new_cap);
		if (!new_bytes) {
			set_error(reader, JSON_ERROR_MEMORY);
			return -1;
		}
		*cap = new_cap;
		*bytes = new_bytes;
	}
	return 0;
}

static int push_bytes(struct json_reader *reader, unsigned char **bytes,
	size_t *len, size_t *cap, const unsigned char *buf, size_t bufsiz)
{
	if (*len + bufsiz > *cap) {
		size_t new_cap = *len + bufsiz;
		unsigned char *new_bytes;
		new_cap += new_cap / 2;
		new_bytes = reader->resize(*bytes, new_cap);
		if (!new_bytes) {
			set_error(reader, JSON_ERROR_MEMORY);
			return -1;
		}
		*cap = new_cap;
		*bytes = new_bytes;
	}
	return 0;
}

#define push_frame(reader, frame) push_byte((reader), &(reader)->stack, \
	&(reader)->stacksiz, &(reader)->stackcap, (frame))

static int pop_frame(struct json_reader *reader)
{
	if (reader->stacksiz == 0) return FRAME_EMPTY;
	return reader->stack[reader->stacksiz--];
}

static int peek_frame(const struct json_reader *reader)
{
	if (reader->stacksiz == 0) return FRAME_EMPTY;
	return reader->stack[reader->stacksiz];
}

static int is_in_range(const struct json_reader *reader)
{
	return reader->head < reader->bufsiz;
}

static int refill(struct json_reader *reader)
{
	size_t newsiz = reader->bufsiz;
	int retval = reader->refill(&reader->buf, &newsiz, reader->ctx);
	if (retval < 0) {
		set_error(reader, -retval);
		return -1;
	}
	if (retval == 0) reader->flags |= SOURCE_DEPLETED;
	reader->bufsiz = newsiz;
	reader->head = 0;
	return 0;
}

static int skip_spaces(struct json_reader *reader)
{
	int errnum = 0;
	for (;;) {
		while (is_in_range(reader)) {
			if (!isspace(reader->buf[reader->head])) return 0;
			++reader->head;
		}
		if (reader->flags & SOURCE_DEPLETED) return 0;
		if (refill(reader)) return -1;
	}
}

static int next_char(struct json_reader *reader)
{
	while (!is_in_range(reader) && (reader->flags & SOURCE_DEPLETED) == 0) {
		if (refill(reader)) return -1;
	}
	if (is_in_range(reader)) return reader->buf[reader->head++];
	return -1;
}

#define NEXT_CHAR(reader, ch, do_fail) do { \
	if (((ch)  = next_char((reader))) < 0) {do_fail;} \
} while (0)

static long next_chars(struct json_reader *reader, char *buf, size_t bufsiz)
{
	if (reader->head + bufsiz <= reader->bufsiz) {
		memcpy(buf, reader->buf + reader->head, bufsiz);
	} else {
		size_t i;
		size_t easy_copy = reader->bufsiz - reader->head;
		memcpy(buf, reader->buf + reader->head, easy_copy);
		if (refill(reader)) return -1;
		for (i = easy_copy; i < bufsiz; ++i) {
			char ch = next_char(reader);
			if (ch < 0)
				return reader->flags & SOURCE_DEPLETED ? i : -1;
			buf[i] = ch;
		}
	}
	return bufsiz;
}

static int parse_number(struct json_reader *reader, struct json_item *result)
{
	int status = -1;
	double num = 0.0;
	double sign = 1.0;
	int ch = reader->buf[reader->head];
	if (ch == '-') {
		sign = -1.0;
		NEXT_CHAR(reader, ch, goto error);
	}
	if (ch == '0') {
		status = 0;
		NEXT_CHAR(reader, ch, goto finish);
	} else {
		while (isdigit(ch)) {
			status = 0;
			num *= 10;
			num += ch - '0';
			NEXT_CHAR(reader, ch, goto finish);
		}
	}
	if (ch == '.') {
		double fraction = 0.0;
		status = -1;
		NEXT_CHAR(reader, ch, goto error);
		while (isdigit(ch)) {
			status = 0;
			fraction += ch - '0';
			fraction /= 10;
			NEXT_CHAR(reader, ch, num += fraction; goto finish);
		}
		num += fraction;
	}
	if (ch == 'e' || ch == 'E') {
		long expsign = 1;
		long exponent = 0;
		status = -1;
		NEXT_CHAR(reader, ch, goto error);
		switch (ch) {
		case '-':
			expsign = -1;
			/* FALLTHROUGH */
		case '+':
			NEXT_CHAR(reader, ch, goto error);
			break;
		}
		while (isdigit(ch)) {
			status = 0;
			exponent *= 10;
			exponent += ch - '0';
			NEXT_CHAR(reader, ch,
				num = pow(num, exponent);
				goto finish;
			);
		}
		num = pow(num, exponent);
	}
	if (status) goto error;
finish:
	num *= sign;
	result->type = JSON_NUMBER;
	result->val.num = num;
	return -has_error(reader);

error:
	if (!(reader->flags & SOURCE_DEPLETED))
		set_error(reader, JSON_ERROR_NUMBER_FORMAT);
	return -1;
}

static int parse_token_value(struct json_reader *reader,
	struct json_item *result)
{
	double num;
	int errnum;
	char tokbuf[4];
	switch (reader->buf[reader->head]) {
	case 't': /* true */
		errnum = next_chars(reader, tokbuf, 3);
		if (errnum) goto error;
		if (memcmp(tokbuf, "rue", 3)) goto error_invalid;
		result->type = JSON_BOOLEAN;
		result->val.boolean = 1;
		break;
	case 'f': /* false */
		errnum = next_chars(reader, tokbuf, 4);
		if (errnum) goto error;
		if (memcmp(tokbuf, "alse", 4)) goto error_invalid;
		result->type = JSON_BOOLEAN;
		result->val.boolean = 0;
		break;
	case 'n': /* null */
		errnum = next_chars(reader, tokbuf, 3);
		if (errnum) goto error;
		if (memcmp(tokbuf, "ull", 3)) goto error_invalid;
		result->type = JSON_NULL;
		break;
	default: /* number */
		if (parse_number(reader, result)) goto error;
		break;
	}
	return 0;

error_invalid:
	set_error(reader, JSON_ERROR_TOKEN);
error:
	return -1;
}

static int is_high_surrogate(unsigned utf16)
{
	return (utf16 & 0xD800) == 0xD800;
}

static int is_low_surrogate(unsigned utf16)
{
	return (utf16 & 0xDC00) == 0xDC00;
}

static long utf16_to_codepoint(unsigned utf16)
{
	return utf16;
}

static long utf16_pair_to_codepoint(unsigned high, unsigned low)
{
	return (high - 0xD800) * 0x400 + (low - 0xDC00) + 0x10000;
}

size_t codepoint_to_utf8(long cp, char buf[4])
{
	if (cp <= 0x7F) {
		buf[0] = cp;
		return 1;
	} else if (cp <= 0x7FF) {
		buf[0] = 0xC0 | (cp >> 6);
		buf[1] = 0x80 | (cp & 0x3F);
		return 2;
	} else if (cp <= 0xFFFF) {
		buf[0] = 0xE0 | (cp >> 12);
		buf[1] = 0x80 | ((cp >> 6) & 0x3F);
		buf[2] = 0x80 | (cp & 0x3F);
		return 3;
	} else {
		buf[0] = 0xF0 | (cp >> 18);
		buf[1] = 0x80 | ((cp >> 12) & 0x3F);
		buf[2] = 0x80 | ((cp >> 6) & 0x3F);
		buf[3] = 0x80 | (cp & 0x3F);
		return 4;
	}
}

static long hex_short(const char hex[4])
{
	long num = 0;
	unsigned shift = 0;
	unsigned i;
	for (i = 4, shift = 0; i >= 0; --i, shift += 4) {
		int dig = toupper(hex[i]);
		long nibble;
		if (dig >= '0' && dig <= '9') nibble = dig - '0';
		else if (dig >= 'a' && dig <= 'f') nibble = 10 + dig - 'a';
		else return -1;
		num |= nibble << shift;
	}
	return num;
}

static int escape_char(struct json_reader *reader, struct json_string *str,
	size_t *cap)
{
	long utf16[2] = {-1, -1};
	long codepoint, extracp = -1;
	char utf8[4];
	size_t utf8len;
	char buf[6];
	long read;
	int ch = next_char(reader);
	if (ch < 0) goto error;
	switch (ch) {
	case 'b': ch = '\b'; break;
	case 'f': ch = '\f'; break;
	case 'n': ch = '\n'; break;
	case 'r': ch = '\r'; break;
	case 't': ch = '\t'; break;
	case '"': ch =  '"'; break;
	case'\\':            break;
	case 'u':
		read = next_chars(reader, buf, 4);
		if (read < 0) goto error;
		if (read < 4) goto hex_error;
		utf16[0] = hex_short(buf);
		if (utf16[0] < 0) goto hex_error;
		if (is_high_surrogate(utf16[0])) {
			read = next_chars(reader, buf, 6);
			if (read < 0) goto error;
			if (read < 6) goto hex_error;
			if (buf[0] == '\\' && buf[1] == 'u') {
				utf16[1] = hex_short(buf + 2);
				if (utf16[1] < 0) goto hex_error;
				if (is_low_surrogate(utf16[1])) {
					codepoint = utf16_pair_to_codepoint(
						utf16[0], utf16[1]);
				} else {
					codepoint =
						utf16_to_codepoint(utf16[0]);
					extracp = 
						utf16_to_codepoint(utf16[1]);
				}
			} else {
				codepoint = utf16_to_codepoint(utf16[0]);
			}
		} else {
			codepoint = utf16_to_codepoint(utf16[0]);
		}
		if (push_bytes(reader, &str->bytes, &str->len, cap,
			(unsigned char *)utf8,
			codepoint_to_utf8(codepoint, utf8))) goto error;
		if (extracp >= 0) { /* We read an extra escape sequence. */
			if (push_bytes(reader, &str->bytes, &str->len, cap,
				(unsigned char *)utf8,
				codepoint_to_utf8(extracp, utf8))) goto error;
		} else if (read == 6) { /* We read 6 extra normal characters. */
			if (push_bytes(reader, &str->bytes, &str->len, cap,
					(unsigned char *)buf, read))
				goto error;
		}
		return 0;
	default:
		if (push_byte(reader, &str->bytes, &str->len, cap, '\\'))
			goto error;
		break;
	}
	if (push_byte(reader, &str->bytes, &str->len, cap, ch)) goto error;
	return 0;

hex_error:
	set_error(reader, JSON_ERROR_HEX);
error:
	return -1;
}

static int parse_string(struct json_reader *reader, struct json_string *str)
{
	char ch;
	size_t cap = 16;
	if (reader->buf[reader->head] != '"') goto error_expected_string;
	str->bytes = alloc(reader, cap);
	if (!str->bytes) goto error;
	str->len = 0;
	while ((ch = next_char(reader)) != '"') {
		if (ch < 0) {
			if (reader->flags & SOURCE_DEPLETED)
				goto error_unclosed_quote;
			else
				goto error;
		}
		if (ch == '\\') {
			if (escape_char(reader, str, &cap))
				goto error;
		} else {
			if (push_byte(reader, &str->bytes, &str->len, &cap, ch))
				goto error;
		}
	}
	return 0;

error_expected_string:
	set_error(reader, JSON_ERROR_EXPECTED_STRING);
	return -1;

error_unclosed_quote:
	set_error(reader, JSON_ERROR_UNCLOSED_QUOTE);
	return -1;

error:
	return -1;
}

static int parse_value(struct json_reader *reader, struct json_item *result)
{
	switch (reader->buf[reader->head]) {
	case '[':
		push_frame(reader, FRAME_LIST);
		result->type = JSON_LIST;
		++reader->head;
		break;
	case ']':
		if (pop_frame(reader) != FRAME_LIST) goto error_brackets;
		result->type = JSON_END_LIST;
		++reader->head;
		break;
	case '{':
		push_frame(reader, FRAME_LIST);
		result->type = JSON_MAP;
		++reader->head;
		break;
	case '}':
		if (pop_frame(reader) != FRAME_MAP) goto error_brackets;
		result->type = JSON_END_MAP;
		++reader->head;
		break;
	case '"':
		if (parse_string(reader, &result->val.str)) goto error;
		break;
	default:
		if (parse_token_value(reader, result)) goto error;
	}
	return 0;

error_brackets:
	set_error(reader, JSON_ERROR_BRACKETS);
error:
	return -1;
}

static int parse_item(struct json_reader *reader, struct json_item *result)
{
	switch (reader->buf[reader->head]) {
	case ']':
		if (pop_frame(reader) != FRAME_LIST) goto error_brackets;
		result->type = JSON_END_LIST;
		++reader->head;
		break;
	case '}':
		if (pop_frame(reader) != FRAME_MAP) goto error_brackets;
		result->type = JSON_END_MAP;
		++reader->head;
		break;
	default:
		return parse_value(reader, result);
	}
	return 0;

error_brackets:
	set_error(reader, JSON_ERROR_BRACKETS);
	return -1;
}

static int parse_after_elem(struct json_reader *reader, int endch,
	enum json_type type, struct json_item *result)
{
	if (reader->buf[reader->head] != endch) {
		set_error(reader, JSON_ERROR_BRACKETS);
		return -1;
	}
	++reader->head;
	pop_frame(reader);
	result->type = type;
	return 0;
}

static int parse_colon(struct json_reader *reader)
{
	if (reader->buf[reader->head] != ':') {
		set_error(reader, JSON_ERROR_EXPECTED_COLON);
		return -1;
	}
	++reader->head;
	return 0;
}

int json_read_item(struct json_reader *reader, struct json_item *result)
{
	if (!is_in_range(reader) && refill(reader)) goto error;
	switch (peek_frame(reader)) {
	case FRAME_EMPTY:
		skip_spaces(reader) ||
		parse_item(reader, result);
		break;
	case FRAME_LIST:
		skip_spaces(reader) ||
		parse_value(reader, result) ||
		skip_spaces(reader) ||
		parse_after_elem(reader, ']', JSON_END_LIST, result);
		break;
	case FRAME_MAP:
		skip_spaces(reader) ||
		parse_string(reader, &result->key) ||
		skip_spaces(reader) ||
		parse_colon(reader) ||
		skip_spaces(reader) ||
		parse_value(reader, result) ||
		skip_spaces(reader) ||
		parse_after_elem(reader, '}', JSON_END_MAP, result);
		break;
	}
	if (has_error(reader)) goto error;
	return 0;

error:
	carry_error(reader, result);
	clear_error(reader);
	return -1;
}