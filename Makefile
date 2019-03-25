header = json.h
json = json.c
parse = parse.c

parse-exe = parse

linkage = -lm

c-flags = -ansi -Wall -Wextra -O3 $(CFLAGS)

$(parse-exe): $(header) $(json) $(parse)
	$(CC) $(c-flags) -o $@ $(parse) $(json) $(linkage)

.PHONY: clean
clean:
	$(RM) $(parse-exe)
