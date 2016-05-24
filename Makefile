DEBUG:=
CFLAGS:=-std=c99 -Wall -Wextra -Wshadow -Wno-unused-parameter -O2 -fstack-protector-all $(DEBUG)


.PHONY: clean lint test

networkd: networkd.c parse.c parse.h util.c util.h validate.c validate.h service_exec.c service_exec.h service_write.c service_write.h
	$(CC) $(CFLAGS) -o $@ networkd.c parse.c util.c validate.c service_exec.c service_write.c -lutil

test: parse.c parse.h test.c util.c util.h validate.c validate.h
	$(CC) $(CFLAGS) -o $@ test.c parse.c util.c validate.c
	./test

lint:
	cppcheck -q --std=c99 --enable=style,performance,portability,unusedFunction --inconclusive --error-exitcode=1 ./
	make clean && scan-build make

clean:
	rm -f networkd test
