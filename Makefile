CFLAGS += -std=c99 -Wall -O2 -ObjC
LDFLAGS += -framework CoreServices -framework Foundation
PREFIX ?= /usr/local

dskill: dskill.c
	cc $< $(CFLAGS) $(LDFLAGS) -o $@

$(PREFIX)/bin/dskill: dskill
	install $< $@

.PHONY: run
run: dskill
	./$<

.PHONY: install
install: $(PREFIX)/bin/dskill

.PHONY: clean
clean:
	rm -rf dskill
