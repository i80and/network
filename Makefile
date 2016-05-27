DEBUG:=
CFLAGS:=-std=c99 -Wall -Wextra -Wshadow -Wno-unused-parameter -O2 -fstack-protector-all $(DEBUG)

.PHONY: clean lint fuzz test

networkd: networkd.c flatjson.c flatjson.h util.c util.h validate.c validate.h service_exec.c service_exec.h service_write.c service_write.h
	$(CC) $(CFLAGS) -o $@ networkd.c flatjson.c util.c validate.c service_exec.c service_write.c -lutil

test: test.c flatjson.c flatjson.h util.c util.h validate.c validate.h
	$(CC) $(CFLAGS) -g -o $@ test.c flatjson.c util.c validate.c
	./test

lint:
	cppcheck -q --std=c99 --enable=style,performance,portability,unusedFunction --inconclusive --error-exitcode=1 ./
	make clean && scan-build make
	perlcritic ./lib/network.pl

fuzzer: fuzz.c flatjson.c flatjson.h validate.c validate.h util.c util.h
	AFL_HARDEN=1 afl-clang $(CFLAGS) -o fuzzer fuzz.c flatjson.c validate.c util.c

fuzz: fuzzer
	afl-fuzz -i t/fuzz/in -o t/fuzz/out ./fuzzer

clean:
	rm -f networkd test fuzzer
