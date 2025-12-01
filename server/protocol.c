// protocol.c
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <sys/socket.h>

#include "protocol.h"
#include "handlers.h"
#include "database.h"
#include "logger.h"

// cắt khoảng trắng 2 đầu + bỏ \r\n
void proto_sanitize_line(char *buf, int *len) {
    int n = *len;
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' || isspace((unsigned char)buf[n-1])))
        n--;
    int i = 0;
    while (i < n && isspace((unsigned char)buf[i])) i++;
    if (i > 0) memmove(buf, buf + i, n - i);
    buf[n - i] = '\0';
    *len = n - i;
}

static int next_token(const char *s, int *start, int *end) {
    int i = *end;
    while (s[i] && isspace((unsigned char)s[i])) i++;
    if (!s[i]) { *start = *end = i; return 0; }
    int j = i;
    while (s[j] && !isspace((unsigned char)s[j])) j++;
    *start = i; *end = j;
    return 1;
}

Command proto_parse(const char *line) {
    Command cmd = { CMD_UNKNOWN, NULL, NULL, NULL, NULL };
    int s=0, e=0;

    // token 1: tên lệnh
    if (!next_token(line, &s, &e)) return cmd;

    #define MATCH(cmdstr, ctype) \
        if (strncasecmp(line+s, cmdstr, (e-s))==0 && (int)strlen(cmdstr)==(e-s)) { cmd.type=ctype; }

    MATCH("HELP",   CMD_HELP)
    else MATCH("PING",   CMD_PING)
    else MATCH("REGISTER",CMD_REGISTER)
    else MATCH("LOGIN",   CMD_LOGIN)
    else MATCH("CREATE_PROJECT", CMD_CREATE_PROJECT)
    else MATCH("INVITE_MEMBER",  CMD_INVITE_MEMBER)
    else MATCH("CREATE_TASK",    CMD_CREATE_TASK)
    else MATCH("UPDATE_TASK_STATUS", CMD_UPDATE_TASK_STATUS)
    else MATCH("ADD_COMMENT",    CMD_ADD_COMMENT)
    else MATCH("GET_GANTT",      CMD_GET_GANTT)
    else MATCH("CHAT",           CMD_CHAT)
    else cmd.type = CMD_UNKNOWN;

    // token 2..4
    if (next_token(line, &s, &e)) cmd.arg1 = line + s;
    if (next_token(line, &s, &e)) cmd.arg2 = line + s;
    if (next_token(line, &s, &e)) cmd.arg3 = line + s;

    // rest (dùng cho text/comment)
    int pos = e;
    while (line[pos] && isspace((unsigned char)line[pos])) pos++;
    if (line[pos]) cmd.rest = line + pos;

    return cmd;
}

void proto_make_help(char *out, size_t outsz) {
    snprintf(out, outsz,
        "OK HELP\n"
        "  PING\n"
        "  HELP\n"
        "  REGISTER <username> <password>\n"
        "  LOGIN <username> <password>\n"
        "  CREATE_PROJECT <name> <description>\n"
        "  INVITE_MEMBER <project_id> <username>\n"
        "  CREATE_TASK <project_id> <title> <assignee>\n"
        "  UPDATE_TASK_STATUS <task_id> <NOT_STARTED|IN_PROGRESS|DONE>\n"
        "  ADD_COMMENT <task_id> <text...>\n"
        "  GET_GANTT <project_id>\n"
        "  CHAT <project_id> <text...>\n");
}

void process_command(int fd, char *line) {

    Command cmd = proto_parse(line);

    switch (cmd.type) {

        case CMD_PING:
            send(fd, "PONG\n", 5, 0);
            break;

        case CMD_HELP: {
            char out[1024];
            proto_make_help(out, sizeof(out));
            send(fd, out, strlen(out), 0);
            break;
        }

        case CMD_REGISTER:
            if (cmd.arg1 && cmd.arg2)
                handle_register(fd, (char*)cmd.arg1, (char*)cmd.arg2);
            else
                send(fd, "ERROR REGISTER_USAGE\n", 22, 0);
            break;

        case CMD_LOGIN:
            if (cmd.arg1 && cmd.arg2)
                handle_login(fd, (char*)cmd.arg1, (char*)cmd.arg2);
            else
                send(fd, "ERROR LOGIN_USAGE\n", 19, 0);
            break;

        default:
            send(fd, "ERROR UNKNOWN_COMMAND\n", 23, 0);
            break;
    }
}
