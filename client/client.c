#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "common.h"
#include "ui.h"

int main() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket creation failed");
        return 1;
    }

    struct sockaddr_in serv;
    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons(SERVER_PORT);
    serv.sin_addr.s_addr = inet_addr("127.0.0.1");

    printf("Connecting to server...\n");

    // In lỗi nếu không connect được (bản cũ không in gì → bạn thấy trống)
    if (connect(sockfd, (struct sockaddr*)&serv, sizeof(serv)) < 0) {
        perror("Cannot connect to server");
        close(sockfd);
        return 1;
    }

    printf("Connected to server!\n");

    // Gọi menu chính
    main_menu(sockfd);

    close(sockfd);
    return 0;
}
