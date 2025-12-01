// handlers.c
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "handlers.h"
#include "database.h"

void handle_register(int fd, char *username, char *password)
{
    if (!username || !password) {
        const char *msg = "ERR Missing username/password\n";
        send(fd, msg, strlen(msg), 0);
        return;
    }

    int ok = db_create_user(username, password);
    if (ok) {
        const char *msg = "OK REGISTER\n";
        send(fd, msg, strlen(msg), 0);
    } else {
        const char *msg = "ERR Username exists\n";
        send(fd, msg, strlen(msg), 0);
    }
}

void handle_login(int fd, char *username, char *password)
{
    if (!username || !password) {
        const char *msg = "ERR Missing username/password\n";
        send(fd, msg, strlen(msg), 0);
        return;
    }

    int user_id = -1;
    int ok = db_check_login(username, password, &user_id);

    if (ok) {
        char buf[128];
        snprintf(buf, sizeof(buf), "OK LOGIN user_id=%d\n", user_id);
        send(fd, buf, strlen(buf), 0);
    } else {
        const char *msg = "ERR Invalid username/password\n";
        send(fd, msg, strlen(msg), 0);
    }
}
