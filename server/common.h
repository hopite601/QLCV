#ifndef COMMON_H
#define COMMON_H
#define SERVER_PORT 9000
#define MAX_CLIENT 100
#define BUF_SIZE 2048
typedef enum { TASK_TODO=0, TASK_DOING=1, TASK_DONE=2 } TaskStatus;
typedef struct { int code; char message[256]; } Response;
#endif
