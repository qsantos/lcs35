CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Wvla -O3
LDFLAGS = -O3 -lgmp -lpthread -lsqlite3
TARGETS = work validate

all: $(TARGETS)

work: work.o session.o socket.o time.o util.o
	@# MinGW wants source files before linker flags
	$(CC) $^ $(LDFLAGS) -o $@

validate: validate.o session.o util.o
	@# MinGW wants source files before linker flags
	$(CC) $^ $(LDFLAGS) -o $@

-include $(wildcard *.d)
%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<
	@$(CC) $(CFLAGS) $(CPPFLAGS) -MM -MP -o $*.d $<

clean:
	rm -f *.o *.d

check:
	clang-tidy *.h *.c

run: all
	./lcs35

.PHONY: all clean run
