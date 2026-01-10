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
// return: 1=ok, -1=already member, 0=fail
int db_invite_member(int project_id, int user_id);

// permissions/helpers
int db_is_project_owner(int project_id, int user_id);
int db_is_project_member(int project_id, int user_id);
int db_get_task_project_id(int task_id, int *project_id);
int db_get_task_assignee_id(int task_id, int *assignee_id);

// create task with full fields (mandatory assignee & dates)
int db_create_task_full(int project_id, const char *title, const char *desc,
                        int assignee_id, const char *start_date, const char *end_date,
                        int *task_id);

int db_create_task(int project_id, const char *title, const char *desc, int *task_id);
int db_list_tasks_in_project(int project_id, char *out, int out_size);
int db_assign_task(int task_id, int user_id);

// Extended features
int db_update_task_status(int task_id, const char *status);
int db_update_task_progress(int task_id, int progress);
int db_set_task_dates(int task_id, const char *start_date, const char *end_date);
int db_get_task_detail(int task_id, char *out, int out_size);
int db_list_tasks_gantt(int project_id, char *out, int out_size);

int db_add_comment(int task_id, int user_id, const char *content);
int db_list_comments(int task_id, char *out, int out_size);

int db_add_attachment(int task_id, const char *filename, const char *filepath);
int db_list_attachments(int task_id, char *out, int out_size);

int db_add_chat(int project_id, int user_id, const char *content);
int db_list_chat(int project_id, int after_id, char *out, int out_size);

// Reports
int db_add_report(int project_id, int created_by, const char *title, const char *desc, int *report_id);
int db_list_reports(int project_id, char *out, int out_size);
int db_get_report(int report_id, char *out, int out_size);
int db_get_report_project_id(int report_id, int *project_id);
int db_update_report(int report_id, const char *title, const char *desc);
int db_delete_report(int report_id);

// Report comments
int db_add_report_comment(int report_id, int user_id, const char *content);
int db_list_report_comments(int report_id, char *out, int out_size);
int db_delete_report_comment(int comment_id);

// Report files
int db_add_report_file(int report_id, const char *filename, const char *filepath);
int db_list_report_files(int report_id, char *out, int out_size);
int db_delete_report_file(int file_id);


#endif
