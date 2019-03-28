#include "json.h"
#include <ctype.h>
#include <math.h>
#include <string.h>

int json_alloc(struct json_reader *reader, size_t stacksiz,
	void *(*alloc)(size_t),
	void  (*dealloc)(void *),
	void *(*resize)(void *, size_t))
{
	reader->stackcap = stacksiz;
	reader->stacksiz = 0;
	reader->alloc = alloc;
	reader->dealloc = dealloc;
	reader->resize = resize;
	reader->stack = reader->alloc(stacksiz);
	if (!reader->stack) return -1;
	return 0;
}

void json_source(struct json_reader *reader, void *ctx,
	int (*refill)(char **buf, size_t *bufsiz, void *ctx))
{
	reader->ctx = ctx;
	reader->refill = refill;
}

void json_init(struct json_reader *reader)
{
	reader->buf = NULL;
	reader->bufsiz = 0;
	reader->head = 0;
	reader->flags = 0;
}

void json_free(struct json_reader *reader)
{
	reader->dealloc(reader->stack);
}

enum frame {
	FRAME_EMPTY,
	FRAME_LIST,
	FRAME_MAP
};

/* Flags for reader::flags */
#define SOURCE_DEPLETED  0x0100
#define STARTED_COMPOUND 0x0200

static int is_space(int ch)
{
	return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r'
		|| ch == '\v';
}

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

static int push_byte(struct json_reader *reader, char **bytes,
	size_t *len, size_t *cap, int ch)
{
	if (*len >= *cap) {
		size_t new_cap = *cap + *cap / 2;
		char *new_bytes = reader->resize(*bytes, new_cap);
		if (!new_bytes) {
			set_error(reader, JSON_ERROR_MEMORY);
			return -1;
		}
		*cap = new_cap;
		*bytes = new_bytes;
	}
	(*bytes)[*len] = ch;
	++*len;
	return 0;
}

static int push_bytes(struct json_reader *reader, char **bytes,
	size_t *len, size_t *cap, const char *buf, size_t bufsiz)
{
	if (*len + bufsiz > *cap) {
		size_t new_cap = *len + bufsiz;
		char *new_bytes;
		new_cap += new_cap / 2;
		new_bytes = reader->resize(*bytes, new_cap);
		if (!new_bytes) {
			set_error(reader, JSON_ERROR_MEMORY);
			return -1;
		}
		*cap = new_cap;
		*bytes = new_bytes;
	}
	memcpy(*bytes + *len, buf, bufsiz);
	*len += bufsiz;
	return 0;
}

static int push_frame(struct json_reader *reader, int frame)
{
	push_byte(reader, &reader->stack, &reader->stacksiz, &reader->stackcap,
		frame);
	return 0;
}

static int pop_frame(struct json_reader *reader)
{
	if (reader->stacksiz == 0) return FRAME_EMPTY;
	return reader->stack[--reader->stacksiz];
}

static int peek_frame(const struct json_reader *reader)
{
	if (reader->stacksiz == 0) return FRAME_EMPTY;
	return reader->stack[reader->stacksiz - 1];
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
	for (;;) {
		while (is_in_range(reader)) {
			if (!is_space(reader->buf[reader->head])) return 0;
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
	if (is_in_range(reader))
		return (unsigned char)reader->buf[reader->head++];
	return -1;
}

static void reexamine_char(struct json_reader *reader)
{
	--reader->head;
}

#define NEXT_CHAR(reader, ch, do_fail) do { \
	if (((ch)  = next_char((reader))) < 0) {do_fail;} \
} while (0)

static long next_chars(struct json_reader *reader, char *buf, size_t bufsiz)
{
	if (reader->head + bufsiz <= reader->bufsiz) {
		memcpy(buf, reader->buf + reader->head, bufsiz);
		reader->head += bufsiz;
	} else {
		size_t i;
		size_t easy_copy = reader->bufsiz - reader->head;
		memcpy(buf, reader->buf + reader->head, easy_copy);
		if (refill(reader)) return -1;
		for (i = easy_copy; i < bufsiz; ++i) {
			int ch;
			NEXT_CHAR(reader, ch, return
				reader->flags & SOURCE_DEPLETED ? (long)i : -1);
			buf[i] = ch;
		}
	}
	return bufsiz;
}

static int parse_number(struct json_reader *reader, struct json_item *result)
{
	int status = JSON_ERROR_TOKEN;
	double num = 0.0;
	double sign = 1.0;
	int ch;
	NEXT_CHAR(reader, ch, goto error);
	if (ch == '-') {
		sign = -1.0;
		NEXT_CHAR(reader, ch, goto error);
	}
	if (ch == '0') {
		status = 0;
		NEXT_CHAR(reader, ch, goto finish);
	} else if (isdigit(ch)) {
		status = 0;
		do {
			num *= 10;
			num += ch - '0';
			NEXT_CHAR(reader, ch, goto finish);
		} while (isdigit(ch));
	} else {
		goto error;
	}
	if (ch == '.') {
		double fraction = 0.0;
		status = JSON_ERROR_NUMBER_FORMAT;
		NEXT_CHAR(reader, ch, goto error);
		if (isdigit(ch)) {
			status = 0;
			do {
				fraction += ch - '0';
				fraction /= 10;
				NEXT_CHAR(reader, ch,
					num += fraction;
					goto finish);
			} while (isdigit(ch));
		} else {
			goto error;
		}
		num += fraction;
	}
	if (ch == 'e' || ch == 'E') {
		long expsign = 1;
		long exponent = 0;
		status = JSON_ERROR_NUMBER_FORMAT;
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
				num *= pow(expsign * 10, exponent);
				goto finish;
			);
		}
		num *= pow(expsign * 10, exponent);
	}
	if (status) goto error;
	reexamine_char(reader);
