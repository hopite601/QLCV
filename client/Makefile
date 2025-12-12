CC=gcc
CFLAGS=-Wall

SRCS=client.c ui.c
OBJS=$(SRCS:.c=.o)

all: client

client: $(OBJS)
	$(CC) $(CFLAGS) -o client $(OBJS)

clean:
	rm -f *.o client
