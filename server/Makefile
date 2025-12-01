CC = gcc
CFLAGS = -Wall -Wextra -O2
LIBS = -lsqlite3

SRC = server.c logger.c handlers.c protocol.c database.c

all: server

server: $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o server $(LIBS)

clean:
	rm -f server *.o server.log
