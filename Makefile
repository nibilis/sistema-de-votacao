CC=gcc
CFLAGS=-Wall -pthread -O2

all: server client

server: server.c
	$(CC) $(CFLAGS) -o server server.c

client: client.c
	$(CC) $(CFLAGS) -o client client.c

clean:
	rm -f server client resultado_final.txt eleicao.log
