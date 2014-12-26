CC?=gcc
CFLAGS+=-std=gnu99 -O2 -Wall

all: nvimclient nvimserver

nvimclient: nvimclient.c
	$(CC) $(CFLAGS) nvimclient.c -o nvimclient

nvimserver: nvimserver.c
	$(CC) $(CFLAGS) nvimserver.c -o nvimserver

clean:

