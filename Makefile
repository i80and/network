DESTDIR:=/usr/local
MANDIR:=$(DESTDIR)/man
DEBUG:=
CFLAGS:=-std=c99 -Wall -Wextra -Wshadow -Wno-unused-parameter -O2 -fstack-protector-all $(DEBUG)

.PHONY: clean lint fuzz test install

CORE_SRC=src/flatjson.c \
         src/util.c \
         src/validate.c
CORE_DEPS=$(CORE_SRC) $(CORE_SRC:%.h=$.c)
SRC=$(CORE_SRC) \
    src/service_write.c \
    src/service_exec.c \
    src/networkd.c
DEPS=$(SRC) $(CORE_DEPS) src/service_write.h src/service_exec.h

networkd: $(DEPS)
	$(CC) $(CFLAGS) -o $@ $(SRC) -lutil

test: t/test.c $(CORE_DEPS)
	$(CC) $(CFLAGS) -o $@ -Isrc/ t/test.c $(CORE_SRC)
	./test

lint:
	cppcheck -q --std=c99 --enable=style,performance,portability,unusedFunction --inconclusive --error-exitcode=1 ./src
	make clean && scan-build make
	perlcritic ./src/network-cli.pl

fuzzer: src/fuzz.c $(CORE_DEPS)
	AFL_HARDEN=1 afl-clang $(CFLAGS) -o $@ src/fuzz.c $(CORE_SRC)

fuzz: fuzzer
	afl-fuzz -i t/fuzz/in -o t/fuzz/out ./fuzzer

install: networkd
	install -m755 networkd $(DESTDIR)/sbin/networkd
	install -m755 src/network-cli.pl $(DESTDIR)/bin/network-cli
	install -m444 networkd.8 $(MANDIR)/man8/networkd.8

clean:
	rm -f networkd test fuzzer
