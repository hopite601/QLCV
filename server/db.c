#include "db.h"
#include <stdio.h>
#include <string.h>

sqlite3 *db = NULL;

int db_init(const char *path) {
    if (sqlite3_open(path, &db) != SQLITE_OK) {
        printf("Cannot open DB: %s\n", sqlite3_errmsg(db));
        return 0;
    }

    const char *sql =
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "username TEXT UNIQUE,"
        "password TEXT"
        ");"

        "CREATE TABLE IF NOT EXISTS projects ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT,"
        "owner_id INTEGER"
        ");"

        "CREATE TABLE IF NOT EXISTS project_members ("
        "project_id INTEGER,"
        "user_id INTEGER"
        ");"

        "CREATE TABLE IF NOT EXISTS tasks ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "project_id INTEGER,"
        "title TEXT,"
        "description TEXT,"
        "assignee_id INTEGER"
        ");";

    char *err;
    if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
        printf("DB init error: %s\n", err);
        sqlite3_free(err);
        return 0;
    }

    return 1;
}

void db_close() {
    if (db) sqlite3_close(db);
}

/* =====================================
            USER FUNCTIONS
===================================== */

int db_register_user(const char *username, const char *password) {
    sqlite3_stmt *st;

    sqlite3_prepare_v2(db,
        "INSERT INTO users(username,password) VALUES(?,?)",
        -1, &st, NULL);

    sqlite3_bind_text(st, 1, username, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, password, -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(st);
    sqlite3_finalize(st);

    return rc == SQLITE_DONE;
}

int db_auth_user(const char *username, const char *password, int *user_id) {
    sqlite3_stmt *st;

    sqlite3_prepare_v2(db,
        "SELECT id FROM users WHERE username=? AND password=?",
        -1, &st, NULL);

    sqlite3_bind_text(st, 1, username, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, password, -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        *user_id = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
        return 1;
    }

    sqlite3_finalize(st);
    return 0;
}

int db_get_user_id(const char *username, int *user_id) {
    sqlite3_stmt *st;

    sqlite3_prepare_v2(db,
        "SELECT id FROM users WHERE username=?",
        -1, &st, NULL);

    sqlite3_bind_text(st, 1, username, -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        *user_id = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
        return 1;
    }

    sqlite3_finalize(st);
    return 0;
}

/* =====================================
            PROJECT FUNCTIONS
===================================== */

int db_create_project(const char *name, int owner_id, int *project_id) {
    sqlite3_stmt *st;

    sqlite3_prepare_v2(db,
        "INSERT INTO projects(name, owner_id) VALUES (?, ?)",
        -1, &st, NULL);

    sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, owner_id);

    if (sqlite3_step(st) != SQLITE_DONE) {
        sqlite3_finalize(st);
        return 0;
    }

    *project_id = sqlite3_last_insert_rowid(db);
    sqlite3_finalize(st);

    // Make the owner a member too
    sqlite3_prepare_v2(db,
        "INSERT INTO project_members(project_id, user_id) VALUES (?, ?)",
        -1, &st, NULL);

    sqlite3_bind_int(st, 1, *project_id);
    sqlite3_bind_int(st, 2, owner_id);

    sqlite3_step(st);
    sqlite3_finalize(st);

    return 1;
}

int db_list_projects_for_user(int user_id, char *out, int out_size) {

    sqlite3_stmt *st;

    sqlite3_prepare_v2(db,
        "SELECT projects.id, projects.name "
        "FROM projects "
        "JOIN project_members ON project_members.project_id = projects.id "
        "WHERE project_members.user_id = ?",
        -1, &st, NULL);

    sqlite3_bind_int(st, 1, user_id);

    char temp[2048] = "";
    while (sqlite3_step(st) == SQLITE_ROW) {
        int id = sqlite3_column_int(st, 0);
        const char *name = (const char *)sqlite3_column_text(st, 1);

        char line[256];
        snprintf(line, sizeof(line), "%d|%s\n", id, name);
        strcat(temp, line);
    }

    sqlite3_finalize(st);

    strncpy(out, temp, out_size - 1);
    return 1;
}

int db_invite_member(int project_id, int user_id) {

    sqlite3_stmt *st;

    sqlite3_prepare_v2(db,
        "INSERT INTO project_members(project_id, user_id) VALUES (?, ?)",
        -1, &st, NULL);

    sqlite3_bind_int(st, 1, project_id);
    sqlite3_bind_int(st, 2, user_id);

    int rc = sqlite3_step(st);
    sqlite3_finalize(st);

    return rc == SQLITE_DONE;
}

/* =====================================
            TASK FUNCTIONS
===================================== */

int db_create_task(int project_id, const char *title, const char *desc, int *task_id) {

    sqlite3_stmt *st;

    sqlite3_prepare_v2(db,
        "INSERT INTO tasks(project_id, title, description) VALUES (?, ?, ?)",
        -1, &st, NULL);

    sqlite3_bind_int(st, 1, project_id);
    sqlite3_bind_text(st, 2, title, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, desc, -1, SQLITE_TRANSIENT);

    if (sqlite3_step(st) != SQLITE_DONE) {
        sqlite3_finalize(st);
        return 0;
    }

    *task_id = sqlite3_last_insert_rowid(db);
    sqlite3_finalize(st);

    return 1;
}

int db_list_tasks_in_project(int project_id, char *out, int out_size) {
    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT t.id, t.title, "
        "IFNULL(u.username, 'None') AS assignee "
        "FROM tasks t "
        "LEFT JOIN users u ON t.assignee_id = u.user_id "
        "WHERE t.project_id = ?;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return 0;

    sqlite3_bind_int(stmt, 1, project_id);

    char buf[512];
    out[0] = '\0';

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const unsigned char *title = sqlite3_column_text(stmt, 1);
        const unsigned char *assignee = sqlite3_column_text(stmt, 2);

        snprintf(buf, sizeof(buf),
                 "%d|%s|Assignee:%s\n",
                 id,
                 title ? (char *)title : "(null)",
                 assignee ? (char *)assignee : "None");

        strncat(out, buf, out_size - strlen(out) - 1);
    }

    sqlite3_finalize(stmt);
    return 1;
}


int db_assign_task(int task_id, int user_id) {
    sqlite3_stmt *stmt;
    const char *sql = "UPDATE tasks SET assignee_id = ? WHERE id = ?";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return 0;

    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_int(stmt, 2, task_id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}
