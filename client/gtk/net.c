#include "net.h"
#include "../common.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// Mutex toàn cục để serialize mọi lần gửi/nhận qua 1 socket
// -> tránh trường hợp nhiều thread cùng send/recv chồng lên nhau.
static pthread_mutex_t g_net_lock = PTHREAD_MUTEX_INITIALIZER;

// Server format: "<code>|<payload>\n"
// - code: ký tự số '0'..'9' ở đầu dòng (ví dụ '0' = success, '1' = error...)
// - sau đó là '|' rồi đến payload (có thể là nhiều dòng), kết thúc bằng '\n'
static int parse_server_line(const char *line,
                             char *out_payload, size_t out_sz) {
    if (!line || !*line) {
        // nếu server không gửi gì, payload = "" và code default = 1 (lỗi)
        if (out_payload && out_sz) out_payload[0] = '\0';
        return 1;
    }

    // lấy code từ ký tự đầu (char digit -> int)
    int code = line[0] - '0';

    // tìm dấu '|' phân tách code và payload
    const char *p = strchr(line, '|');
    if (!p) {
        // không có '|': coi như toàn bộ line là payload thô
        if (out_payload && out_sz) {
            strncpy(out_payload, line, out_sz - 1);
            out_payload[out_sz - 1] = '\0';
        }
        return code;
    }

    p++; // bỏ qua '|', trỏ vào phần payload
    if (out_payload && out_sz) {
        // copy payload vào out_payload (cắt theo out_sz)
        strncpy(out_payload, p, out_sz - 1);
        out_payload[out_sz - 1] = '\0';

        // trim \n / \r ở cuối
        size_t len = strlen(out_payload);
        while (len > 0 &&
               (out_payload[len-1] == '\n' ||
                out_payload[len-1] == '\r')) {
            out_payload[len-1] = '\0';
            len--;
        }
    }
    return code;
}

// Hàm request đồng bộ: gửi 1 dòng lệnh và chờ 1 response
// - line: chuỗi đã có '\n' ở cuối (ví dụ "LOGIN|u|p\n")
// - Trả về: code (0 = OK, !=0 = lỗi theo protocol hoặc lỗi network)
int net_request(int sockfd,
                const char *line,
                char *out_payload, size_t out_sz) {
    if (!line) return 1;

    // Khoá mutex để chỉ 1 thread được send/recv tại 1 thời điểm
    pthread_mutex_lock(&g_net_lock);

    // send toàn bộ line (blocking)
    if (send(sockfd, line, strlen(line), 0) <= 0) {
        pthread_mutex_unlock(&g_net_lock);
        return 1; // lỗi gửi
    }

    // recv 1 lần (giả sử server trả vừa trong 1 gói / 1 recv)
    // (nếu protocol dài hơn, cần loop recv tới khi gặp '\n')
    char buf[BUF_SIZE];
    int n = recv(sockfd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        pthread_mutex_unlock(&g_net_lock);
        return 1; // lỗi nhận
    }
    buf[n] = '\0';

    // parse code|payload từ response
    int code = parse_server_line(buf, out_payload, out_sz);

    pthread_mutex_unlock(&g_net_lock);
    return code;
}
