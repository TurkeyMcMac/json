header = json.h
json = json.c
parse = parse.c
test-dir = tests

parse-exe = parse
test-results = test-results.tsv

c-flags = -ansi -Wall -Wextra -O3 $(CFLAGS)
linkage = -lm

$(parse-exe): $(header) $(json) $(parse)
	$(CC) $(c-flags) -o $@ $(parse) $(json) $(linkage)

$(test-results): $(parse-exe)
	./run-tests $(test-dir) | env LC_ALL=C sort > $@

.PHONY: test
test: $(test-results)

.PHONY: clean
clean:
	$(RM) $(parse-exe) $(test-results)
