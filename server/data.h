// data.h
#ifndef DATA_H
#define DATA_H

#include <time.h>

typedef enum {
    TASK_NOT_STARTED = 0,
    TASK_IN_PROGRESS = 1,
    TASK_DONE        = 2
} TaskStatus;

typedef struct {
    int   id;
    char  username[64];
    char  password[64];
} User;

typedef struct {
    int  id;
    char name[128];
    char description[256];
    int  owner_id;
} Project;

typedef struct {
    int        id;
    int        project_id;
    char       title[128];
    int        assignee_id;
    time_t     start_date;
    time_t     due_date;
    TaskStatus status;
} Task;

typedef struct {
    int     id;
    int     task_id;
    int     author_id;
    char    content[256];
    char    file_path[256]; // Tuần 2–3 mới dùng
    time_t  created_at;
} Comment;

typedef struct {
    int     id;
    int     project_id;
    int     sender_id;
    char    content[256];
    time_t  created_at;
} ChatMessage;

#endif
