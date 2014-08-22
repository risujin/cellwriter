all: test.o
	gcc `pkg-config --libs glib-2.0 gtk+-2.0` -ggdb -lXtst test.o -o test

clean:
	-rm test test.o

test.o: test.c
	gcc `pkg-config --cflags glib-2.0 gtk+-2.0` -ggdb -Wall -c test.c
