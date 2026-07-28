CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Werror -I./src

.PHONY: all test clean docker-build docker-test

all: test_dns_parser

test_dns_parser: src/ngx_dns_parser.c test/test_dns_parser.c
	$(CC) $(CFLAGS) $^ -o $@

test: test_dns_parser
	./test_dns_parser

clean:
	rm -f test_dns_parser *.o *.so

docker-build:
	docker build -t ngx_stream_dns_module .

docker-test: docker-build
	docker run --rm ngx_stream_dns_module
