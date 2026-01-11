#include "log.h"
#include <stdio.h>
#include <time.h>

// File log toàn cục, mở ở chế độ append
static FILE *log_file = NULL;

// Khởi tạo logging: mở file log để ghi nối (a = append)
// - filepath: đường dẫn file log (vd: "server.log")
// - Nếu fopen thất bại, log_file sẽ NULL và các lệnh log_message sẽ bị bỏ qua.
void log_init(const char *filepath) {
    log_file = fopen(filepath, "a");
}

// Ghi 1 dòng log với prefix + message, kèm timestamp [DD-MM HH:MM:SS]
// - prefix: loại log (vd "RECV", "SEND", "ERROR")
// - msg: nội dung log (đã là chuỗi hoàn chỉnh, không cần \n ở cuối)
void log_message(const char *prefix, const char *msg) {
    if (!log_file) return;   // chưa init hoặc mở file thất bại -> bỏ qua

    time_t now = time(NULL);         // lấy thời gian hiện tại dạng time_t
    struct tm *t = localtime(&now);  // chuyển sang giờ local (tm_mday, tm_mon,...)

    fprintf(log_file, "[%02d-%02d %02d:%02d:%02d] %s: %s\n",
            t->tm_mday,        // ngày
            t->tm_mon + 1,     // tháng (0-11) -> +1
            t->tm_hour,
            t->tm_min,
            t->tm_sec,
            prefix ? prefix : "",
            msg    ? msg    : "");
    fflush(log_file);                // flush ngay để không bị mất log nếu crash
}
