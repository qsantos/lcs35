CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -O3
LDFLAGS = -shared -fPIC -lgmp
TARGETS = square.so square_py2.so square_py3.so

all: $(TARGETS)

%.so: %.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

%_py2.so: %_ext.c %.c
	$(CC) $(CFLAGS) $$(pkg-config --cflags python2) $(LDFLAGS) $< -o $@

%_py3.so: %_ext.c %.c
	$(CC) $(CFLAGS) $$(pkg-config --cflags python3) $(LDFLAGS) $< -o $@

clean:

destroy: clean
	rm -f $(TARGETS)

rebuild: destroy all

.PHONY: all clean destroy rebuild
