CC = gcc
CFLAGS = -lpthread -Wall -pedantic --std=gnu99 -g
.DEFAULT_GOAL := 2310depot

2310depot: depot.c depot.h
		$(CC) $(CFLAGS) -o 2310depot depot.c
