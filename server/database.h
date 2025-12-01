#ifndef DATABASE_H
#define DATABASE_H

#include <sqlite3.h>

int db_init(const char *filename);
int db_create_user(const char *username, const char *password);
int db_check_login(const char *username, const char *password, int *out_user_id);

#endif
