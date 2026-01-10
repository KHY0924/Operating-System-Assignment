CC = gcc
CFLAGS = -Wall -pthread -lrt

all: server client

server: server.c common.h
	$(CC) $(CFLAGS) server.c -o server

client: client.c common.h
	$(CC) $(CFLAGS) client.c -o client

clean:
	rm -f server client game.log
