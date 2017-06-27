CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Wvla -O3
LDFLAGS = -lgmp
TARGETS = lcs35

all: $(TARGETS)

lcs35: lcs35.c
	@# MinGW wants source files before linker flags
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

clean:

run: all
	./$(TARGETS)

.PHONY: all clean run
