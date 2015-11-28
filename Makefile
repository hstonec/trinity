CC=cc

sws: main.c net.o cgi.o http_request.o jstring.o arraylist.o
	$(CC) -Wall -g -o sws main.c net.o cgi.o http_request.o jstring.o arraylist.o \
	-lbsd

net.o: net.c net.h sws.h macros.h http.h
	$(CC) -Wall -g -c net.c

cgi.o: cgi.c cgi.h http.h
	$(CC) -Wall -g -c cgi.c
	
http_request.o: http_request.c http.h
	$(CC) -Wall -g -c http_request.c

jstring.o: jstring.c jstring.h
	$(CC) -Wall -g -c jstring.c

arraylist.o: arraylist.c arraylist.h
	$(CC) -Wall -g -c arraylist.c

.PHONY: clean
clean:
	-rm sws net.o cgi.o http_request.o jstring.o arraylist.o
