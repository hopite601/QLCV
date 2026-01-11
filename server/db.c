#include "db.h"
#include <stdio.h>
#include <string.h>

// Handle kết nối SQLite toàn cục
sqlite3 *db = NULL;

// Khởi tạo DB, tạo bảng (nếu chưa có), và cố gắng ALTER để tương thích DB cũ
int db_init(const char *path) {
    printf("DB init path = %s\n", path);
    if (sqlite3_open(path, &db) != SQLITE_OK) {
        printf("Cannot open DB: %s\n", sqlite3_errmsg(db));
        return 0;
    }

    // Đợt bảng cơ bản: users / projects / project_members / tasks
    // Lưu ý: dùng 1 chuỗi nhiều lệnh CREATE TABLE; sqlite3_exec sẽ chạy tất cả.
    const char *sql =
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "username TEXT UNIQUE,"
        "password TEXT,"
        "role TEXT DEFAULT 'MEMBER',"      // vai trò tổng (MEMBER/ADMIN...)
        "status TEXT DEFAULT 'ACTIVE',"    // trạng thái user
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"

        "CREATE TABLE IF NOT EXISTS projects ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT,"
        "description TEXT DEFAULT '',"
        "owner_id INTEGER,"                // chủ sở hữu project
        "status TEXT DEFAULT 'ACTIVE',"
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"

        // Bảng thành viên project (khoá chính (project_id, user_id) để tránh trùng)
        "CREATE TABLE IF NOT EXISTS project_members ("
        "project_id INTEGER,"
        "user_id INTEGER,"
        "role_in_project TEXT DEFAULT 'MEMBER'," // role trong project
        "joined_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "PRIMARY KEY(project_id, user_id)"
        ");"

        // Bảng tasks chính
        "CREATE TABLE IF NOT EXISTS tasks ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "project_id INTEGER,"
        "title TEXT,"
        "description TEXT,"
        "assignee_id INTEGER,"            // user được assign
        "status TEXT DEFAULT 'NOT_STARTED',"
        "progress INTEGER DEFAULT 0,"     // % tiến độ
        "start_date TEXT,"                // YYYY-MM-DD
        "end_date TEXT,"
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";

    char *err;
    if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
        printf("DB init error: %s\n", err);
        sqlite3_free(err);
        return 0;
    }

    // Bảng mở rộng: comments, attachments, project chat
    const char *sql2 =
        "CREATE TABLE IF NOT EXISTS task_comments ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "task_id INTEGER,"
        "user_id INTEGER,"
        "content TEXT,"
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS task_attachments ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "task_id INTEGER,"
        "filename TEXT,"
        "filepath TEXT,"
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS project_chat ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "project_id INTEGER,"
        "user_id INTEGER,"
        "content TEXT,"
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";

    if (sqlite3_exec(db, sql2, NULL, NULL, &err) != SQLITE_OK) {
        printf("DB init error: %s\n", err);
        sqlite3_free(err);
        return 0;
    }

    // Bảng báo cáo (reports) + comment/file của report
    const char *sql3 =
        "CREATE TABLE IF NOT EXISTS reports ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "project_id INTEGER,"
        "title TEXT,"
        "description TEXT DEFAULT '',"
        "created_by INTEGER,"
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS report_comments ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "report_id INTEGER,"
        "user_id INTEGER,"
        "content TEXT,"
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS report_files ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "report_id INTEGER,"
        "filename TEXT,"
        "filepath TEXT,"
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";

    if (sqlite3_exec(db, sql3, NULL, NULL, &err) != SQLITE_OK) {
        printf("DB init error: %s\n", err);
        sqlite3_free(err);
        return 0;
    }

    // Bổ sung cột cho DB cũ: chạy ALTER TABLE, bỏ qua error "duplicate column"
    // (SQLite không có IF NOT EXISTS cho ADD COLUMN, nên cách này là chuẩn) [web:38]
    const char *alter_sql[] = {
        // users
        "ALTER TABLE users ADD COLUMN role TEXT DEFAULT 'MEMBER'",
        "ALTER TABLE users ADD COLUMN status TEXT DEFAULT 'ACTIVE'",
        "ALTER TABLE users ADD COLUMN created_at DATETIME DEFAULT CURRENT_TIMESTAMP",

        // projects
        "ALTER TABLE projects ADD COLUMN description TEXT DEFAULT ''",
        "ALTER TABLE projects ADD COLUMN status TEXT DEFAULT 'ACTIVE'",
        "ALTER TABLE projects ADD COLUMN created_at DATETIME DEFAULT CURRENT_TIMESTAMP",

        // tasks
        "ALTER TABLE tasks ADD COLUMN status TEXT DEFAULT 'NOT_STARTED'",
        "ALTER TABLE tasks ADD COLUMN progress INTEGER DEFAULT 0",
        "ALTER TABLE tasks ADD COLUMN start_date TEXT",
        "ALTER TABLE tasks ADD COLUMN end_date TEXT",
        "ALTER TABLE tasks ADD COLUMN created_at DATETIME DEFAULT CURRENT_TIMESTAMP",

        // project_members
        "ALTER TABLE project_members ADD COLUMN role_in_project TEXT DEFAULT 'MEMBER'",
        "ALTER TABLE project_members ADD COLUMN joined_at DATETIME DEFAULT CURRENT_TIMESTAMP",

        NULL
    };
    for (int i = 0; alter_sql[i]; i++) {
        sqlite3_exec(db, alter_sql[i], NULL, NULL, NULL); // không check lỗi
    }

    return 1;
}

