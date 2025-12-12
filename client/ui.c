#include "ui.h"
#include "protocol.h"
#include "common.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

static void send_cmd(int sockfd, const char *cmd_line) {
    send(sockfd, cmd_line, strlen(cmd_line), 0);
}

static int recv_response_and_return_code(int sockfd, char *outBuf) {
    char buf[BUF_SIZE];
    int n = recv(sockfd, buf, sizeof(buf) - 1, 0);

    if (n <= 0) {
        printf("Server disconnected.\n");
        return -1;
    }

    buf[n] = '\0';
    printf("Server:\n%s\n", buf);

    if (outBuf) strcpy(outBuf, buf);

    return buf[0] - '0';   // '0' or '1'
}

static void menu_after_login(int sockfd) {
    int choice;
    char cmd[BUF_SIZE];

    while (1) {
        printf("\n--- PROJECT MANAGEMENT MENU ---\n");
        printf("1. Create project\n");
        printf("2. Invite member\n");
        printf("3. Create task\n");
        printf("4. Assign task\n");
        printf("5. Update task status\n");
        printf("6. Add comment\n");
        printf("7. Add attachment\n");
        printf("8. View Gantt\n");
        printf("9. Send chat message\n");
        printf("10. List my projects\n");
        printf("11. List tasks in project\n");
        printf("0. Logout\n");

        printf("Choice: ");
        scanf("%d", &choice);
        getchar();

        if (choice == 0) break;

        char p1[256], p2[256], p3[256];

        switch (choice) {

        case 1: // CREATE PROJECT
            printf("Project name: ");
            fgets(p1, sizeof(p1), stdin);
            p1[strcspn(p1, "\n")] = 0;

            snprintf(cmd, sizeof(cmd), "%s|%s\n", CMD_CREATE_PROJECT, p1);
            break;

        case 2: // INVITE MEMBER
            printf("\nYour projects (use ID number):\n");
            send_cmd(sockfd, CMD_LIST_PROJECT "\n");
            recv_response_and_return_code(sockfd, NULL);

            printf("Enter Project ID (number): ");
            fgets(p1, sizeof(p1), stdin);
            p1[strcspn(p1, "\n")] = 0;

            printf("Username to invite: ");
            fgets(p2, sizeof(p2), stdin);
            p2[strcspn(p2, "\n")] = 0;

            snprintf(cmd, sizeof(cmd), "%s|%s|%s\n",
                     CMD_INVITE_MEMBER, p1, p2);
            break;

        case 3: // CREATE TASK
            printf("\nYour projects (use ID number):\n");
            send_cmd(sockfd, CMD_LIST_PROJECT "\n");
            recv_response_and_return_code(sockfd, NULL);

            printf("Project ID (number): ");
            fgets(p1, sizeof(p1), stdin);
            p1[strcspn(p1, "\n")] = 0;

            printf("Task title: ");
            fgets(p2, sizeof(p2), stdin);
            p2[strcspn(p2, "\n")] = 0;

            printf("Description: ");
            fgets(p3, sizeof(p3), stdin);
            p3[strcspn(p3, "\n")] = 0;

            snprintf(cmd, sizeof(cmd), "%s|%s|%s|%s\n",
                     CMD_CREATE_TASK, p1, p2, p3);
            break;

        case 4: // ASSIGN TASK
            printf("\nEnter Project ID (number) to view tasks: ");
            fgets(p1, sizeof(p1), stdin);
            p1[strcspn(p1, "\n")] = 0;

            snprintf(cmd, sizeof(cmd), "%s|%s\n", CMD_LIST_TASK, p1);
            send_cmd(sockfd, cmd);
            recv_response_and_return_code(sockfd, NULL);

            printf("\nEnter Task ID to assign: ");
            fgets(p2, sizeof(p2), stdin);
            p2[strcspn(p2, "\n")] = 0;

            printf("Enter username to assign: ");
            fgets(p3, sizeof(p3), stdin);
            p3[strcspn(p3, "\n")] = 0;

            snprintf(cmd, sizeof(cmd), "%s|%s|%s\n",
                     CMD_ASSIGN_TASK, p2, p3);
            break;

        case 10: // LIST MY PROJECTS
            send_cmd(sockfd, CMD_LIST_PROJECT "\n");
            recv_response_and_return_code(sockfd, NULL);
            continue;

        case 11: // LIST TASKS IN PROJECT
            printf("\nYour projects (use ID number):\n");
            send_cmd(sockfd, CMD_LIST_PROJECT "\n");
            recv_response_and_return_code(sockfd, NULL);

            printf("Enter Project ID (number): ");
            fgets(p1, sizeof(p1), stdin);
            p1[strcspn(p1, "\n")] = 0;

            snprintf(cmd, sizeof(cmd), "%s|%s\n", CMD_LIST_TASK, p1);
            send_cmd(sockfd, cmd);
            recv_response_and_return_code(sockfd, NULL);
            continue;

        default:
            printf("Feature not implemented yet.\n");
            continue;
        }

        send_cmd(sockfd, cmd);
        recv_response_and_return_code(sockfd, NULL);
    }
}

int main_menu(int sockfd) {
    int choice;
    char username[64], password[64], cmd[BUF_SIZE];
    char response[BUF_SIZE];

    while (1) {
        printf("\n--- LOGIN MENU ---\n");
        printf("1. Register\n");
        printf("2. Login\n");
        printf("0. Exit\n");

        printf("Choice: ");
        scanf("%d", &choice);
        getchar();

        if (choice == 0) return 0;

        printf("Username: ");
        fgets(username, sizeof(username), stdin);
        username[strcspn(username, "\n")] = 0;

        printf("Password: ");
        fgets(password, sizeof(password), stdin);
        password[strcspn(password, "\n")] = 0;

        if (choice == 1)
            snprintf(cmd, sizeof(cmd), "%s|%s|%s\n",
                     CMD_REGISTER, username, password);
        else
            snprintf(cmd, sizeof(cmd), "%s|%s|%s\n",
                     CMD_LOGIN, username, password);

        send_cmd(sockfd, cmd);
        int code = recv_response_and_return_code(sockfd, response);

        if (choice == 2 && code == 0) {
            menu_after_login(sockfd);
        } else if (choice == 2 && code != 0) {
            printf("Login failed. Try again.\n");
        }
    }

    return 0;
}
