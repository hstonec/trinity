CC=cc
PROG=sws
CFLAGS=-DLINUX -Wall -g

all: ${PROG}

${PROG}: main.c net.o cgi.o http_request.o http_response.o jstring.o arraylist.o
	$(CC) ${CFLAGS} -o ${PROG} main.c net.o cgi.o http_request.o http_response.o jstring.o arraylist.o \
	-lbsd

net.o: net.c net.h sws.h macros.h http.h
	$(CC) ${CFLAGS} -c net.c

cgi.o: cgi.c cgi.h http.h
	$(CC) ${CFLAGS} -c cgi.c
	
http_request.o: http_request.c http.h
	$(CC) ${CFLAGS} -c http_request.c

http_response.o: http_response.c http.h
	$(CC) ${CFLAGS} -c http_response.c

jstring.o: jstring.c jstring.h
	$(CC) ${CFLAGS} -c jstring.c

arraylist.o: arraylist.c arraylist.h
	$(CC) ${CFLAGS} -c arraylist.c

.PHONY: clean
clean:
	-rm sws net.o cgi.o http_request.o http_response.o jstring.o arraylist.o
