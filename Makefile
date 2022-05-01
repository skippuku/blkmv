PREFIX := /usr/local
INSTALL_DEST := $(DESTDIR)$(PREFIX)/bin/blkmv

CFLAGS += -std=gnu99 -Wall

all: r_blkmv

debug: db_blkmv
release: r_blkmv

db_blkmv: blkmv.c
	$(CC) $< $(CFLAGS) -g -o $@


r_blkmv: blkmv.c
	$(CC) $< $(CFLAGS) -O2 -s -o $@

clean:
	rm db_blkmv
	rm r_blkmv

install: r_blkmv
	cp -f r_blkmv $(INSTALL_DEST)
	chmod 755 $(INSTALL_DEST)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/blkmv
