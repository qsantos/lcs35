CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Wvla -O3
LDFLAGS = -O3 -lgmp
TARGETS = lcs35

all: $(TARGETS)

lcs35: lcs35.o session.o time.o util.o
	@# MinGW wants source files before linker flags
	$(CC) $^ $(LDFLAGS) -o $@

-include $(wildcard *.d)
%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<
	$(CC) $(CFLAGS) $(CPPFLAGS) -MM -o $*.d $<

clean:
	rm -f *.o *.d

check:
	clang-tidy *.h *.c

run: all
	./$(TARGETS)

.PHONY: all clean run
