CC?=gcc
CFLAGS+=-std=gnu99 -O2 -Wall

all: nvimrclient nvimrserver

nvimrclient: nvimrclient.c
	$(CC) $(CFLAGS) nvimrclient.c -o nvimrclient

nvimrserver: nvimrserver.c
	$(CC) $(CFLAGS) nvimrserver.c -o nvimrserver

clean:

