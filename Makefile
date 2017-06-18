CC = gcc
# __USE_MINGW_ANSI_STDIO lets MinGW use the %llu format specifier
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -O3 -D__USE_MINGW_ANSI_STDIO=1
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
