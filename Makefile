DEBUG ?= 1
CC=gcc
LIBS=`pkg-config --libs memarena openssl mariadb`

ifeq ($(DEBUG),1)
CFLAGS=`pkg-config --cflags memarena openssl mariadb` -ggdb -Wall -Wextra -pedantic
else
CFLAGS=`pkg-config --cflags memarena openssl mariadb` -O2 -DNDEBUG -march=native
endif
SRCFILES=$(wildcard src/*.c)
OBJFILES=$(addprefix build/, $(addsuffix .o,$(basename $(notdir $(SRCFILES)))))
RM=rm -Rf
NAME=quaerimus

all: $(NAME)

$(NAME): $(OBJFILES)
	$(CC) $^ -o $(NAME) $(LIBS)

build/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(wildcard $(OBJFILES) $(NAME)) vgcore.*
