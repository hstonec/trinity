CC=cc

sws: main.c net.o jstring.o
	$(CC) -Wall -g -o sws main.c net.o jstring.o \
	-lbsd

net.o: net.c net.h sws.h macros.h
	$(CC) -Wall -g -c net.c

jstring.o: jstring.c jstring.h
	$(CC) -Wall -g -c jstring.c

.PHONY: clean
clean:
	-rm sws net.o jstring.o