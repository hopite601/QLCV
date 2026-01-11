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
    // Khởi tạo SQLite DB (tạo bảng nếu cần)
    if (!db_init("db/database.db")) {
        fprintf(stderr, "Init DB failed\n");
        return 1;
    }

    // Mở file log/server.log để ghi log
    log_init("log/server.log");

    // Tạo socket TCP (IPv4, stream)
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("socket");
        return 1;
    }

    // Cấu hình địa chỉ server: lắng nghe trên mọi IP (0.0.0.0) và SERVER_PORT
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family      = AF_INET;
    serv_addr.sin_port        = htons(SERVER_PORT);
    serv_addr.sin_addr.s_addr = INADDR_ANY;   // listen all interfaces

    // bind socket với địa chỉ trên
    if (bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        return 1;
    }

    // đưa socket vào trạng thái lắng nghe, cho phép tối đa MAX_CLIENT backlog
    if (listen(listenfd, MAX_CLIENT) < 0) {
        perror("listen");
        return 1;
    }

    printf("Server listening on port %d...\n", SERVER_PORT);

    // Vòng lặp accept vô hạn: mỗi connection => tạo 1 thread client_handler
    while (1) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int connfd = accept(listenfd,
                            (struct sockaddr*)&cli_addr,
                            &cli_len);
        if (connfd < 0) {
            perror("accept");
            continue;   // lỗi tạm thời -> accept tiếp
        }

        // Cấp phát ClientInfo cho connection mới
        ClientInfo *ci = malloc(sizeof(ClientInfo));
        ci->sockfd = connfd;
        ci->user_id = -1;   // chưa login

        // Tạo thread xử lý riêng cho client này
        pthread_t tid;
        pthread_create(&tid, NULL, client_handler, ci);
        pthread_detach(tid);   // thread tự dọn dẹp sau khi kết thúc
    }

    // (Thực tế sẽ không tới đây vì while(1), nhưng để đầy đủ)
    db_close();
    close(listenfd);
    return 0;
}
