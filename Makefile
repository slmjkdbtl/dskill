CC ?= cc
SRC = dskill.c
BIN = dskill
CFLAGS += -std=c99 -Wall -O2
LDFLAGS += -framework CoreServices
PREFIX ?= /usr/local

$(BIN): $(SRC)
	$(CC) $< $(CFLAGS) $(LDFLAGS) -o $@

$(PREFIX)/bin/$(BIN): $(BIN)
	install $< $@

.PHONY: run
run: $(BIN)
	./$< $(ARGS)

.PHONY: install
install: $(PREFIX)/bin/$(BIN)

.PHONY: clean
clean:
	rm -f $(BIN)
