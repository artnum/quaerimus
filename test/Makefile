CC=gcc
LIBS=`pkg-config --libs memarena check`
CFLAGS=`pkg-config --cflags memarena check`
RM=rm

all: test-array

test-array: ../src/array.c array.c
	$(CC) $(CFLAGS) ../src/array.c array.c -o test-array $(LIBS) -ggdb

clean:
	$(RM) test-array
