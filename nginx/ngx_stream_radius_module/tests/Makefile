CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -g -I.
LDFLAGS =

TESTS   = test_radius_parser

.PHONY: all clean

all: $(TESTS)
	@echo "==> Running tests..."
	@for t in $(TESTS); do ./$$t && echo "" || exit 1; done

test_radius_parser: test_radius_parser.c test_shim.h
	$(CC) $(CFLAGS) -o $@ test_radius_parser.c $(LDFLAGS)

clean:
	rm -f $(TESTS) *.o
