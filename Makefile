CFLAGS = -std=gnu99 -O2 -Wall -I./inc

all:
	make library
	make testapp

library:
	mkdir -p obj

	gcc $(CFLAGS) -c httpserver.c -o obj/httpserver.o
	gcc $(CFLAGS) -c httpsocket.c -o obj/httpsocket.o

	ar -rcs libhttpserver.a obj/httpserver.o obj/httpsocket.o

testapp:
	mkdir -p obj

	gcc $(CFLAGS) -c main.c -o obj/main.o
	gcc -o httpservertest obj/main.o -L. -lhttpserver

clean:
	rm -f obj/*.o libhttpserver.a httpservertest
