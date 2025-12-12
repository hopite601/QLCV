CC=gcc
CFLAGS=-Wall -pthread
LIBS=-lsqlite3

SRCS=server.c handler.c db.c log.c
OBJS=$(SRCS:.c=.o)

all: server

server: $(OBJS)
	$(CC) $(CFLAGS) -o server $(OBJS) $(LIBS)

clean:
	rm -f *.o server
