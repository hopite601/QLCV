#ifndef DB_H
#define DB_H

#include <sqlite3.h>
#include "common.h"

extern sqlite3 *db;

int db_init(const char *path);
void db_close();

int db_register_user(const char *username, const char *password);
int db_auth_user(const char *username, const char *password, int *user_id);
int db_get_user_id(const char *username, int *user_id);

int db_create_project(const char *name, int owner_id, int *project_id);
int db_list_projects_for_user(int user_id, char *out, int out_size);
int db_invite_member(int project_id, int user_id);

int db_create_task(int project_id, const char *title, const char *desc, int *task_id);
int db_list_tasks_in_project(int project_id, char *out, int out_size);
int db_assign_task(int task_id, int user_id);

#endif