finish:
	num *= sign;
	result->type = JSON_NUMBER;
	result->val.num = num;
	return -has_error(reader);

error:
	reexamine_char(reader);
	set_error(reader, status);
	return -1;
}

static int parse_token_value(struct json_reader *reader,
	struct json_item *result)
{
	long read;
	char tokbuf[5];
	switch (reader->buf[reader->head]) {
	case 't': /* true */
		if ((read = next_chars(reader, tokbuf, 4)) < 0) goto error;
		if (read < 4 || memcmp(tokbuf, "true", 4)) goto error_invalid;
		result->type = JSON_BOOLEAN;
		result->val.boolean = 1;
		break;
	case 'f': /* false */
		if ((read = next_chars(reader, tokbuf, 5)) < 0) goto error;
		if (read < 5 || memcmp(tokbuf, "false", 5)) goto error_invalid;
		result->type = JSON_BOOLEAN;
		result->val.boolean = 0;
		break;
	case 'n': /* null */
		if ((read = next_chars(reader, tokbuf, 4)) < 0) goto error;
		if (read < 4 || memcmp(tokbuf, "null", 4)) goto error_invalid;
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
	long i;
	for (i = 3, shift = 0; i >= 0; --i, shift += 4) {
		int dig = tolower(hex[i]);
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
	int read_extra_cp = 0, read_extra_escape = 0;
	long utf16[2] = {-1, -1};
	long codepoint, extracp = -1;
	char utf8[4];
	char buf[4];
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
	case '/':            break;
	case 'u':
		read = next_chars(reader, buf, 4);
		if (read < 0) goto error;
		if (read < 4) goto error_esc;
		utf16[0] = hex_short(buf);
		if (utf16[0] < 0) goto error_esc;
		codepoint = utf16_to_codepoint(utf16[0]);
		if (is_high_surrogate(utf16[0])) {
			NEXT_CHAR(reader, ch, goto error);
			if (ch != '\\') {
				reexamine_char(reader);
			} else {
				NEXT_CHAR(reader, ch, goto error);
				if (ch == 'u') {
					read = next_chars(reader, buf,
						4);
					if (read < 0) goto error;
					if (read < 4) goto error_esc;
					utf16[1] = hex_short(buf);
					if (utf16[1] < 0) goto error_esc;
					if (is_low_surrogate(utf16[1])) {
						codepoint =
							utf16_pair_to_codepoint(
							utf16[0], utf16[1]);
					} else {
						read_extra_cp = 1;
						extracp = utf16_to_codepoint(
								utf16[1]);
					}
				} else {
					reexamine_char(reader);
					read_extra_escape = 1;
				}
			}
		}
		if (push_bytes(reader, &str->bytes, &str->len, cap,
			utf8, codepoint_to_utf8(codepoint, utf8))) goto error;
		if (read_extra_cp) {
			if (push_bytes(reader, &str->bytes, &str->len, cap,
					utf8, codepoint_to_utf8(extracp, utf8)))
				goto error;
		} else if (read_extra_escape) {
			/* This will only every recurse once, since it can only
			 * do so for \uXXXX, but that is handled non-
			 * recursively. */
			if (escape_char(reader, str, cap)) goto error;
		}
		return 0;
	default:
		goto error_esc;
	}
	if (push_byte(reader, &str->bytes, &str->len, cap, ch)) goto error;
	return 0;

error_esc:
	set_error(reader, JSON_ERROR_ESCAPE);
error:
	return -1;
}

static int parse_string(struct json_reader *reader, struct json_string *str)
{
	int ch;
	size_t cap = 16;
	NEXT_CHAR(reader, ch, goto error);
	if (ch != '"') goto error_expected_string;
	str->bytes = alloc(reader, cap);
	if (!str->bytes) goto error;
	str->len = 0;
	while ((ch = next_char(reader)) != '"') {
		if (ch < 0) {
			if (reader->flags & SOURCE_DEPLETED)
				goto error_unclosed_quote;
			else if (refill(reader))
				goto error;
		}
		if (ch == '\\') {
			if (escape_char(reader, str, &cap))
				goto error;
		} else if (ch < 32) {
			goto error_control_char;
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

error_control_char:
	set_error(reader, JSON_ERROR_CONTROL_CHAR);
	return -1;

error:
	return -1;
}

static int parse_value(struct json_reader *reader, struct json_item *result)
{
	int ch;
	NEXT_CHAR(reader, ch, goto error_expected_value);
	switch (ch) {
	case '[':
		push_frame(reader, FRAME_LIST);
		reader->flags |= STARTED_COMPOUND;
		result->type = JSON_LIST;
		break;
	case '{':
		push_frame(reader, FRAME_MAP);
		reader->flags |= STARTED_COMPOUND;
		result->type = JSON_MAP;
		break;
	case '"':
		reexamine_char(reader);
		if (parse_string(reader, &result->val.str)) goto error;
		result->type = JSON_STRING;
		break;
	default:
		reexamine_char(reader);
		if (parse_token_value(reader, result)) goto error;
		break;
	}
	return 0;

error_expected_value:
	if (!has_error(reader)) set_error(reader, JSON_ERROR_EXPECTED_VALUE);
error:
	return -1;
}

static int try_compound_end(struct json_reader *reader, int endch,
	enum json_type type, struct json_item *result)
{
	int ch;
	NEXT_CHAR(reader, ch, return -has_error(reader));
	if (ch == endch) {
		pop_frame(reader);
		result->type = type;
	} else {
		reexamine_char(reader);
	}
	return 0;
}

static int parse_after_elem(struct json_reader *reader, int endch,
	enum json_type type, struct json_item *result)
{
	int ch;
	NEXT_CHAR(reader, ch, return -has_error(reader));
	if (ch == endch) {
		pop_frame(reader);
		result->type = type;
	} else if (ch != ',') {
		set_error(reader, JSON_ERROR_BRACKETS);
		return -1;
	}
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
	result->type = JSON_EMPTY;
	result->key.len = 0;
	result->key.bytes = NULL;
	if (!is_in_range(reader)) {
		if (reader->flags & SOURCE_DEPLETED) {
			if (reader->stacksiz == 0) {
				return 0;
			} else {
				set_error(reader, JSON_ERROR_BRACKETS);
				goto error;
			}
		} else if (refill(reader)) {
			goto error;
		}
	}
	switch (peek_frame(reader)) {
	case FRAME_EMPTY:
		if (skip_spaces(reader)) goto error;
		if (is_in_range(reader) && parse_value(reader, result))
			goto error;
		return 0;
	case FRAME_LIST:
		if (skip_spaces(reader)) goto error;
		if (reader->flags & STARTED_COMPOUND) {
			if (try_compound_end(reader, ']', JSON_END_LIST,
				result)) goto error;
			reader->flags &= ~STARTED_COMPOUND;
		} else {
			if (parse_after_elem(reader, ']', JSON_END_LIST,
				result)) goto error;
		}
		if (result->type == JSON_END_LIST) return 0;
		(void)(
			skip_spaces(reader) ||
			parse_value(reader, result)
		);
		break;
	case FRAME_MAP:
		if (skip_spaces(reader)) goto error;
		if (reader->flags & STARTED_COMPOUND) {
			if (try_compound_end(reader, '}', JSON_END_MAP,result))
				goto error;
			reader->flags &= ~STARTED_COMPOUND;
		} else {
			if (parse_after_elem(reader, '}', JSON_END_MAP, result))
				goto error;
		}
		if (result->type == JSON_END_MAP) return 0;
		(void)(
			skip_spaces(reader) ||
			parse_string(reader, &result->key) ||
			skip_spaces(reader) ||
			parse_colon(reader) ||
			skip_spaces(reader) ||
			parse_value(reader, result)
		);
		break;
	}
	if (has_error(reader)) goto error;
	return 0;

error:
	carry_error(reader, result);
	clear_error(reader);
	return -1;
}
