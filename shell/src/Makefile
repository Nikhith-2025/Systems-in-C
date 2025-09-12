CC=gcc
CFLAGS=-std=c99 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -Wall -Wextra -Werror -Wno-unused-parameter -fno-asm -I../include

all: shell.out

shell.out: shell.o input.o parser.o hop.o reveal.o command.o log.o piping.o background.o activities.o ping.o
	$(CC) $(CFLAGS) -o shell.out shell.o input.o parser.o hop.o reveal.o command.o log.o piping.o background.o activities.o ping.o

shell.o: shell.c ../include/input.h ../include/parser.h ../include/hop.h ../include/reveal.h ../include/command.h ../include/log.h ../include/piping.h ../include/background.h ../include/activities.h ../include/ping.h
	$(CC) $(CFLAGS) -c shell.c

input.o: input.c ../include/input.h
	$(CC) $(CFLAGS) -c input.c

parser.o: parser.c ../include/parser.h
	$(CC) $(CFLAGS) -c parser.c

hop.o : hop.c ../include/hop.h
	$(CC) $(CFLAGS) -c hop.c

reveal.o: reveal.c ../include/reveal.h
	$(CC) $(CFLAGS) -c reveal.c
	
command.o: command.c ../include/command.h
	$(CC) $(CFLAGS) -c command.c

log.o: log.c ../include/log.h
	$(CC) $(CFLAGS) -c log.c

piping.o: piping.c ../include/piping.h
	$(CC) $(CFLAGS) -c piping.c

background.o: background.c ../include/background.h ../include/hop.h ../include/reveal.h
	$(CC) $(CFLAGS) -c background.c

activities.o: activities.c ../include/activities.h ../include/hop.h ../include/reveal.h
	$(CC) $(CFLAGS) -c activities.c

ping.o: ping.c ../include/ping.h
	$(CC) $(CFLAGS) -c ping.c

clean:
	rm -f *.o shell.out