CFLAGS += -std=c99 -Wall -O2 -ObjC
LDFLAGS += -framework CoreServices -framework Foundation

dskill: dskill.c
	cc $< $(CFLAGS) $(LDFLAGS) -o $@

.PHONY: run
run: dskill
	./$<

.PHONY: install
install: dskill
	install $< /usr/local/bin/$<

.PHONY: clean
clean:
	rm -rf dskill
