#include "log.h"
#include <stdio.h>
#include <time.h>

static FILE *log_file = NULL;

void log_init(const char *filepath) {
    log_file = fopen(filepath, "a");
}

void log_message(const char *prefix, const char *msg) {
    if (!log_file) return;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    fprintf(log_file, "[%02d-%02d %02d:%02d:%02d] %s: %s\n",
            t->tm_mday, t->tm_mon + 1,
            t->tm_hour, t->tm_min, t->tm_sec,
            prefix, msg);
    fflush(log_file);
}