// Đóng DB global
void db_close() {
    if (db) sqlite3_close(db);
}

/* =====================================
            USER FUNCTIONS
===================================== */

// Thêm user mới (username UNIQUE). Trả 1 nếu OK, 0 nếu lỗi (trùng tên...).
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

// Auth user: kiểm tra username/password, trả user_id nếu thành công
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

// Lấy id từ username (dùng cho invite, assign...)
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

// Tạo project mới; đồng thời tự thêm owner vào project_members
int db_create_project(const char *name, int owner_id, int *project_id) {
    sqlite3_stmt *st;

    printf("CREATE_PROJECT name=%s owner_id=%d\n", name, owner_id);

    sqlite3_prepare_v2(db,
        "INSERT INTO projects(name, owner_id) VALUES (?, ?)",
        -1, &st, NULL);

    sqlite3_bind_text(st, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, owner_id);

    if (sqlite3_step(st) != SQLITE_DONE) {
        printf("CREATE_PROJECT SQL error: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(st);
        return 0;
    }

    *project_id = sqlite3_last_insert_rowid(db);
    sqlite3_finalize(st);

    // Owner cũng là member của project
    sqlite3_prepare_v2(db,
        "INSERT INTO project_members(project_id, user_id) VALUES (?, ?)",
        -1, &st, NULL);

    sqlite3_bind_int(st, 1, *project_id);
    sqlite3_bind_int(st, 2, owner_id);

    sqlite3_step(st);
    sqlite3_finalize(st);

    return 1;
}

// Liệt kê các project mà user là member -> "id|name\n..."
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

// Mời 1 user tham gia project (tránh trùng membership)
int db_invite_member(int project_id, int user_id) {
    // kiểm tra tồn tại trước
    sqlite3_stmt *st;
    sqlite3_prepare_v2(db,
        "SELECT 1 FROM project_members WHERE project_id=? AND user_id=?",
        -1, &st, NULL);
    sqlite3_bind_int(st, 1, project_id);
    sqlite3_bind_int(st, 2, user_id);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc == SQLITE_ROW) return -1; // đã là member

    // insert mới
    sqlite3_prepare_v2(db,
        "INSERT INTO project_members(project_id, user_id) VALUES (?, ?)",
        -1, &st, NULL);
    sqlite3_bind_int(st, 1, project_id);
    sqlite3_bind_int(st, 2, user_id);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);

    return rc == SQLITE_DONE ? 1 : 0;
}

