// logger.c ghi lại nhật ký log 
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include "logger.h"

static FILE *log_file = NULL;

void init_logger(const char *filename) {
    log_file = fopen(filename, "a");
    if (!log_file) {
        perror("fopen log");
    }
}

void write_log(const char *fmt, ...) {
    if (!log_file) return;

    // time
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    fprintf(log_file, "%04d-%02d-%02d %02d:%02d:%02d ",
            t->tm_year + 1900,
            t->tm_mon + 1,
            t->tm_mday,
            t->tm_hour,
            t->tm_min,
            t->tm_sec);

    // message
    va_list args;
    va_start(args, fmt);
    vfprintf(log_file, fmt, args);
    va_end(args);

    fprintf(log_file, "\n");
    fflush(log_file);
}

void close_logger() {
    if (log_file) fclose(log_file);
}
