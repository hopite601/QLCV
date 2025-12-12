#include "db.h"
#include "log.h"
#include "common.h"
#include "handler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

int main() {
    if (!db_init("db/database.db")) {
        fprintf(stderr, "Init DB failed\n");
        return 1;
    }
    log_init("log/server.log");

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(listenfd, MAX_CLIENT) < 0) {
        perror("listen");
        return 1;
    }

    printf("Server listening on port %d...\n", SERVER_PORT);

    while (1) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int connfd = accept(listenfd, (struct sockaddr*)&cli_addr, &cli_len);
        if (connfd < 0) {
            perror("accept");
            continue;
        }

        ClientInfo *ci = malloc(sizeof(ClientInfo));
        ci->sockfd = connfd;
        ci->user_id = -1;

        pthread_t tid;
        pthread_create(&tid, NULL, client_handler, ci);
        pthread_detach(tid);
    }

    db_close();
    close(listenfd);
    return 0;
}