/* =====================================
            PERMISSIONS / HELPERS
===================================== */

// Kiểm tra user có phải owner của project không
int db_is_project_owner(int project_id, int user_id) {
    sqlite3_stmt *st;
    sqlite3_prepare_v2(db,
        "SELECT 1 FROM projects WHERE id=? AND owner_id=?",
        -1, &st, NULL);
    sqlite3_bind_int(st, 1, project_id);
    sqlite3_bind_int(st, 2, user_id);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_ROW;
}

// Kiểm tra user có là member của project không
int db_is_project_member(int project_id, int user_id) {
    sqlite3_stmt *st;
    sqlite3_prepare_v2(db,
        "SELECT 1 FROM project_members WHERE project_id=? AND user_id=?",
        -1, &st, NULL);
    sqlite3_bind_int(st, 1, project_id);
    sqlite3_bind_int(st, 2, user_id);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_ROW;
}

// Lấy project_id của 1 task
int db_get_task_project_id(int task_id, int *project_id) {
    sqlite3_stmt *st;
    sqlite3_prepare_v2(db,
        "SELECT project_id FROM tasks WHERE id=?",
        -1, &st, NULL);
    sqlite3_bind_int(st, 1, task_id);
    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        *project_id = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
        return 1;
    }
    sqlite3_finalize(st);
    return 0;
}

// Lấy project_id của 1 report
int db_get_report_project_id(int report_id, int *project_id) {
    sqlite3_stmt *st;
    sqlite3_prepare_v2(db,
        "SELECT project_id FROM reports WHERE id=?",
        -1, &st, NULL);
    sqlite3_bind_int(st, 1, report_id);
    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        *project_id = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
        return 1;
    }
    sqlite3_finalize(st);
    return 0;
}

// Lấy assignee_id của task
int db_get_task_assignee_id(int task_id, int *assignee_id) {
    sqlite3_stmt *st;
    sqlite3_prepare_v2(db,
        "SELECT assignee_id FROM tasks WHERE id=?",
        -1, &st, NULL);
    sqlite3_bind_int(st, 1, task_id);
    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        *assignee_id = sqlite3_column_int(st, 0);
        sqlite3_finalize(st);
        return 1;
    }
    sqlite3_finalize(st);
    return 0;
}

/* =====================================
            TASK FUNCTIONS
===================================== */

