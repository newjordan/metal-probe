# metal-probe: `make` / `make probe` needs macOS and the Metal SDK.
# `make test` is host-portable C and Python and does not link Metal.

CC ?= clang
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror
OBJCFLAGS ?= -fobjc-arc -Wall -Wextra
UNAME_S := $(shell uname -s)

.PHONY: all test clean

all: probe

probe: probe.o probe_format.o
	$(CC) -o probe probe.o probe_format.o -framework Metal -framework Foundation

probe.o: probe.m probe_format.h
ifeq ($(UNAME_S),Darwin)
	$(CC) $(OBJCFLAGS) -c probe.m -o probe.o
else
	@echo "probe requires macOS with the Metal framework (this host is $(UNAME_S))." >&2
	@echo "Formatting tests do not need Metal: make test" >&2
	@exit 1
endif

probe_format.o: probe_format.c probe_format.h
	$(CC) $(CFLAGS) -c probe_format.c -o probe_format.o

test_format: test_format.c probe_format.c probe_format.h
	$(CC) $(CFLAGS) -o test_format test_format.c probe_format.c

test: test_format
	./test_format README.md
	python3 scripts/validate_report.py --self-test

clean:
	rm -f probe probe.o probe_format.o test_format report.json
