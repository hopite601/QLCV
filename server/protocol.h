// protocol.h
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>

typedef enum {
    CMD_HELP,
    CMD_PING,
    CMD_REGISTER,
    CMD_LOGIN,
    CMD_CREATE_PROJECT,
    CMD_INVITE_MEMBER,
    CMD_CREATE_TASK,
    CMD_UPDATE_TASK_STATUS,
    CMD_ADD_COMMENT,
    CMD_GET_GANTT,
    CMD_CHAT,
    CMD_UNKNOWN
} CommandType;

typedef struct {
    CommandType type;
    // Con trỏ tới vùng buffer gốc (không copy) để tiết kiệm
    const char *arg1;
    const char *arg2;
    const char *arg3;
    const char *rest; // phần còn lại (tiêu đề, nội dung dài…)
} Command;

// Chuẩn hoá dòng vào: trim, bỏ \r\n cuối
void proto_sanitize_line(char *buf, int *len);

// Parse 1 dòng lệnh thành Command
Command proto_parse(const char *line);

// Tạo chuỗi HELP
void proto_make_help(char *out, size_t outsz);
void process_command(int fd, char *line);

#endif
