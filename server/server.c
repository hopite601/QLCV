#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>

#include "logger.h"
#include "database.h" 
#include "protocol.h"   
#include "handlers.h" 

#define PORT 5555
#define BUF_SIZE 1024
#define MAX_CLIENTS FD_SETSIZE

int main() {
    int listen_fd, new_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addrlen;
    char buf[BUF_SIZE];

    fd_set master_set, read_fds;
    int fd_max;

    printf("===== PROJECT SERVER STARTING =====\n");

    // 1. Init logger
    init_logger("server.log");
    write_log("[SERVER] Starting...");

    // 2. Init database
    if (!db_init("project.db")) {
        write_log("[ERROR] Cannot initialize database.");
        printf("DB init failed!\n");
        return 1;
    }
    write_log("[DB] Database initialized.");

    // 3. Create socket
    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    // Avoid "Address already in use"
    int yes = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
        perror("setsockopt");
        exit(1);
    }

    // 4. Bind
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    memset(&(server_addr.sin_zero), '\0', 8);

    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        exit(1);
    }

    // 5. Listen
    if (listen(listen_fd, 10) == -1) {
        perror("listen");
        exit(1);
    }

    // 6. Init fd sets
    FD_ZERO(&master_set);
    FD_ZERO(&read_fds);

    FD_SET(listen_fd, &master_set);
    fd_max = listen_fd;

    printf("Server listening on port %d...\n", PORT);
    write_log("[SERVER] Listening on port %d", PORT);

    // 7. Main loop
    while (1) {
        read_fds = master_set;

        if (select(fd_max + 1, &read_fds, NULL, NULL, NULL) == -1) {
            perror("select");
            break;
        }

        // fds loop
        for (int i = 0; i <= fd_max; i++) {
            if (FD_ISSET(i, &read_fds)) {

                // new connect
                if (i == listen_fd) {
                    addrlen = sizeof(client_addr);
                    new_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &addrlen);
                    if (new_fd == -1) {
                        perror("accept");
                    } else {
                        FD_SET(new_fd, &master_set);
                        if (new_fd > fd_max) fd_max = new_fd;

                        write_log("[SERVER] New connection from %s on socket %d",
                                  inet_ntoa(client_addr.sin_addr), new_fd);

                        printf("New connection from %s on socket %d\n",
                               inet_ntoa(client_addr.sin_addr), new_fd);
                    }
                }

                // du lieu tu client
                else {
                    int nbytes = recv(i, buf, sizeof(buf) - 1, 0);

                    if (nbytes <= 0) {
                        // ngat clinet
                        if (nbytes == 0) {
                            write_log("[SERVER] socket %d disconnected", i);
                            printf("Socket %d disconnected\n", i);
                        } else {
                            perror("recv");
                        }

                        close(i);
                        FD_CLR(i, &master_set);
                    }

                    else {
                        buf[nbytes] = '\0';
                        printf("[fd=%d] %s\n", i, buf);
                        write_log("[RECV fd=%d] %s", i, buf);

                        // xu ly command
                        process_command(i, buf);
                    }
                }
            }
        }
    }

    close(listen_fd);
    close_logger();
    return 0;
}
