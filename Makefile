CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -O3
LDFLAGS = -lgmp
TARGETS = lcs35

all: $(TARGETS)

lcs35: lcs35.c
	@# MinGW wants source files before linker flags
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

clean:

destroy: clean
	rm -f $(TARGETS)

rebuild: destroy all

.PHONY: all clean destroy rebuild
