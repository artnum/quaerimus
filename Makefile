DEBUG ?= 1
CC=gcc
LIBS=`pkg-config --libs memarena openssl mariadb`

ifeq ($(DEBUG),1)
CFLAGS=`pkg-config --cflags memarena openssl mariadb` -ggdb -Wall -Wextra -pedantic
else
CFLAGS=`pkg-config --cflags memarena openssl mariadb` -O2 -DNDEBUG -march=native
endif
SRCFILES=src/main.c src/array.c
OBJFILES=$(addprefix build/, $(addsuffix .o,$(basename $(notdir $(SRCFILES)))))
RM=rm -Rf
NAME=quaerimus
AR=ar

all: $(NAME)

$(NAME): $(OBJFILES) build/$(NAME).a
	$(CC) $^ -o $(NAME) $(LIBS)

build/$(NAME).a: build/quaerimus.o build/array.o
	$(AR) rcs $@ $^

build/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(wildcard $(OBJFILES) $(NAME)) build/$(NAME).a vgcore.*
