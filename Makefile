CFLAGS += -std=c99 -Wall -O2 -ObjC
LDFLAGS += -framework CoreServices -framework Foundation
PREFIX ?= /usr/local

dskill: dskill.c
	cc $< $(CFLAGS) $(LDFLAGS) -o $@

.PHONY: run
run: dskill
	./$<

.PHONY: install
install: dskill
	install $< $(PREFIX)/bin/$<

.PHONY: clean
clean:
	rm -rf dskill
