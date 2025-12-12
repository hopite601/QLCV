#ifndef HANDLER_H
#define HANDLER_H

typedef struct {
    int sockfd;
    int user_id;
} ClientInfo;

void *client_handler(void *arg);

#endif
