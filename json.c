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
	int             flags;
};

struct json_string {
	char           *bytes;
	size_t        	len;
};

enum json_type {
	JSON_NULL,
	JSON_MAP,
	JSON_END_MAP,
	JSON_END_LIST,
	JSON_STRING,
	JSON_NUMBER,
	JSON_BOOLEAN,
	JSON_ERROR
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
	struct json_data   val;
};

static int is_space(char ch)
{
	return ch == ' ' || ch == '\n' || ch == '\t' || ch == '\r';
}

static int is_in_range(const struct json_reader *reader)
{
	return reader->head < reader->bufsiz;
}

static int refill(struct json_reader *reader)
{
	size_t newsiz = reader->bufsiz;
	int errnum = reader->refill(&reader->buf, &newsiz, reader->ctx);
	if (errnum) {
		set_error(reader, -errnum);
		return -1;
	}
	if (newsiz != reader->bufsiz) reader->flags |= SOURCE_DEPLETED;
	reader->bufsiz = newsiz;
	reader->head = 0;
	return 0;
}

static int skip_spaces(struct json_reader *reader)
{
	int errnum = 0;
	for (;;) {
		while (is_in_range(reader)) {
			if (!is_space(reader->buf[reader->head])) return 0;
			++reader->head;
		}
		if (reader->flags & SOURCE_DEPLETED) return 0;
		if (refill(reader)) return -1;
	}
}

static int parse_token_value(struct json_reader *reader, struct json_string *str)
{
	char token[32];
	switch (reader->buf[reader->head]) {
	case 't': /* true */
		
	}
}

static int parse_string(struct json_reader *reader, struct json_string *str)
{
	char ch;
	size_t cap = 16;
	if (reader->buf[reader->head] != '"') goto error_expected_string;
	str->bytes = alloc(reader, cap);
	if (!str->bytes) goto error;
	str->len = 0;
	++reader->head;
	while ((ch = reader->buf[reader->head]) != '"') {
		if (!is_in_range(reader) && refill(reader)) goto error;
		if (ch == '\\') {
			ch = escape_char(reader);
		} else {
			++reader->head;
		}
		if (push_byte(&str->bytes, &str->len, &cap, ch)) goto error;
	}
	return 0;

error_expected_string:
	set_error(reader, JSON_ERROR_EXPECTED_STRING);
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
		parse_after_elem(reader, ']', JSON_END_LIST);
		break;
	case FRAME_MAP:
		skip_spaces(reader) ||
		parse_string(reader, &result->key, result) ||
		skip_spaces(reader) ||
		parse_char(reader, ':') ||
		skip_spaces(reader) ||
		parse_value(reader, result) ||
		skip_spaces(reader) ||
		parse_after_elem(reader, '}', JSON_END_MAP);
		break;
	}
	if (reader->flags & ERROR_OCCURRED) {
		reader->flags &= ~ERROR_OCCURRED;
		result->err_idx = reader->head;
		goto error;
	}
	return 0;

error:
	return -1;
}
