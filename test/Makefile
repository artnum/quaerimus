CC ?= gcc
ROOT := ..

CHECK_CFLAGS := $(shell pkg-config --cflags check)
CHECK_LIBS := $(shell pkg-config --libs check)
MARIA_CFLAGS := $(shell pkg-config --cflags mariadb)
MARIA_LIBS := $(shell pkg-config --libs mariadb)

CFLAGS ?= -ggdb -Wall -Wextra -pedantic -O0
CFLAGS += -I$(ROOT)/src/include $(CHECK_CFLAGS)
QURY_CFLAGS := $(CFLAGS) $(MARIA_CFLAGS)

RM ?= rm -f

ARRAY_SRC := $(ROOT)/src/array.c
QURY_SRC := $(ROOT)/src/quaerimus.c $(ROOT)/src/array.c

.PHONY: all test test-array test-qury clean

all: test-array test-qury

test: all
	@echo "==> array unit tests"
	./test-array
	@echo "==> quaerimus integration tests"
	./test-qury

test-array: test_array.c test_util.h $(ARRAY_SRC)
	$(CC) $(CFLAGS) test_array.c $(ARRAY_SRC) -o $@ $(CHECK_LIBS)

test-qury: test_qury.c test_util.h $(QURY_SRC)
	$(CC) $(QURY_CFLAGS) test_qury.c $(QURY_SRC) -o $@ $(CHECK_LIBS) $(MARIA_LIBS)

clean:
	$(RM) test-array test-qury
