
PREFIX = /usr/local
INSTALL_DEST = ${DESTDIR}${PREFIX}/bin/blkmv

CC = gcc -std=gnu99

default: debug

debug: blkmv.c
	${CC} blkmv.c -g -o db_blkmv

release: r_blkmv

r_blkmv: blkmv.c
	${CC} blkmv.c -Os -o r_blkmv

clean:
	rm db_blkmv
	rm r_blkmv

install: r_blkmv
	cp -f r_blkmv ${INSTALL_DEST}
	chmod 755 ${INSTALL_DEST}

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/blkmv

