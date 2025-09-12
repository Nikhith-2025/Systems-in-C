CC = gcc
CFLAGS = -std=c99 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 \
         -Wall -Wextra -Werror -Wno-unused-parameter -fno-asm
LDFLAGS = -lssl -lcrypto

all: client.out server.out

client.out: client.o
	$(CC) $(CFLAGS) -o client.out client.o $(LDFLAGS)

server.out: server.o
	$(CC) $(CFLAGS) -o server.out server.o $(LDFLAGS)

client.o: client.c sham.h
	$(CC) $(CFLAGS) -c client.c

server.o: server.c sham.h
	$(CC) $(CFLAGS) -c server.c

clean:
	rm -f *.o client.out server.out