// Tạo task đầy đủ + project_task_no tự tăng trong từng project (cho UI)
int db_create_task_full(int project_id, const char *title, const char *desc,
                        int assignee_id, const char *start_date, const char *end_date,
                        int *task_id) {
    int rc;
    sqlite3_stmt *st;
    int next_no = 1;

    // Lấy số thứ tự mới trong project: MAX(project_task_no)+1
    const char *sql_max =
        "SELECT COALESCE(MAX(project_task_no), 0) + 1 "
        "FROM tasks WHERE project_id = ?;";

    rc = sqlite3_prepare_v2(db, sql_max, -1, &st, NULL);
    if (rc != SQLITE_OK) return 0;

    sqlite3_bind_int(st, 1, project_id);

    if (sqlite3_step(st) == SQLITE_ROW) {
        next_no = sqlite3_column_int(st, 0);
    }
    sqlite3_finalize(st);

    // Insert task với project_task_no
    const char *sql_ins =
        "INSERT INTO tasks(project_id, project_task_no, title, description, assignee_id, start_date, end_date) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);";

    rc = sqlite3_prepare_v2(db, sql_ins, -1, &st, NULL);
    if (rc != SQLITE_OK) return 0;

    sqlite3_bind_int  (st, 1, project_id);
    sqlite3_bind_int  (st, 2, next_no);
    sqlite3_bind_text (st, 3, title, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (st, 4, desc ? desc : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int  (st, 5, assignee_id);
    sqlite3_bind_text (st, 6, start_date, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text (st, 7, end_date, -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(st);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(st);
        return 0;
    }

    if (task_id) {
        *task_id = (int)sqlite3_last_insert_rowid(db);
    }

    sqlite3_finalize(st);
    return 1;
}

// tạo task chỉ lưu project_id, title, desc (không có assignee/dates)
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

// Liệt kê tasks theo project: dùng project_task_no và nhiều field cho UI
int db_list_tasks_in_project(int project_id, char *out, int out_size) {
    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT t.id, t.title, "
        "IFNULL(u.username, 'None') AS assignee, "
        "IFNULL(t.status,'NOT_STARTED') AS status, "
        "IFNULL(t.progress,0) AS progress, "
        "IFNULL(t.start_date,''), IFNULL(t.end_date,'') "
        "FROM tasks t "
        "LEFT JOIN users u ON t.assignee_id = u.id "
        "WHERE t.project_id = ? "
        "ORDER BY t.id ASC;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return 0;

    sqlite3_bind_int(stmt, 1, project_id);

    char buf[512];
    out[0] = '\0';

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int task_id                    = sqlite3_column_int(stmt, 0);
        const unsigned char *title     = sqlite3_column_text(stmt, 1);
        const unsigned char *assignee  = sqlite3_column_text(stmt, 2);
        const unsigned char *status    = sqlite3_column_text(stmt, 3);
        int progress                   = sqlite3_column_int(stmt, 4);
        const unsigned char *start_date= sqlite3_column_text(stmt, 5);
        const unsigned char *end_date  = sqlite3_column_text(stmt, 6);

        snprintf(buf, sizeof(buf),
         "%d|%s|Assignee:%s|Status:%s|Progress:%d|Start:%s|End:%s\n",
         task_id,
         title ? (const char *)title : "",
         assignee ? (const char *)assignee : "None",
         status ? (const char *)status : "NOT_STARTED",
         progress,
         start_date ? (const char *)start_date : "",
         end_date ? (const char *)end_date : "");


        strncat(out, buf, out_size - strlen(out) - 1);
    }

    sqlite3_finalize(stmt);
    return 1;
}

/* =====================================
        EXTENDED TASK FEATURES
===================================== */

// Set status trực tiếp (ít dùng vì đã có progress->status)
int db_update_task_status(int task_id, const char *status) {
    sqlite3_stmt *stmt;
    const char *sql = "UPDATE tasks SET status = ? WHERE id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, status, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, task_id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

// Cập nhật progress và tự set status theo progress (0 / IN_PROGRESS / DONE)
int db_update_task_progress(int task_id, int progress) {
    const char *status = "NOT_STARTED";
    if (progress >= 100) status = "DONE";
    else if (progress > 0) status = "IN_PROGRESS";

    sqlite3_stmt *stmt;
    const char *sql = "UPDATE tasks SET progress = ?, status = ? WHERE id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, progress);
    sqlite3_bind_text(stmt, 2, status, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, task_id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

// Set start/end date cho task
int db_set_task_dates(int task_id, const char *start_date, const char *end_date) {
    sqlite3_stmt *stmt;
    const char *sql = "UPDATE tasks SET start_date = ?, end_date = ? WHERE id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(stmt, 1, start_date, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, end_date, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, task_id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

// Lấy detail 1 task để show popup
int db_get_task_detail(int task_id, char *out, int out_size) {
    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT t.id, t.project_id, t.title, t.description, IFNULL(u.username,'None'), "
        "IFNULL(t.status,'NOT_STARTED'), IFNULL(t.progress,0), IFNULL(t.start_date,''), IFNULL(t.end_date,'') "
        "FROM tasks t LEFT JOIN users u ON t.assignee_id = u.id WHERE t.id = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, task_id);
    if (sqlite3_step(stmt) != SQLITE_ROW) { sqlite3_finalize(stmt); return 0; }
    snprintf(out, out_size,
        "%d|%d|%s|%s|Assignee:%s|Status:%s|Progress:%d|Start:%s|End:%s\n",
        sqlite3_column_int(stmt,0),
        sqlite3_column_int(stmt,1),
        (const char*)sqlite3_column_text(stmt,2),
        (const char*)sqlite3_column_text(stmt,3),
        (const char*)sqlite3_column_text(stmt,4),
        (const char*)sqlite3_column_text(stmt,5),
        sqlite3_column_int(stmt,6),
        (const char*)sqlite3_column_text(stmt,7),
        (const char*)sqlite3_column_text(stmt,8));
    sqlite3_finalize(stmt);
    return 1;
}

// List task chuyên cho Gantt: trả id|title|Status:..|Progress:..|Start:..|End:..|Assignee:..
int db_list_tasks_gantt(int project_id, char *out, int out_size) {
    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT t.id, t.title, IFNULL(t.status,'NOT_STARTED'), IFNULL(t.progress,0),"
        "IFNULL(t.start_date,''), IFNULL(t.end_date,''), IFNULL(u.username,'None') "
        "FROM tasks t LEFT JOIN users u ON t.assignee_id = u.id WHERE t.project_id = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, project_id);
    out[0] = '\0';
    char buf[512];
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        snprintf(buf, sizeof(buf),
            "%d|%s|Status:%s|Progress:%d|Start:%s|End:%s|Assignee:%s\n",
            sqlite3_column_int(stmt,0),
            (const char*)sqlite3_column_text(stmt,1),
            (const char*)sqlite3_column_text(stmt,2),
            sqlite3_column_int(stmt,3),
            (const char*)sqlite3_column_text(stmt,4),
            (const char*)sqlite3_column_text(stmt,5),
            (const char*)sqlite3_column_text(stmt,6));
        strncat(out, buf, out_size - strlen(out) - 1);
    }
    sqlite3_finalize(stmt);
    return 1;
}

/* =====================================
            COMMENTS / ATTACHMENTS / CHAT
===================================== */

// Thêm comment task
int db_add_comment(int task_id, int user_id, const char *content) {
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO task_comments(task_id,user_id,content) VALUES(?,?,?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, task_id);
    sqlite3_bind_int(stmt, 2, user_id);
    sqlite3_bind_text(stmt, 3, content, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

// List comment task -> "id|username|content|created_at\n..."
int db_list_comments(int task_id, char *out, int out_size) {
    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT c.id, IFNULL(u.username,'?'), c.content, c.created_at "
        "FROM task_comments c LEFT JOIN users u ON c.user_id = u.id "
        "WHERE c.task_id = ? ORDER BY c.id ASC;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, task_id);
    out[0] = '\0';
    char buf[768];
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        snprintf(buf, sizeof(buf), "%d|%s|%s|%s\n",
            sqlite3_column_int(stmt,0),
            (const char*)sqlite3_column_text(stmt,1),
            (const char*)sqlite3_column_text(stmt,2),
            (const char*)sqlite3_column_text(stmt,3));
        strncat(out, buf, out_size - strlen(out) - 1);
    }
    sqlite3_finalize(stmt);
    return 1;
}

// Thêm attachment cho task
int db_add_attachment(int task_id, const char *filename, const char *filepath) {
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO task_attachments(task_id,filename,filepath) VALUES(?,?,?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, task_id);
    sqlite3_bind_text(stmt, 2, filename, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, filepath, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

// List attachments -> "id|filename|filepath|created_at\n..."
int db_list_attachments(int task_id, char *out, int out_size) {
    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT a.id, a.filename, a.filepath, a.created_at FROM task_attachments a "
        "WHERE a.task_id = ? ORDER BY a.id ASC;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, task_id);
    out[0] = '\0';
    char buf[768];
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        snprintf(buf, sizeof(buf), "%d|%s|%s|%s\n",
            sqlite3_column_int(stmt,0),
            (const char*)sqlite3_column_text(stmt,1),
            (const char*)sqlite3_column_text(stmt,2),
            (const char*)sqlite3_column_text(stmt,3));
        strncat(out, buf, out_size - strlen(out) - 1);
    }
    sqlite3_finalize(stmt);
    return 1;
}

// Thêm chat message vào project_chat
int db_add_chat(int project_id, int user_id, const char *content) {
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO project_chat(project_id,user_id,content) VALUES(?,?,?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, project_id);
    sqlite3_bind_int(stmt, 2, user_id);
    sqlite3_bind_text(stmt, 3, content, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

// List chat của project, chỉ lấy những id > after_id
int db_list_chat(int project_id, int after_id, char *out, int out_size) {
    sqlite3_stmt *stmt;
    const char *sql =
        "SELECT c.id, IFNULL(u.username,'?'), c.content, c.created_at "
        "FROM project_chat c LEFT JOIN users u ON c.user_id = u.id "
        "WHERE c.project_id = ? AND c.id > ? ORDER BY c.id ASC;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, project_id);
    sqlite3_bind_int(stmt, 2, after_id);
    out[0] = '\0';
    char buf[768];
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        snprintf(buf, sizeof(buf), "%d|%s|%s|%s\n",
            sqlite3_column_int(stmt,0),
            (const char*)sqlite3_column_text(stmt,1),
            (const char*)sqlite3_column_text(stmt,2),
            (const char*)sqlite3_column_text(stmt,3));
        strncat(out, buf, out_size - strlen(out) - 1);
    }
    sqlite3_finalize(stmt);
    return 1;
}

// Assign task cho user (update assignee_id)
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

/* =====================================
                REPORTS
===================================== */

// Thêm report mới
int db_add_report(int project_id, int created_by,
                  const char *title, const char *desc, int *report_id) {
    sqlite3_stmt *st;
    sqlite3_prepare_v2(db,
        "INSERT INTO reports(project_id,title,description,created_by) VALUES(?,?,?,?)",
        -1, &st, NULL);
    sqlite3_bind_int(st, 1, project_id);
    sqlite3_bind_text(st, 2, title, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, desc ? desc : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 4, created_by);

    if (sqlite3_step(st) != SQLITE_DONE) { sqlite3_finalize(st); return 0; }
    *report_id = (int)sqlite3_last_insert_rowid(db);
    sqlite3_finalize(st);
    return 1;
}

// List reports trong 1 project
int db_list_reports(int project_id, char *out, int out_size) {
    sqlite3_stmt *st;
    const char *sql =
        "SELECT r.id, r.title, IFNULL(u.username,'?'), r.created_at "
        "FROM reports r LEFT JOIN users u ON r.created_by = u.id "
        "WHERE r.project_id = ? ORDER BY r.id DESC;";
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(st, 1, project_id);

    out[0] = '\0';
    char buf[768];
    while (sqlite3_step(st) == SQLITE_ROW) {
        snprintf(buf, sizeof(buf), "%d|%s|By:%s|At:%s\n",
            sqlite3_column_int(st,0),
            (const char*)sqlite3_column_text(st,1),
            (const char*)sqlite3_column_text(st,2),
            (const char*)sqlite3_column_text(st,3));
        strncat(out, buf, out_size - (int)strlen(out) - 1);
    }
    sqlite3_finalize(st);
    return 1;
}

// Lấy detail 1 report
int db_get_report(int report_id, char *out, int out_size) {
    sqlite3_stmt *st;
    const char *sql =
        "SELECT r.id, r.project_id, r.title, r.description, IFNULL(u.username,'?'), r.created_at "
        "FROM reports r LEFT JOIN users u ON r.created_by = u.id "
        "WHERE r.id = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(st, 1, report_id);

    if (sqlite3_step(st) != SQLITE_ROW) { sqlite3_finalize(st); return 0; }

    snprintf(out, out_size, "%d|%d|%s|%s|By:%s|At:%s\n",
        sqlite3_column_int(st,0),
        sqlite3_column_int(st,1),
        (const char*)sqlite3_column_text(st,2),
        (const char*)sqlite3_column_text(st,3),
        (const char*)sqlite3_column_text(st,4),
        (const char*)sqlite3_column_text(st,5));

    sqlite3_finalize(st);
    return 1;
}

// Update title/desc report
int db_update_report(int report_id, const char *title, const char *desc) {
    sqlite3_stmt *st;
    const char *sql = "UPDATE reports SET title=?, description=? WHERE id=?";
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_text(st, 1, title, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, desc ? desc : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 3, report_id);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE;
}

// Xoá report + toàn bộ comment/file của nó
int db_delete_report(int report_id) {
    sqlite3_stmt *st;

    sqlite3_prepare_v2(db, "DELETE FROM report_comments WHERE report_id=?", -1, &st, NULL);
    sqlite3_bind_int(st, 1, report_id);
    sqlite3_step(st);
    sqlite3_finalize(st);

    sqlite3_prepare_v2(db, "DELETE FROM report_files WHERE report_id=?", -1, &st, NULL);
    sqlite3_bind_int(st, 1, report_id);
    sqlite3_step(st);
    sqlite3_finalize(st);

    sqlite3_prepare_v2(db, "DELETE FROM reports WHERE id=?", -1, &st, NULL);
    sqlite3_bind_int(st, 1, report_id);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE;
}

/* =====================================
            REPORT COMMENTS
===================================== */

// Thêm comment vào report
int db_add_report_comment(int report_id, int user_id, const char *content) {
    sqlite3_stmt *st;
    const char *sql = "INSERT INTO report_comments(report_id,user_id,content) VALUES(?,?,?)";
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(st, 1, report_id);
    sqlite3_bind_int(st, 2, user_id);
    sqlite3_bind_text(st, 3, content, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE;
}

// List comment của report
int db_list_report_comments(int report_id, char *out, int out_size) {
    sqlite3_stmt *st;
    const char *sql =
        "SELECT c.id, IFNULL(u.username,'?'), c.content, c.created_at "
        "FROM report_comments c LEFT JOIN users u ON c.user_id=u.id "
        "WHERE c.report_id=? ORDER BY c.id ASC;";
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(st, 1, report_id);

    out[0] = '\0';
    char buf[768];
    while (sqlite3_step(st) == SQLITE_ROW) {
        snprintf(buf, sizeof(buf), "%d|%s|%s|%s\n",
            sqlite3_column_int(st,0),
            (const char*)sqlite3_column_text(st,1),
            (const char*)sqlite3_column_text(st,2),
            (const char*)sqlite3_column_text(st,3));
        strncat(out, buf, out_size - (int)strlen(out) - 1);
    }
    sqlite3_finalize(st);
    return 1;
}

// Xoá 1 comment của report
int db_delete_report_comment(int comment_id) {
    sqlite3_stmt *st;
    const char *sql = "DELETE FROM report_comments WHERE id=?";
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(st, 1, comment_id);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE;
}

/* =====================================
                REPORT FILES
===================================== */

// Thêm file đính kèm cho report
int db_add_report_file(int report_id, const char *filename, const char *filepath) {
    sqlite3_stmt *st;
    const char *sql = "INSERT INTO report_files(report_id,filename,filepath) VALUES(?,?,?)";
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(st, 1, report_id);
    sqlite3_bind_text(st, 2, filename, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, filepath, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE;
}

// List file đính kèm report
int db_list_report_files(int report_id, char *out, int out_size) {
    sqlite3_stmt *st;
    const char *sql =
        "SELECT f.id, f.filename, f.filepath, f.created_at "
        "FROM report_files f WHERE f.report_id=? ORDER BY f.id ASC;";
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(st, 1, report_id);

    out[0] = '\0';
    char buf[768];
    while (sqlite3_step(st) == SQLITE_ROW) {
        snprintf(buf, sizeof(buf), "%d|%s|%s|%s\n",
            sqlite3_column_int(st,0),
            (const char*)sqlite3_column_text(st,1),
            (const char*)sqlite3_column_text(st,2),
            (const char*)sqlite3_column_text(st,3));
        strncat(out, buf, out_size - (int)strlen(out) - 1);
    }
    sqlite3_finalize(st);
    return 1;
}

// Xoá file đính kèm report
int db_delete_report_file(int file_id) {
    sqlite3_stmt *st;
    const char *sql = "DELETE FROM report_files WHERE id=?";
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(st, 1, file_id);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE;
}
