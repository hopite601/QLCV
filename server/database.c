#include <stdio.h>
#include <string.h>
#include <sqlite3.h>
#include "database.h"
#include "logger.h"


sqlite3 *db;   // Biến DB toàn cục

int db_init(const char *filename) {
    char *err = NULL;

    if (sqlite3_open(filename, &db) != SQLITE_OK) {
        fprintf(stderr, "Cannot open db: %s\n", sqlite3_errmsg(db));
        return 0;
    }

    const char *sql_users =
        "CREATE TABLE IF NOT EXISTS users ("
        " user_id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " username TEXT UNIQUE NOT NULL,"
        " password TEXT NOT NULL,"
        " role TEXT DEFAULT 'MEMBER',"
        " status TEXT DEFAULT 'ACTIVE',"
        " created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";

    if (sqlite3_exec(db, sql_users, 0, 0, &err) != SQLITE_OK) {
        fprintf(stderr, "DB Init Error: %s\n", err);
        sqlite3_free(err);
        return 0;
    }

    printf("[DB] initialized.\n");
    return 1;
}


int db_create_user(const char *username, const char *password) {
    const char *sql =
        "INSERT INTO users(username, password) VALUES(?, ?);";

    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK)
        return -1;

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE)
        return 1;     // success
    else
        return 0;     // failed (username exists)
}


int db_check_login(const char *username, const char *password, int *out_user_id) {
    const char *sql =
        "SELECT user_id FROM users WHERE username=? AND password=?;";

    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK)
        return -1;

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);

    if (rc == SQLITE_ROW) {
        *out_user_id = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return 1;  // login ok
    }

    sqlite3_finalize(stmt);
    return 0; // invalid
}
