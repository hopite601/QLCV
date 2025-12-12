#include "handler.h"
#include "protocol.h"
#include "db.h"
#include "log.h"
#include "common.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>

static void send_response(int sockfd, int code, const char *msg) {
    char buf[BUF_SIZE];
    snprintf(buf, sizeof(buf), "%d|%s\n", code, msg);
    send(sockfd, buf, strlen(buf), 0);
    log_message("SEND", buf);
}

// Hàm cắt kí tự \r, \n, space ở cuối chuỗi
static void trim_trailing(char *s) {
    int len = strlen(s);
    while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' || s[len - 1] == ' ')) {
        s[len - 1] = '\0';
        len--;
    }
}

void *client_handler(void *arg) {
    ClientInfo *ci = (ClientInfo *)arg;
    char buffer[BUF_SIZE];

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        int n = recv(ci->sockfd, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) break;

        buffer[n] = '\0';
        log_message("RECV", buffer);

        // tách command
        char *cmd = strtok(buffer, "|");
        if (!cmd) continue;

        trim_trailing(cmd);   // RẤT QUAN TRỌNG: bỏ \n, \r, space ở cuối

        // DEBUG: xem chính xác server đang nhận command gì
        printf("[DEBUG] CMD = '%s'\n", cmd);

        /* ==========================
               REGISTER
        ========================== */
        if (strcmp(cmd, CMD_REGISTER) == 0) {
            char *username = strtok(NULL, "|");
            char *password = strtok(NULL, "|\n");

            if (!username || !password) {
                send_response(ci->sockfd, 1, "Invalid REGISTER format");
                continue;
            }

            if (db_register_user(username, password))
                send_response(ci->sockfd, 0, "Register OK");
            else
                send_response(ci->sockfd, 1, "Register failed");
        }

        /* ==========================
                LOGIN
        ========================== */
        else if (strcmp(cmd, CMD_LOGIN) == 0) {
            char *username = strtok(NULL, "|");
            char *password = strtok(NULL, "|\n");
            int uid;

            if (!username || !password) {
                send_response(ci->sockfd, 1, "Invalid LOGIN format");
                continue;
            }

            if (db_auth_user(username, password, &uid)) {
                ci->user_id = uid;
                send_response(ci->sockfd, 0, "Login OK");
            } else {
                send_response(ci->sockfd, 1, "Login failed");
            }
        }

        /* ==========================
             LIST PROJECTS
        ========================== */
        else if (strcmp(cmd, CMD_LIST_PROJECT) == 0) {

            char list[2048] = {0};
            db_list_projects_for_user(ci->user_id, list, sizeof(list));

            if (strlen(list) == 0)
                send_response(ci->sockfd, 0, "No projects");
            else
                send_response(ci->sockfd, 0, list);
        }

        /* ==========================
             CREATE PROJECT
        ========================== */
        else if (strcmp(cmd, CMD_CREATE_PROJECT) == 0) {

            char *project_name = strtok(NULL, "|\n");
            int project_id;

            if (!project_name) {
                send_response(ci->sockfd, 1, "Invalid CREATE_PROJECT format");
                continue;
            }

            if (db_create_project(project_name, ci->user_id, &project_id))
                send_response(ci->sockfd, 0, "Project created");
            else
                send_response(ci->sockfd, 1, "Create project failed");
        }

        /* ==========================
             INVITE MEMBER
        ========================== */
        else if (strcmp(cmd, CMD_INVITE_MEMBER) == 0) {

            char *pid_str = strtok(NULL, "|");
            char *username = strtok(NULL, "|\n");

            if (!pid_str || !username) {
                send_response(ci->sockfd, 1, "Invalid INVITE_MEMBER format");
                continue;
            }

            int uid;
            if (!db_get_user_id(username, &uid)) {
                send_response(ci->sockfd, 1, "User not found");
                continue;
            }

            if (db_invite_member(atoi(pid_str), uid))
                send_response(ci->sockfd, 0, "Member invited");
            else
                send_response(ci->sockfd, 1, "Invite failed");
        }

        /* ==========================
              CREATE TASK
        ========================== */
        else if (strcmp(cmd, CMD_CREATE_TASK) == 0) {

            char *pid_str = strtok(NULL, "|");
            char *title   = strtok(NULL, "|");
            char *desc    = strtok(NULL, "|\n");

            if (!pid_str || !title || !desc) {
                send_response(ci->sockfd, 1, "Invalid CREATE_TASK format");
                continue;
            }

            int task_id;
            if (db_create_task(atoi(pid_str), title, desc, &task_id))
                send_response(ci->sockfd, 0, "Task created");
            else
                send_response(ci->sockfd, 1, "Create task failed");
        }

        /* ==========================
             LIST TASKS IN PROJECT
        ========================== */
        else if (strcmp(cmd, CMD_LIST_TASK) == 0) {

            char *pid_str = strtok(NULL, "|\n");
            if (!pid_str) {
                send_response(ci->sockfd, 1, "Invalid LIST_TASK format");
                continue;
            }

            char list[4096] = {0};
            db_list_tasks_in_project(atoi(pid_str), list, sizeof(list));

            if (strlen(list) == 0)
                send_response(ci->sockfd, 0, "No tasks");
            else
                send_response(ci->sockfd, 0, list);
        }

        /* ==========================
                ASSIGN TASK
        ========================== */
        else if (strcmp(cmd, CMD_ASSIGN_TASK) == 0) {

            char *taskID_str = strtok(NULL, "|");
            char *username   = strtok(NULL, "|\n");

            if (!taskID_str || !username) {
                send_response(ci->sockfd, 1, "Invalid ASSIGN_TASK format");
                continue;
            }

            // Lấy user_id từ username
            int uid;
            if (!db_get_user_id(username, &uid)) {
                send_response(ci->sockfd, 1, "User not found");
                continue;
            }

            // Assign đúng user_id lấy được từ username
            if (db_assign_task(atoi(taskID_str), uid))
                send_response(ci->sockfd, 0, "Task assigned");
            else
                send_response(ci->sockfd, 1, "Assign failed");
        }


        /* ==========================
              UNKNOWN COMMAND
        ========================== */
        else {
            send_response(ci->sockfd, 1, "Unknown command");
        }
    }

    close(ci->sockfd);
    free(ci);
    return NULL;
}
