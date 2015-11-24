CC=cc

sws: main.c net.o http.o jstring.o arraylist.o
	$(CC) -Wall -g -o sws main.c net.o http.o jstring.o arraylist.o \
	-lbsd

net.o: net.c net.h sws.h macros.h
	$(CC) -Wall -g -c net.c

http.o: http.c http.h
	$(CC) -Wall -g -c http.c
	
jstring.o: jstring.c jstring.h
	$(CC) -Wall -g -c jstring.c

arraylist.o: arraylist.c arraylist.h
	$(CC) -Wall -g -c arraylist.c

.PHONY: clean
clean:
	-rm sws net.o http.o jstring.o arraylist.o