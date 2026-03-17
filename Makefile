DEBUG ?= 1
CC=gcc
LIBS=`pkg-config --libs memarena openssl mariadb` -O0 -fno-omit-frame-pointer -ggdb

ifeq ($(DEBUG),1)
CFLAGS=`pkg-config --cflags memarena openssl mariadb` -ggdb -Wall -Wextra -pedantic -O0 -fno-omit-frame-pointer
else
CFLAGS=`pkg-config --cflags memarena openssl mariadb` -O2 -DNDEBUG -march=native
endif
SRCFILES=src/main.c src/array.c
OBJFILES=$(addprefix build/, $(addsuffix .o,$(basename $(notdir $(SRCFILES)))))
RM=rm -Rf
NAME=quaerimus
AR=ar
DOXYGEN=/usr/local/bin/doxygen

all: $(NAME)

$(NAME): $(OBJFILES) build/$(NAME).a
	$(CC) $^ -o $(NAME) $(LIBS)

build/$(NAME).a: build/quaerimus.o build/array.o
	$(AR) rcs $@ $^

build/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean doc
clean:
	$(RM) $(wildcard $(OBJFILES) $(NAME)) build/$(NAME).a vgcore.*

DIR=$(shell basename $(CURDIR))
OUTDIR=../Web/$(DIR).doc/
doc:
	@mkdir -p $(OUTDIR)

	# Dynamically build list of all git submodules (including nested)
	touch Doxyfile.tmp
	SUBMODULES=$$(git submodule --quiet foreach --recursive 'echo $$sm_path' 2>/dev/null || true); \
	if [ -n "$$SUBMODULES" ]; then \
		EXCLUDES=$$(echo "$$SUBMODULES" | sed 's|^|*/|; s|$$|/*|' | tr '\n' ' '); \
		echo "EXCLUDE_PATTERNS += $$EXCLUDES" >> Doxyfile.tmp; \
	fi

	# Generate final Doxyfile with dynamic excludes
	cat Doxyfile Doxyfile.tmp > Doxyfile.tmp2 
	DIR=$(DIR) $(DOXYGEN) Doxyfile.tmp2

	for xml in $(OUTDIR)/xml/*.xml; do \
		[ -f "$$xml" ] || continue; \
		echo "  Processing $$xml"; \
		doxy2man --novalidate --pkg "Quaerimus Manual" --short-pkg Quaerimus --section 3 --out ~/.man/man3/ "$$xml"; \
	done; \
	
	rm -f Doxyfile.tmp
	rm -f Doxyfile.tmp2

	@echo "Documentation successfully generated in ../Web/$(DIR).doc/"
	@echo "Open: ../Web/$(DIR).doc/index.html"
