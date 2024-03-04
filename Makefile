LDFLAGS += -framework CoreServices

dskill: dskill.c
	cc $< $(LDFLAGS) -o $@

.PHONY: run
run: dskill
	./$<

.PHONY: install
install: dskill
	install $< /usr/local/bin/$<

.PHONY: clean
clean:
	rm -rf dskill
