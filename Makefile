CFLAGS = -Wall -pedantic -g

all: minls minget


minls: minls.o minfs.o
	gcc $(CFLAGS) -o minls minls.o minfs.o

minls.o: minls.c
	gcc $(CFLAGS) -c minls.c


minget: minget.o minfs.o
	gcc $(CFLAGS) -o minget minget.o minfs.o

minget.o: minget.c
	gcc $(CFLAGS) -c minget.c


minfs.o: minfs.c minfs.h
	gcc $(CFLAGS) -c minfs.c minfs.h

clean:
	rm *~

new:
	rm minget minls *~ *.o *.gch
