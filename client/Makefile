# ===== GTK client =====
GTK_CFLAGS = $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS   = $(shell pkg-config --libs gtk+-3.0)

# build đúng file bạn đang sửa: gtk/gtk_client.c
GTK_SRCS = gtk/gtk_client.c gtk/net.c
GTK_OBJS = $(GTK_SRCS:.c=.o)

all: client gtk_client

# ---- CLI client ----
client: $(OBJS)
	$(CC) $(CFLAGS) -o client $(OBJS)

# ---- GTK client ----
gtk_client: ui.o $(GTK_OBJS)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) \
		-o gtk_client ui.o $(GTK_OBJS) \
		$(GTK_LIBS) -pthread

%.o: %.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -c $< -o $@

gtk/%.o: gtk/%.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -c $< -o $@

clean:
	rm -f *.o gtk/*.o client gtk_client
