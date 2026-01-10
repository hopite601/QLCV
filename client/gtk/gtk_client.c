#include <gtk/gtk.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../common.h"
#include "../protocol.h"
#include "net.h"

typedef struct {
    int sockfd;
    int current_project_id;
    int last_chat_id;

    // login
    GtkWidget *login_win;
    GtkWidget *entry_user;
    GtkWidget *entry_pass;
    GtkWidget *label_login_status;

    // main
    GtkWidget *main_win;
    GtkWidget *project_store_view;
    GtkListStore *project_store;

    GtkWidget *combo_projects_tasks;
    GtkWidget *combo_projects_gantt;
    GtkWidget *combo_projects_chat;

    GtkListStore *task_store;
    GtkWidget *task_view;

    GtkWidget *gantt_area;
    GPtrArray *gantt_tasks; // array of strings lines

        // chat
    GtkWidget *chat_list;      // GtkListBox hiển thị các message
    GtkWidget *entry_chat;
    char current_user[64];     // username hiện tại sau khi login

} App;

static void show_error(GtkWindow *parent, const char *msg) {
    GtkWidget *d = gtk_message_dialog_new(parent,
        GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
        "%s", msg ? msg : "Error");
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

static int connect_server(void) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return -1;

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(sockfd);
        return -1;
    }
    return sockfd;
}

// -------------------- Parsing helpers --------------------
static void store_clear(GtkListStore *store) {
    gtk_list_store_clear(store);
}

static void projects_load(App *app);
static void tasks_load(App *app, int project_id);
static void gantt_load(App *app, int project_id);

static int combo_get_selected_project_id(GtkComboBoxText *combo) {
    const char *txt = gtk_combo_box_text_get_active_text(combo);
    if (!txt) return -1;
    // format: "<id> - <name>"
    int id = atoi(txt);
    g_free((gpointer)txt);
    return id;
}

static void combo_set_from_projects_payload(GtkComboBoxText *combo, const char *payload) {
    gtk_combo_box_text_remove_all(combo);
    if (!payload || !*payload) return;

    // lines: id|name
    char *copy = g_strdup(payload);
    char *saveptr = NULL;
    for (char *line = strtok_r(copy, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
        if (!*line) continue;
        char *sep = strchr(line, '|');
        if (!sep) continue;
        *sep = '\0';
        const char *id = line;
        const char *name = sep + 1;
        char item[256];
        snprintf(item, sizeof(item), "%s - %s", id, name);
        gtk_combo_box_text_append_text(combo, item);
    }
    g_free(copy);
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
}

// -------------------- Projects tab --------------------
static void projects_load(App *app) {
    char payload[BUF_SIZE] = {0};
    int code = net_request(app->sockfd, CMD_LIST_PROJECT "\n", payload, sizeof(payload));
    (void)code;

    // update store
    store_clear(app->project_store);

    if (payload[0]) {
        char *copy = g_strdup(payload);
        char *saveptr = NULL;
        for (char *line = strtok_r(copy, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
            if (!*line) continue;
            char *sep = strchr(line, '|');
            if (!sep) continue;
            *sep = '\0';
            int id = atoi(line);
            const char *name = sep + 1;

            GtkTreeIter it;
            gtk_list_store_append(app->project_store, &it);
            gtk_list_store_set(app->project_store, &it,
                0, id,
                1, name,
                -1);
        }
        g_free(copy);
    }

    // update combos
    combo_set_from_projects_payload(GTK_COMBO_BOX_TEXT(app->combo_projects_tasks), payload);
    combo_set_from_projects_payload(GTK_COMBO_BOX_TEXT(app->combo_projects_gantt), payload);
    combo_set_from_projects_payload(GTK_COMBO_BOX_TEXT(app->combo_projects_chat), payload);
}

static void on_project_refresh(GtkButton *btn, gpointer user_data) {
    (void)btn;
    App *app = user_data;
    projects_load(app);
}

static void on_project_create(GtkButton *btn, gpointer user_data) {
    (void)btn;
    App *app = user_data;

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
    "Create Task", GTK_WINDOW(app->main_win),
    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
    "Cancel", GTK_RESPONSE_CANCEL,
    "Create", GTK_RESPONSE_OK,
    NULL
);

GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
GtkWidget *grid = gtk_grid_new();
gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
gtk_container_add(GTK_CONTAINER(content), grid);

GtkWidget *e_title = gtk_entry_new();
GtkWidget *e_desc = gtk_entry_new();
GtkWidget *e_assignee = gtk_entry_new();
GtkWidget *e_start = gtk_entry_new();
GtkWidget *e_end = gtk_entry_new();

gtk_entry_set_placeholder_text(GTK_ENTRY(e_title), "Title");
gtk_entry_set_placeholder_text(GTK_ENTRY(e_desc), "Description");
gtk_entry_set_placeholder_text(GTK_ENTRY(e_assignee), "Assignee username");
gtk_entry_set_placeholder_text(GTK_ENTRY(e_start), "Start date YYYY-MM-DD");
gtk_entry_set_placeholder_text(GTK_ENTRY(e_end), "End date YYYY-MM-DD");

gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Title"), 0, 0, 1, 1);
gtk_grid_attach(GTK_GRID(grid), e_title, 1, 0, 1, 1);
gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Description"), 0, 1, 1, 1);
gtk_grid_attach(GTK_GRID(grid), e_desc, 1, 1, 1, 1);
gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Assignee"), 0, 2, 1, 1);
gtk_grid_attach(GTK_GRID(grid), e_assignee, 1, 2, 1, 1);
gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Start"), 0, 3, 1, 1);
gtk_grid_attach(GTK_GRID(grid), e_start, 1, 3, 1, 1);
gtk_grid_attach(GTK_GRID(grid), gtk_label_new("End"), 0, 4, 1, 1);
gtk_grid_attach(GTK_GRID(grid), e_end, 1, 4, 1, 1);

gtk_widget_show_all(dialog);

int resp = gtk_dialog_run(GTK_DIALOG(dialog));
if (resp == GTK_RESPONSE_OK) {
    const char *title = gtk_entry_get_text(GTK_ENTRY(e_title));
    const char *desc = gtk_entry_get_text(GTK_ENTRY(e_desc));
    const char *assignee = gtk_entry_get_text(GTK_ENTRY(e_assignee));
    const char *start = gtk_entry_get_text(GTK_ENTRY(e_start));
    const char *end = gtk_entry_get_text(GTK_ENTRY(e_end));

    if (!title || !*title) {
        show_error(GTK_WINDOW(app->main_win), "Task title is required");
    } else if (!assignee || !*assignee) {
        show_error(GTK_WINDOW(app->main_win), "Assignee username is required");
    } else if (!start || !*start || !end || !*end) {
        show_error(GTK_WINDOW(app->main_win), "Start/End date is required (YYYY-MM-DD)");
    } else {
        char line[BUF_SIZE];
        int pid = combo_get_selected_project_id(GTK_COMBO_BOX_TEXT(app->combo_projects_tasks));
if (pid <= 0) { show_error(GTK_WINDOW(app->main_win), "Select a project first"); gtk_widget_destroy(dialog); return; }
snprintf(line, sizeof(line), "%s|%d|%s|%s|%s|%s|%s\n",
         CMD_CREATE_TASK, pid, title, desc ? desc : "", assignee, start, end);

char payload[BUF_SIZE] = {0};
int code = net_request(app->sockfd, line, payload, sizeof(payload));
if (code != 0) show_error(GTK_WINDOW(app->main_win), payload[0] ? payload : "Create task failed");
else {
    tasks_load(app, pid);
    gantt_load(app, pid); // náº¿u cÃ³ gantt
}
    }
}

gtk_widget_destroy(dialog);

}

static void on_project_invite(GtkButton *btn, gpointer user_data) {
    (void)btn;
    App *app = user_data;

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Invite Member", GTK_WINDOW(app->main_win),
        GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "Invite", GTK_RESPONSE_OK, NULL);

    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *combo = gtk_combo_box_text_new();
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Username to invite");

    // fill projects into combo
    GtkTreeIter it;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(app->project_store), &it);
    while (valid) {
        int id; char *name;
        gtk_tree_model_get(GTK_TREE_MODEL(app->project_store), &it, 0, &id, 1, &name, -1);
        char item[256];
        snprintf(item, sizeof(item), "%d - %s", id, name);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), item);
        g_free(name);
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(app->project_store), &it);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);

    gtk_box_pack_start(GTK_BOX(box), combo, FALSE, FALSE, 8);
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 8);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        int pid = combo_get_selected_project_id(GTK_COMBO_BOX_TEXT(combo));
        const char *username = gtk_entry_get_text(GTK_ENTRY(entry));
        if (pid <= 0 || !username || !*username) {
            show_error(GTK_WINDOW(app->main_win), "Project and username are required");
        } else {
            char line[BUF_SIZE];
            snprintf(line, sizeof(line), "%s|%d|%s\n", CMD_INVITE_MEMBER, pid, username);
            char payload[BUF_SIZE] = {0};
            int code = net_request(app->sockfd, line, payload, sizeof(payload));
            if (code != 0) show_error(GTK_WINDOW(app->main_win), payload[0] ? payload : "Invite failed");
        }
    }

    gtk_widget_destroy(dialog);
}

// -------------------- Tasks tab --------------------
static void tasks_load(App *app, int project_id) {
    store_clear(app->task_store);
    if (project_id <= 0) return;

    char line[128];
    snprintf(line, sizeof(line), "%s|%d\n", CMD_LIST_TASK, project_id);
    char payload[BUF_SIZE] = {0};
    int code = net_request(app->sockfd, line, payload, sizeof(payload));
    (void)code;

    if (!payload[0]) return;

    // lines: id|title|Assignee:xx|Status:..|Start:..|End:..
    char *copy = g_strdup(payload);
    char *saveptr = NULL;
    for (char *ln = strtok_r(copy, "\n", &saveptr); ln; ln = strtok_r(NULL, "\n", &saveptr)) {
        if (!*ln) continue;
        char *parts[8] = {0};
        int k=0;
        char *sv2=NULL;
        for (char *p=strtok_r(ln, "|", &sv2); p && k<8; p=strtok_r(NULL,"|",&sv2)) parts[k++]=p;
        if (k < 2) continue;
        int id = atoi(parts[0]);
        const char *title = parts[1];
        // sau khi split parts[]
const char *assignee_raw = (k>=3)? parts[2] : "Assignee:None";
const char *status_raw   = (k>=4)? parts[3] : "Status:NOT_STARTED";
const char *progress_raw = (k>=5)? parts[4] : "Progress:0";
const char *start_raw    = (k>=6)? parts[5] : "Start:";
const char *end_raw      = (k>=7)? parts[6] : "End:";

// cáº¯t prefix
const char *assignee = assignee_raw;
if (g_str_has_prefix(assignee_raw, "Assignee:"))
    assignee = assignee_raw + strlen("Assignee:");

const char *status = status_raw;
if (g_str_has_prefix(status_raw, "Status:"))
    status = status_raw + strlen("Status:");

const char *progress = progress_raw;
if (g_str_has_prefix(progress_raw, "Progress:"))
    progress = progress_raw + strlen("Progress:");

const char *start = start_raw;
if (g_str_has_prefix(start_raw, "Start:"))
    start = start_raw + strlen("Start:");

const char *end = end_raw;
if (g_str_has_prefix(end_raw, "End:"))
    end = end_raw + strlen("End:");

// trim khoáº£ng tráº¯ng
while (*assignee == ' ') assignee++;
while (*status == ' ') status++;
while (*progress == ' ') progress++;
while (*start == ' ') start++;
while (*end == ' ') end++;

// build chuá»—i hiá»ƒn thá»‹ cho cá»™t 3
char progress_display[32];
snprintf(progress_display, sizeof(progress_display), "%s%%", *progress ? progress : "0");

GtkTreeIter it;
gtk_list_store_append(app->task_store, &it);
gtk_list_store_set(app->task_store, &it,
    0, id,
    1, title,
    2, assignee,
    3, progress_display,  // cá»™t Status giá» lÃ  % tiáº¿n Ä‘á»™
    4, start,
    5, end,
    -1);
    }
    g_free(copy);
}

static void on_tasks_refresh(GtkButton *btn, gpointer user_data) {
    (void)btn;
    App *app = user_data;
    int pid = combo_get_selected_project_id(GTK_COMBO_BOX_TEXT(app->combo_projects_tasks));
    tasks_load(app, pid);
}

static void on_tasks_project_changed(GtkComboBox *combo, gpointer user_data) {
    App *app = user_data;
    int pid = combo_get_selected_project_id(GTK_COMBO_BOX_TEXT(combo));
    tasks_load(app, pid);
}

static int get_selected_task_id(App *app) {
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(app->task_view));
    GtkTreeModel *model;
    GtkTreeIter it;
    if (gtk_tree_selection_get_selected(sel, &model, &it)) {
        int id;
        gtk_tree_model_get(model, &it, 0, &id, -1);
        return id;
    }
    return -1;
}

// ===== Comments helpers =====
static void load_task_comments(App *app, int task_id, GtkTextBuffer *buf) {
    char line[128];
    snprintf(line, sizeof(line), "%s|%d\n", CMD_LIST_COMMENTS, task_id);
    char payload[BUF_SIZE] = {0};
    int code = net_request(app->sockfd, line, payload, sizeof(payload));
    if (code != 0) {
        gtk_text_buffer_set_text(buf, "Failed to load comments", -1);
        return;
    }
    if (!payload[0] || strcmp(payload, "No comments\n") == 0) {
        gtk_text_buffer_set_text(buf, "No comments", -1);
        return;
    }

    gtk_text_buffer_set_text(buf, "", -1);
    GtkTextIter it;

    char *copy = g_strdup(payload);
    char *save = NULL;
    for (char *ln = strtok_r(copy, "\n", &save); ln; ln = strtok_r(NULL, "\n", &save)) {
        if (!*ln) continue;

        // format giáº£ Ä‘á»‹nh: ts|user|content
        char *tmp = g_strdup(ln);
        char *p[3] = {0}; int k = 0; char *sv = NULL;
        for (char *x = strtok_r(tmp, "|", &sv); x && k < 3; x = strtok_r(NULL, "|", &sv))
            p[k++] = x;

        if (k == 3) {
            char row[1024];
            snprintf(row, sizeof(row), "[%s] %s: %s\n", p[0], p[1], p[2]);
            gtk_text_buffer_get_end_iter(buf, &it);
            gtk_text_buffer_insert(buf, &it, row, -1);
        }
        g_free(tmp);
    }
    g_free(copy);
}


static void on_task_comments(GtkButton *btn, gpointer user_data) {
    (void)btn;
    App *app = user_data;
    int task_id = get_selected_task_id(app);
    if (task_id <= 0) {
        show_error(GTK_WINDOW(app->main_win), "Select a task first");
        return;
    }

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Comments",
        GTK_WINDOW(app->main_win),
        GTK_DIALOG_MODAL,
        "Close", GTK_RESPONSE_CLOSE,
        "Add", GTK_RESPONSE_OK,
        NULL
    );
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    GtkWidget *view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_box_pack_start(GTK_BOX(box), view, TRUE, TRUE, 8);

    GtkWidget *entry = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(entry), TRUE);   // thÃªm dÃ²ng nÃ y
    gtk_widget_set_size_request(entry, -1, 80);
    GtkTextBuffer *buf_new = gtk_text_view_get_buffer(GTK_TEXT_VIEW(entry));

    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 8);

    gtk_widget_show_all(dialog);

    load_task_comments(app, task_id, buf);

    while (1) {
        int resp = gtk_dialog_run(GTK_DIALOG(dialog));
        if (resp != GTK_RESPONSE_OK) break;

        GtkTextIter s, e;
        gtk_text_buffer_get_bounds(buf_new, &s, &e);
        char *content = gtk_text_buffer_get_text(buf_new, &s, &e, FALSE);
        if (!content || !*content) { g_free(content); continue; }

        char line[BUF_SIZE];
        snprintf(line, sizeof(line), "%s|%d|%s\n", CMD_ADD_COMMENT, task_id, content);
        char payload[BUF_SIZE] = {0};
        g_print("ADD_COMMENT CMD=[%s]\n", line);
        int code = net_request(app->sockfd, line, payload, sizeof(payload));
        g_free(content);
        if (code != 0) {
            show_error(GTK_WINDOW(app->main_win), payload[0] ? payload : "Add comment failed");
            continue;
        }

        gtk_text_buffer_set_text(buf_new, "", -1);
        load_task_comments(app, task_id, buf);
    }

    gtk_widget_destroy(dialog);
}

// ===== Attachments helpers =====
typedef struct {
    int id;
    char *filename;
    char *path;
    char *created_at;
} AttachRow;

typedef struct {
    App *app;
    GtkListBox *list;
    GPtrArray *rows;
} AttachCtx;

static void clear_attach_rows(GPtrArray *rows) {
    for (guint i = 0; i < rows->len; i++) {
        AttachRow *r = g_ptr_array_index(rows, i);
        g_free(r->filename);
        g_free(r->path);
        g_free(r->created_at);
        g_free(r);
    }
    g_ptr_array_set_size(rows, 0);
}

static void load_task_attachments(App *app, int task_id, GtkListBox *list, GPtrArray *rows) {
    clear_attach_rows(rows);
    GList *children = gtk_container_get_children(GTK_CONTAINER(list));
    for (GList *l = children; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);

    char line[128];
    snprintf(line, sizeof(line), "%s|%d\n", CMD_LIST_ATTACHMENTS, task_id);
    char payload[BUF_SIZE] = {0};
    int code = net_request(app->sockfd, line, payload, sizeof(payload));
    if (code != 0 || !payload[0] || strcmp(payload, "No attachments\n") == 0)
        return;

    char *copy = g_strdup(payload);
    char *save = NULL;
    for (char *ln = strtok_r(copy, "\n", &save); ln; ln = strtok_r(NULL, "\n", &save)) {
        if (!*ln) continue;
        char *tmp = g_strdup(ln);
        char *p[4] = {0}; int k = 0; char *sv = NULL;
        for (char *x = strtok_r(tmp, "|", &sv); x && k < 4; x = strtok_r(NULL, "|", &sv))
            p[k++] = x;
        if (k == 4) {
            AttachRow *r = g_new0(AttachRow, 1);
            r->id = atoi(p[0]);
            r->filename = g_strdup(p[1]);
            r->path = g_strdup(p[2]);
            r->created_at = g_strdup(p[3]);
            g_ptr_array_add(rows, r);

            char label[1024];
            snprintf(label, sizeof(label), "%s  (%s)", r->filename, r->created_at);
            GtkWidget *row = gtk_label_new(label);
            gtk_widget_set_halign(row, GTK_ALIGN_START);
            gtk_list_box_insert(list, row, -1);
        }
        g_free(tmp);
    }
    g_free(copy);
    gtk_widget_show_all(GTK_WIDGET(list));
}

static void open_selected_attachment(AttachCtx *ctx) {
    GtkListBoxRow *row = gtk_list_box_get_selected_row(ctx->list);
    if (!row) return;
    int idx = gtk_list_box_row_get_index(row);
    if (idx < 0 || (guint)idx >= ctx->rows->len) return;

    AttachRow *r = g_ptr_array_index(ctx->rows, idx);
    if (!r->path || !*r->path) return;

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "open \"%s\"", r->path);   // macOS
    system(cmd);
}

static int send_attachment(App *app, int task_id, const char *path) {
    char *fname = g_path_get_basename(path);
    char line[BUF_SIZE];
    snprintf(line, sizeof(line), "%s|%d|%s|%s\n",
             CMD_ADD_ATTACHMENT, task_id, fname, path);
    g_free(fname);

    char payload[BUF_SIZE] = {0};
    int code = net_request(app->sockfd, line, payload, sizeof(payload));
    if (code != 0)
        show_error(GTK_WINDOW(app->main_win), payload[0] ? payload : "Add attachment failed");
    return code;
}

static void on_task_attachments(GtkButton *btn, gpointer user_data) {
    (void)btn;
    App *app = user_data;
    int task_id = get_selected_task_id(app);
    if (task_id <= 0) {
        show_error(GTK_WINDOW(app->main_win), "Select a task first");
        return;
    }

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Attachments",
        GTK_WINDOW(app->main_win),
        GTK_DIALOG_MODAL,
        "Close", GTK_RESPONSE_CLOSE,
        "Add file", GTK_RESPONSE_ACCEPT,
        NULL
    );
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    GtkWidget *list_widget = gtk_list_box_new();
    gtk_widget_set_vexpand(list_widget, TRUE);
    gtk_box_pack_start(GTK_BOX(box), list_widget, TRUE, TRUE, 8);

    GtkWidget *btn_open = gtk_button_new_with_label("Open selected");
    gtk_box_pack_start(GTK_BOX(box), btn_open, FALSE, FALSE, 8);

    GPtrArray *rows = g_ptr_array_new();
    AttachCtx ctx = { app, GTK_LIST_BOX(list_widget), rows };
    g_signal_connect_swapped(btn_open, "clicked",
                             G_CALLBACK(open_selected_attachment), &ctx);

    gtk_widget_show_all(dialog);

    load_task_attachments(app, task_id, GTK_LIST_BOX(list_widget), rows);

    while (1) {
        int resp = gtk_dialog_run(GTK_DIALOG(dialog));
        if (resp != GTK_RESPONSE_ACCEPT) break;

        GtkWidget *fc = gtk_file_chooser_dialog_new(
            "Select file",
            GTK_WINDOW(dialog),
            GTK_FILE_CHOOSER_ACTION_OPEN,
            "Cancel", GTK_RESPONSE_CANCEL,
            "Open", GTK_RESPONSE_ACCEPT,
            NULL
        );
        if (gtk_dialog_run(GTK_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
            char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
            if (filename) {
                if (send_attachment(app, task_id, filename) == 0)
                    load_task_attachments(app, task_id, GTK_LIST_BOX(list_widget), rows);
                g_free(filename);
            }
        }
        gtk_widget_destroy(fc);
    }

    gtk_widget_destroy(dialog);
    clear_attach_rows(rows);
    g_ptr_array_free(rows, TRUE);
}

static void on_task_create(GtkButton *btn, gpointer user_data) {
    (void)btn;
    App *app = user_data;

    int pid = combo_get_selected_project_id(GTK_COMBO_BOX_TEXT(app->combo_projects_tasks));
    if (pid <= 0) {
        show_error(GTK_WINDOW(app->main_win), "Select a project first");
        return;
    }

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Create Task",
        GTK_WINDOW(app->main_win),
        GTK_DIALOG_MODAL,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Create", GTK_RESPONSE_OK,
        NULL
    );

    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    GtkWidget *e_title = gtk_entry_new();
    GtkWidget *e_desc = gtk_entry_new();
    GtkWidget *e_assignee = gtk_entry_new();
    GtkWidget *e_start = gtk_entry_new();
    GtkWidget *e_end = gtk_entry_new();

    gtk_entry_set_placeholder_text(GTK_ENTRY(e_title), "Task title");
    gtk_entry_set_placeholder_text(GTK_ENTRY(e_desc), "Description");
    gtk_entry_set_placeholder_text(GTK_ENTRY(e_assignee), "Assignee username");
    gtk_entry_set_placeholder_text(GTK_ENTRY(e_start), "Start date (YYYY-MM-DD)");
    gtk_entry_set_placeholder_text(GTK_ENTRY(e_end), "End date (YYYY-MM-DD)");

    gtk_box_pack_start(GTK_BOX(box), e_title, FALSE, FALSE, 8);
    gtk_box_pack_start(GTK_BOX(box), e_desc, FALSE, FALSE, 8);
    gtk_box_pack_start(GTK_BOX(box), e_assignee, FALSE, FALSE, 8);
    gtk_box_pack_start(GTK_BOX(box), e_start, FALSE, FALSE, 8);
    gtk_box_pack_start(GTK_BOX(box), e_end, FALSE, FALSE, 8);

    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char *title = gtk_entry_get_text(GTK_ENTRY(e_title));
        const char *desc = gtk_entry_get_text(GTK_ENTRY(e_desc));
        const char *assignee = gtk_entry_get_text(GTK_ENTRY(e_assignee));
        const char *start = gtk_entry_get_text(GTK_ENTRY(e_start));
        const char *end = gtk_entry_get_text(GTK_ENTRY(e_end));

        if (!title || !*title) {
            show_error(GTK_WINDOW(app->main_win), "Task title is required");
        } else if (!desc || !*desc) {
            show_error(GTK_WINDOW(app->main_win), "Description is required");
        } else if (!assignee || !*assignee) {
            show_error(GTK_WINDOW(app->main_win), "Assignee username is required");
        } else if (!start || !*start || !end || !*end) {
            show_error(GTK_WINDOW(app->main_win), "Start/End date is required (YYYY-MM-DD)");
        } else {
            char line[BUF_SIZE];
            snprintf(line, sizeof(line), "%s|%d|%s|%s|%s|%s|%s\n",
                     CMD_CREATE_TASK, pid, title, desc, assignee, start, end);

            char payload[BUF_SIZE] = {0};
            int code = net_request(app->sockfd, line, payload, sizeof(payload));
            if (code != 0) {
                show_error(GTK_WINDOW(app->main_win), payload[0] ? payload : "Create task failed");
            } else {
                tasks_load(app, pid);
               
            }
        }
    }

    gtk_widget_destroy(dialog);
}

static void on_task_assign(GtkButton *btn, gpointer user_data) {
    (void)btn;
    App *app = user_data;
    int task_id = get_selected_task_id(app);
    if (task_id <= 0) { show_error(GTK_WINDOW(app->main_win), "Select a task first"); return; }

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Assign Task", GTK_WINDOW(app->main_win),
        GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "Assign", GTK_RESPONSE_OK, NULL);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Username");
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 8);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char *username = gtk_entry_get_text(GTK_ENTRY(entry));
        if (!username || !*username) {
            show_error(GTK_WINDOW(app->main_win), "Username required");
        } else {
            char line[BUF_SIZE];
            snprintf(line, sizeof(line), "%s|%d|%s\n", CMD_ASSIGN_TASK, task_id, username);
            char payload[BUF_SIZE] = {0};
            int code = net_request(app->sockfd, line, payload, sizeof(payload));
            if (code != 0) show_error(GTK_WINDOW(app->main_win), payload[0] ? payload : "Assign failed");
            int pid = combo_get_selected_project_id(GTK_COMBO_BOX_TEXT(app->combo_projects_tasks));
            tasks_load(app, pid);
        }
    }
    gtk_widget_destroy(dialog);
}

static void on_task_update_status(GtkButton *btn, gpointer user_data) {
    (void)btn;
    App *app = user_data;
    int task_id = get_selected_task_id(app);
    if (task_id <= 0) { show_error(GTK_WINDOW(app->main_win), "Select a task first"); return; }

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Update Progress (%)",
        GTK_WINDOW(app->main_win),
        GTK_DIALOG_MODAL,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Update", GTK_RESPONSE_OK,
        NULL
    );
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    GtkAdjustment *adj = gtk_adjustment_new(0, 0, 100, 1, 10, 0);
    GtkWidget *spin = gtk_spin_button_new(adj, 1, 0);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), TRUE);
    gtk_box_pack_start(GTK_BOX(box), spin, FALSE, FALSE, 8);

    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        int progress = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
        if (progress < 0) progress = 0;
        if (progress > 100) progress = 100;

        char line[BUF_SIZE];
        snprintf(line, sizeof(line), "%s|%d|%d\n", CMD_UPDATE_TASK_PROGRESS, task_id, progress);

        char payload[BUF_SIZE] = {0};
        int code = net_request(app->sockfd, line, payload, sizeof(payload));
        if (code != 0) show_error(GTK_WINDOW(app->main_win), payload[0] ? payload : "Update progress failed");

        int pid = combo_get_selected_project_id(GTK_COMBO_BOX_TEXT(app->combo_projects_tasks));
        tasks_load(app, pid);
        gantt_load(app, pid);
    }

    gtk_widget_destroy(dialog);
}


static void on_task_set_dates(GtkButton *btn, gpointer user_data) {
    (void)btn;
    App *app = user_data;
    int task_id = get_selected_task_id(app);
    if (task_id <= 0) { show_error(GTK_WINDOW(app->main_win), "Select a task first"); return; }

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Set Task Dates (YYYY-MM-DD)", GTK_WINDOW(app->main_win),
        GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "Update", GTK_RESPONSE_OK, NULL);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *e_start = gtk_entry_new();
    GtkWidget *e_end = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(e_start), "Start date (YYYY-MM-DD)");
    gtk_entry_set_placeholder_text(GTK_ENTRY(e_end), "End date (YYYY-MM-DD)");
    gtk_box_pack_start(GTK_BOX(box), e_start, FALSE, FALSE, 8);
    gtk_box_pack_start(GTK_BOX(box), e_end, FALSE, FALSE, 8);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char *start = gtk_entry_get_text(GTK_ENTRY(e_start));
        const char *end = gtk_entry_get_text(GTK_ENTRY(e_end));
        if (!start || !*start || !end || !*end) {
            show_error(GTK_WINDOW(app->main_win), "Start and end are required");
        } else {
            char line[BUF_SIZE];
            snprintf(line, sizeof(line), "%s|%d|%s|%s\n", CMD_SET_TASK_DATES, task_id, start, end);
            char payload[BUF_SIZE] = {0};
            int code = net_request(app->sockfd, line, payload, sizeof(payload));
            if (code != 0) show_error(GTK_WINDOW(app->main_win), payload[0] ? payload : "Update dates failed");
            int pid = combo_get_selected_project_id(GTK_COMBO_BOX_TEXT(app->combo_projects_tasks));
            tasks_load(app, pid);
            gantt_load(app, pid);
        }
    }
    gtk_widget_destroy(dialog);
}

// -------------------- Gantt tab --------------------
static void gantt_tasks_clear(App *app) {
    if (!app->gantt_tasks) return;
    for (guint i=0;i<app->gantt_tasks->len;i++) g_free(g_ptr_array_index(app->gantt_tasks, i));
    g_ptr_array_set_size(app->gantt_tasks, 0);
}

static void gantt_load(App *app, int project_id) {
    gantt_tasks_clear(app);
    if (project_id <= 0) { gtk_widget_queue_draw(app->gantt_area); return; }

    char line[128];
    snprintf(line, sizeof(line), "%s|%d\n", CMD_LIST_TASK_GANTT, project_id);
    char payload[BUF_SIZE] = {0};
    net_request(app->sockfd, line, payload, sizeof(payload));
    g_print("GANTT payload:\\n%s\\n", payload);

    if (payload[0]) {
        char *copy = g_strdup(payload);
        char *saveptr=NULL;
        for (char *ln=strtok_r(copy,"\n",&saveptr); ln; ln=strtok_r(NULL,"\n",&saveptr)) {
            if (!*ln) continue;
            g_ptr_array_add(app->gantt_tasks, g_strdup(ln));
        }
        g_free(copy);
    }

    gtk_widget_queue_draw(app->gantt_area);
}

static void on_gantt_project_changed(GtkComboBox *combo, gpointer user_data) {
    App *app = user_data;
    int pid = combo_get_selected_project_id(GTK_COMBO_BOX_TEXT(combo));
    gantt_load(app, pid);
}

static gboolean gantt_draw_cb(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    App *app = user_data;
    GtkAllocation a;
    gtk_widget_get_allocation(widget, &a);

    // background
    cairo_set_source_rgb(cr, 1,1,1);
    cairo_rectangle(cr, 0,0, a.width, a.height);
    cairo_fill(cr);

    // header
    cairo_set_source_rgb(cr, 0,0,0);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 14);
    cairo_move_to(cr, 10, 20);
    cairo_show_text(cr, "Gantt (simple) - tasks must have Start/End YYYY-MM-DD");

    if (!app->gantt_tasks || app->gantt_tasks->len == 0) {
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 12);
        cairo_move_to(cr, 10, 50);
        cairo_show_text(cr, "No tasks / no data.");
        return FALSE;
    }

    int min_day = 2147483647, max_day = -2147483647;

    // id, title, assignee, status (%=progress), start-day, end-day
    typedef struct {
        int  id;
        char title[128];
        char assignee[64];
        int  status;   // % hoÃ n thÃ nh
        int  sday;
        int  eday;
    } T;

    GArray *items = g_array_new(FALSE, FALSE, sizeof(T));

    // -------- parse payload -> items[] --------
    for (guint i = 0; i < app->gantt_tasks->len; i++) {
        const char *ln = g_ptr_array_index(app->gantt_tasks, i);
        // format: id|title|Status:TXT|Progress:NN|Start:..|End:..|Assignee:..
        char *tmp = g_strdup(ln);
        char *parts[10] = {0}; int k = 0; char *sv = NULL;
        for (char *p = strtok_r(tmp, "|", &sv); p && k < 10; p = strtok_r(NULL, "|", &sv))
            parts[k++] = p;
        if (k < 7) { g_free(tmp); continue; }

        T t;
        memset(&t, 0, sizeof(t));
        t.id = atoi(parts[0]);
        snprintf(t.title, sizeof(t.title), "%s", parts[1]);

        const char *status_txt = parts[2] + strlen("Status:");

        int progress = 0;
        if (g_str_has_prefix(parts[3], "Progress:"))
            progress = atoi(parts[3] + strlen("Progress:"));

        // status trong struct = % hoÃ n thÃ nh
        if (g_strcmp0(status_txt, "DONE") == 0)
            t.status = 100;
        else if (g_strcmp0(status_txt, "IN_PROGRESS") == 0)
            t.status = progress;
        else
            t.status = 0;   // NOT_STARTED, v.v.

        snprintf(t.assignee, sizeof(t.assignee), "%s",
                 parts[6] + strlen("Assignee:"));

        const char *sd = parts[4] + strlen("Start:");
        const char *ed = parts[5] + strlen("End:");

        int y, m, d;
        if (sscanf(sd, "%d-%d-%d", &y,&m,&d)==3) t.sday = y*372 + m*31 + d; else t.sday = 0;
        if (sscanf(ed, "%d-%d-%d", &y,&m,&d)==3) t.eday = y*372 + m*31 + d; else t.eday = t.sday;

        if (t.sday > 0) {
            if (t.sday < min_day) min_day = t.sday;
            if (t.eday > max_day) max_day = t.eday;
        }

        g_array_append_val(items, t);
        g_free(tmp);
    }

    if (items->len == 0 || min_day==2147483647 || max_day==-2147483647) {
        cairo_set_font_size(cr, 12);
        cairo_move_to(cr, 10, 50);
        cairo_show_text(cr, "No valid Start/End dates. Use 'Set Task Dates' in Tasks tab.");
        g_array_free(items, TRUE);
        return FALSE;
    }

    int span = (max_day - min_day) + 1;
    double left = 220;
    double top = 50;
    double row_h = 24;
    double bar_h = 12;
    double width = a.width - left - 20;
    const double AXIS_H = 20.0;   // chiều cao vùng nhãn ngày
    if (width < 100) width = 100;

    // grid + nhãn ngày
cairo_select_font_face(cr, "Sans",
                       CAIRO_FONT_SLANT_NORMAL,
                       CAIRO_FONT_WEIGHT_NORMAL);
cairo_set_font_size(cr, 10);

for (int g = 0; g <= span; g++) {
    double x = left + (g * (width / span));

    // vạch dọc
    cairo_set_source_rgb(cr, 0.9,0.9,0.9);
    cairo_move_to(cr, x, top);
    cairo_line_to(cr, x, top + AXIS_H + items->len*row_h);
    cairo_stroke(cr);

    if (g < span) {  // vẽ nhãn cho từng ngày
        int day_code = min_day + g;
        int tmp = day_code % 372;
        int m0  = tmp / 31;        // 0..12
        int d0  = tmp % 31;        // 0..30

        int m = (m0 <= 0) ? 12 : m0;   // nếu 0 thì hiểu là 12
        int d = (d0 <= 0) ? 1  : d0;

        char buf[16];
        snprintf(buf, sizeof(buf), "%02d-%02d", m, d);

        cairo_set_source_rgb(cr, 0,0,0);
        cairo_move_to(cr, x + 2, top + AXIS_H - 5);
        cairo_show_text(cr, buf);
    }
}

// chuẩn bị lại style cho phần text còn lại
cairo_set_source_rgb(cr, 0,0,0);
cairo_set_font_size(cr, 12);

    // -------- task --------
    for (guint i = 0; i < items->len; i++) {
        T t = g_array_index(items, T, i);
        double y = top + AXIS_H + i*row_h;

        // label: id, title, %, assignee
        char label[200];
        snprintf(label, sizeof(label), "#%d %s (%d%%, %s)",
                 t.id, t.title, t.status, t.assignee);
        cairo_move_to(cr, 10, y+12);
        cairo_show_text(cr, label);

        int s = (t.sday>0)? (t.sday - min_day) : 0;
        int e = (t.eday>0)? (t.eday - min_day) : s;
        if (e < s) e = s;

        double x1 = left + (s*(width/span));
        double x2 = left + ((e+1)*(width/span));
        double by = y + 6;

        int p = t.status;   // % hoÃ n thÃ nh

        if (p >= 100)
            cairo_set_source_rgb(cr, 0.2, 0.7, 0.2);   // xanh lá
        else if (p >= 50)
            cairo_set_source_rgb(cr, 0.3, 0.6, 0.9);   // cam
        else
            cairo_set_source_rgb(cr, 0.9, 0.6, 0.0);   // xanh dương

        cairo_rectangle(cr, x1, by, x2-x1, bar_h);
        cairo_fill(cr);

        // DÙNG LẠI CÙNG MÀU cho chữ
        cairo_move_to(cr, 10, y+12);
        cairo_show_text(cr, label);
    }

    g_array_free(items, TRUE);
    return FALSE;
}

static void chat_add_message(App *app,
                             const char *username,
                             const char *content,
                             const char *ts)
{
    if (!username) username = "";
    if (!content)  content  = "";
    if (!ts)       ts       = "";

    gboolean is_me = (app->current_user[0] &&
                      g_strcmp0(username, app->current_user) == 0);

    // row chứa bubble, để căn trái/phải
    GtkWidget *row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    if (is_me)
        gtk_widget_set_halign(row_box, GTK_ALIGN_END);
    else
        gtk_widget_set_halign(row_box, GTK_ALIGN_START);

    // bubble (box dọc): Tên -> Nội dung -> Timestamp
    GtkWidget *bubble = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(bubble, 2);
    gtk_widget_set_margin_bottom(bubble, 2);
    if (is_me) {
    gtk_widget_set_margin_start(bubble, 40);  // cách nội dung khỏi dính mép trái
    gtk_widget_set_margin_end(bubble, 4);     // sát mép phải
    } else {
    gtk_widget_set_margin_start(bubble, 4);   // sát mép trái
    gtk_widget_set_margin_end(bubble, 40);    // cách khỏi mép phải
    }

    // ----- tên user (nhỏ, xám) -----
    GtkWidget *lbl_name = gtk_label_new(username);
    gtk_label_set_xalign(GTK_LABEL(lbl_name), is_me ? 1.0 : 0.0);
    gtk_widget_set_margin_start(lbl_name, 6);
    gtk_widget_set_margin_end(lbl_name, 6);

    PangoAttrList *name_attrs = pango_attr_list_new();
    pango_attr_list_insert(name_attrs, pango_attr_scale_new(PANGO_SCALE_SMALL));
    gtk_label_set_attributes(GTK_LABEL(lbl_name), name_attrs);
    pango_attr_list_unref(name_attrs);

    GdkRGBA gray_name;
    gdk_rgba_parse(&gray_name, "#aaaaaa");
    gtk_widget_override_color(lbl_name, GTK_STATE_FLAG_NORMAL, &gray_name);

    // ----- nội dung (wrap tự xuống dòng) -----
    GtkWidget *lbl_msg = gtk_label_new(content);
    gtk_label_set_xalign(GTK_LABEL(lbl_msg), 0.0);
    gtk_widget_set_margin_start(lbl_msg, 6);
    gtk_widget_set_margin_end(lbl_msg, 6);
    gtk_widget_set_margin_top(lbl_msg, 4);

    // Quan trọng: bật wrap + giới hạn chiều rộng
    gtk_label_set_line_wrap(GTK_LABEL(lbl_msg), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(lbl_msg), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_max_width_chars(GTK_LABEL(lbl_msg), 40);   // có thể chỉnh 30–60 tuỳ ý

    // ----- timestamp nhỏ ở góc dưới -----
    GtkWidget *lbl_ts = gtk_label_new(ts);

// căn tuỳ user: mình bên phải, người khác bên trái
if (is_me) {
    gtk_label_set_xalign(GTK_LABEL(lbl_ts), 1.0);   // dính về bên phải
    gtk_widget_set_margin_start(lbl_ts, 6);        // cách nội dung 1 chút
    gtk_widget_set_margin_end(lbl_ts, 2);           // sát mép phải bubble
} else {
    gtk_label_set_xalign(GTK_LABEL(lbl_ts), 0.0);   // dính về bên trái
    gtk_widget_set_margin_start(lbl_ts, 2);         // sát mép trái bubble
    gtk_widget_set_margin_end(lbl_ts, 6);
}
gtk_widget_set_margin_bottom(lbl_ts, 2);


    PangoAttrList *ts_attrs = pango_attr_list_new();
    pango_attr_list_insert(ts_attrs, pango_attr_scale_new(PANGO_SCALE_SMALL));
    gtk_label_set_attributes(GTK_LABEL(lbl_ts), ts_attrs);
    pango_attr_list_unref(ts_attrs);

    GdkRGBA gray_ts;
    gdk_rgba_parse(&gray_ts, "#999999");
    gtk_widget_override_color(lbl_ts, GTK_STATE_FLAG_NORMAL, &gray_ts);

    // ghép vào bubble: Tên -> Nội dung -> Timestamp
    gtk_box_pack_start(GTK_BOX(bubble), lbl_name, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bubble), lbl_msg,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bubble), lbl_ts,   FALSE, FALSE, 0);

    // căn bubble trong row
    if (is_me)
        gtk_widget_set_halign(bubble, GTK_ALIGN_END);
    else
        gtk_widget_set_halign(bubble, GTK_ALIGN_START);

    gtk_box_pack_start(GTK_BOX(row_box), bubble, FALSE, FALSE, 0);
    gtk_list_box_insert(GTK_LIST_BOX(app->chat_list), row_box, -1);
    gtk_widget_show_all(row_box);
}

// -------------------- Chat tab --------------------
static gboolean chat_poll(gpointer user_data) {
    App *app = user_data;

    int pid = combo_get_selected_project_id(GTK_COMBO_BOX_TEXT(app->combo_projects_chat));
        g_print("chat_poll pid=%d last_chat_id=%d\n", pid, app->last_chat_id);
    if (pid <= 0) return TRUE;

    // Chá»‰ láº¥y message má»›i hÆ¡n last_chat_id
    char line[128];
    snprintf(line, sizeof(line), "%s|%d|%d\n", CMD_LIST_CHAT, pid, app->last_chat_id);

    char payload[BUF_SIZE] = {0};
    net_request(app->sockfd, line, payload, sizeof(payload));

    if (!payload[0]) return TRUE;

    char *copy = g_strdup(payload);
    char *saveptr = NULL;
    for (char *ln = strtok_r(copy, "\n", &saveptr); ln; ln = strtok_r(NULL, "\n", &saveptr)) {
        if (!*ln) continue;

        char *tmp = g_strdup(ln);
        char *p[4] = {0}; int k = 0; char *sv = NULL;
        for (char *x = strtok_r(tmp, "|", &sv); x && k < 4; x = strtok_r(NULL, "|", &sv))
            p[k++] = x;

        if (k == 4) {
    int msg_id = atoi(p[0]);
    if (msg_id > app->last_chat_id)
        app->last_chat_id = msg_id;

    const char *user    = p[1];
    const char *content = p[2];
    const char *raw_ts  = p[3];   // "YYYY-MM-DD HH:MM:SS" từ SQLite (UTC)

    struct tm tm_utc;
    memset(&tm_utc, 0, sizeof(tm_utc));

    // parse chuỗi thời gian
    if (strptime(raw_ts, "%Y-%m-%d %H:%M:%S", &tm_utc)) {
        // chuyển sang time_t UTC
        time_t t;
#if defined(_GNU_SOURCE) || defined(__APPLE__)
        t = timegm(&tm_utc);          // trên macOS / glibc
#else
        // nếu không có timegm, tạm dùng mktime và đặt TZ=UTC trước khi chạy app
        t = mktime(&tm_utc);
#endif
      
        struct tm tm_vn;
        localtime_r(&t, &tm_vn);

        char vn_ts[64];
        strftime(vn_ts, sizeof(vn_ts), "%Y-%m-%d %H:%M:%S", &tm_vn);

        chat_add_message(app, user, content, vn_ts);
    } else {
        // nếu parse lỗi thì dùng chuỗi gốc
        chat_add_message(app, user, content, raw_ts);
    }
}

        g_free(tmp);
    }
    g_free(copy);
    return TRUE;
}

static void on_chat_send(GtkButton *btn, gpointer user_data) {
    (void)btn;
    App *app = user_data;

    int pid = combo_get_selected_project_id(GTK_COMBO_BOX_TEXT(app->combo_projects_chat));
    const char *content = gtk_entry_get_text(GTK_ENTRY(app->entry_chat));
    if (pid <= 0) { show_error(GTK_WINDOW(app->main_win), "Select a project"); return; }
    if (!content || !*content) return;

    char line[BUF_SIZE];
    snprintf(line, sizeof(line), "%s|%d|%s\n", CMD_SEND_CHAT, pid, content);
    char payload[BUF_SIZE] = {0};
    int code = net_request(app->sockfd, line, payload, sizeof(payload));
    if (code != 0) show_error(GTK_WINDOW(app->main_win), payload[0] ? payload : "Send chat failed");
    gtk_entry_set_text(GTK_ENTRY(app->entry_chat), "");

    // Láº¥y ngay message má»›i (id > last_chat_id cÅ©)
    chat_poll(app);
}

static void on_chat_project_changed(GtkComboBox *combo, gpointer user_data) {
    App *app = user_data;
    (void)combo;

    // xoá sạch các row cũ trong list
GList *children = gtk_container_get_children(GTK_CONTAINER(app->chat_list));
for (GList *l = children; l; l = l->next)
    gtk_widget_destroy(GTK_WIDGET(l->data));
g_list_free(children);

// reset để project mới load lại từ đầu
app->last_chat_id = 0;

}

// -------------------- Build UI --------------------
static GtkWidget* build_projects_tab(App *app) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    app->project_store = gtk_list_store_new(2, G_TYPE_INT, G_TYPE_STRING);
    GtkWidget *view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(app->project_store));
    app->project_store_view = view;

    GtkCellRenderer *r1 = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *c1 = gtk_tree_view_column_new_with_attributes("ID", r1, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view), c1);

    GtkCellRenderer *r2 = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *c2 = gtk_tree_view_column_new_with_attributes("Project", r2, "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view), c2);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    gtk_widget_set_vexpand(scroll, TRUE);

    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *btn_refresh = gtk_button_new_with_label("Refresh");
    GtkWidget *btn_create  = gtk_button_new_with_label("Create");
    GtkWidget *btn_invite  = gtk_button_new_with_label("Invite");
    gtk_box_pack_start(GTK_BOX(hbox), btn_refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), btn_create,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), btn_invite,  FALSE, FALSE, 0);

    g_signal_connect(btn_refresh, "clicked", G_CALLBACK(on_project_refresh), app);
    g_signal_connect(btn_create,  "clicked", G_CALLBACK(on_project_create),  app);
    g_signal_connect(btn_invite,  "clicked", G_CALLBACK(on_project_invite),  app);

    gtk_box_pack_start(GTK_BOX(vbox), hbox,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE,  TRUE,  0);

    return vbox;
}

static GtkWidget* build_tasks_tab(App *app) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    app->combo_projects_tasks = gtk_combo_box_text_new();
    GtkWidget *btn_refresh = gtk_button_new_with_label("Refresh");
    GtkWidget *btn_create = gtk_button_new_with_label("Create Task");
    GtkWidget *btn_assign = gtk_button_new_with_label("Assign");
    GtkWidget *btn_status = gtk_button_new_with_label("Update Status");
    GtkWidget *btn_dates = gtk_button_new_with_label("Set Dates");
    GtkWidget *btn_comments = gtk_button_new_with_label("Comments");
    GtkWidget *btn_files = gtk_button_new_with_label("Files");

    gtk_box_pack_start(GTK_BOX(top), gtk_label_new("Project:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), app->combo_projects_tasks, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), btn_refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), btn_create, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), btn_assign, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), btn_status, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), btn_dates, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), btn_comments, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), btn_files, FALSE, FALSE, 0);

    g_signal_connect(btn_refresh, "clicked", G_CALLBACK(on_tasks_refresh), app);
    g_signal_connect(app->combo_projects_tasks, "changed", G_CALLBACK(on_tasks_project_changed), app);
    g_signal_connect(btn_create, "clicked", G_CALLBACK(on_task_create), app);
    g_signal_connect(btn_assign, "clicked", G_CALLBACK(on_task_assign), app);
    g_signal_connect(btn_status, "clicked", G_CALLBACK(on_task_update_status), app);
    g_signal_connect(btn_dates, "clicked", G_CALLBACK(on_task_set_dates), app);
    g_signal_connect(btn_comments, "clicked", G_CALLBACK(on_task_comments), app);
    g_signal_connect(btn_files, "clicked", G_CALLBACK(on_task_attachments), app);

    app->task_store = gtk_list_store_new(6,
        G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    app->task_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(app->task_store));

    const char *headers[] = {"ID", "Title", "Assignee", "Status", "Start", "End"};
    for (int i=0;i<6;i++) {
        GtkCellRenderer *r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(headers[i], r, "text", i, NULL);
        gtk_tree_view_append_column(GTK_TREE_VIEW(app->task_view), c);
    }

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), app->task_view);
    gtk_widget_set_vexpand(scroll, TRUE);

    gtk_box_pack_start(GTK_BOX(vbox), top, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    return vbox;
}

static GtkWidget* build_gantt_tab(App *app) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    app->combo_projects_gantt = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(top), gtk_label_new("Project:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), app->combo_projects_gantt, FALSE, FALSE, 0);

    g_signal_connect(app->combo_projects_gantt, "changed", G_CALLBACK(on_gantt_project_changed), app);

    app->gantt_area = gtk_drawing_area_new();
    gtk_widget_set_vexpand(app->gantt_area, TRUE);
    g_signal_connect(app->gantt_area, "draw", G_CALLBACK(gantt_draw_cb), app);

    gtk_box_pack_start(GTK_BOX(vbox), top, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), app->gantt_area, TRUE, TRUE, 0);

    return vbox;
}

static GtkWidget* build_chat_tab(App *app) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    // combobox chọn project chat (giữ giống logic cũ)
    GtkWidget *hproj = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *lbl = gtk_label_new("Project:");
    app->combo_projects_chat = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(hproj), lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hproj), app->combo_projects_chat, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hproj, FALSE, FALSE, 0);

    g_signal_connect(app->combo_projects_chat, "changed",
                     G_CALLBACK(on_chat_project_changed), app);

    // khu vực list message
    GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(sw, TRUE);

    app->chat_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(app->chat_list),
                                    GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(sw), app->chat_list);

    gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 0);

    // input + nút Send
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    app->entry_chat = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->entry_chat), "Type message...");
    GtkWidget *btn_send = gtk_button_new_with_label("Send");
    gtk_box_pack_start(GTK_BOX(hbox), app->entry_chat, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), btn_send, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    g_signal_connect(btn_send, "clicked", G_CALLBACK(on_chat_send), app);

    return vbox;
}


static void build_main_window(App *app) {
    app->main_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->main_win), "QLCV - GTK Client");
    gtk_window_set_default_size(GTK_WINDOW(app->main_win), 1000, 650);

    GtkWidget *notebook = gtk_notebook_new();
    gtk_container_add(GTK_CONTAINER(app->main_win), notebook);

    GtkWidget *tab1 = build_projects_tab(app);
    GtkWidget *tab2 = build_tasks_tab(app);
    GtkWidget *tab3 = build_gantt_tab(app);
    GtkWidget *tab4 = build_chat_tab(app);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tab1, gtk_label_new("Projects"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tab2, gtk_label_new("Tasks"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tab3, gtk_label_new("Gantt"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tab4, gtk_label_new("Chat"));

    g_signal_connect(app->main_win, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // init gantt data container
    app->gantt_tasks = g_ptr_array_new();

    projects_load(app);

    int pid = combo_get_selected_project_id(GTK_COMBO_BOX_TEXT(app->combo_projects_tasks));
    tasks_load(app, pid);
    gantt_load(app, combo_get_selected_project_id(GTK_COMBO_BOX_TEXT(app->combo_projects_gantt)));

    app->last_chat_id = 0;

    // poll chat every 1s
    g_timeout_add(1000, chat_poll, app);   // [web:57]

    gtk_widget_show_all(app->main_win);
}

// -------------------- Login window --------------------
static void on_login_action(App *app, gboolean is_register) {
    const char *u = gtk_entry_get_text(GTK_ENTRY(app->entry_user));
    const char *p = gtk_entry_get_text(GTK_ENTRY(app->entry_pass));
    if (!u || !*u || !p || !*p) {
        gtk_label_set_text(GTK_LABEL(app->label_login_status), "Username & password required");
        return;
    }

    char line[BUF_SIZE];
    snprintf(line, sizeof(line), "%s|%s|%s\n", is_register ? CMD_REGISTER : CMD_LOGIN, u, p);
    char payload[BUF_SIZE] = {0};
    int code = net_request(app->sockfd, line, payload, sizeof(payload));

    if (code == 0) {
        gtk_label_set_text(GTK_LABEL(app->label_login_status), is_register ? "Register OK" : "Login OK");
        if (!is_register) {
            // lưu username hiện tại
        g_strlcpy(app->current_user, u ? u : "", sizeof(app->current_user));
        gtk_widget_hide(app->login_win);
        build_main_window(app);
        }
    } else {
        gtk_label_set_text(GTK_LABEL(app->label_login_status), payload[0] ? payload : "Request failed");
    }
}

static void on_login_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    on_login_action((App*)user_data, FALSE);
}

static void on_register_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    on_login_action((App*)user_data, TRUE);
}

static void build_login_window(App *app) {
    app->login_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->login_win), "QLCV - Login");
    gtk_window_set_default_size(GTK_WINDOW(app->login_win), 380, 220);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
    gtk_container_add(GTK_CONTAINER(app->login_win), grid);

    app->entry_user = gtk_entry_new();
    app->entry_pass = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->entry_user), "Username");
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->entry_pass), "Password");
    gtk_entry_set_visibility(GTK_ENTRY(app->entry_pass), FALSE);

    GtkWidget *btn_login = gtk_button_new_with_label("Login");
    GtkWidget *btn_reg = gtk_button_new_with_label("Register");
    app->label_login_status = gtk_label_new("");

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Username"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), app->entry_user, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Password"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), app->entry_pass, 1, 1, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), btn_login, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), btn_reg, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), app->label_login_status, 0, 3, 2, 1);

    g_signal_connect(btn_login, "clicked", G_CALLBACK(on_login_clicked), app);
    g_signal_connect(btn_reg, "clicked", G_CALLBACK(on_register_clicked), app);

    g_signal_connect(app->login_win, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    gtk_widget_show_all(app->login_win);
}

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    g_object_set(gtk_settings_get_default(),
             "gtk-application-prefer-dark-theme",
             TRUE,
             NULL);

    App app;
    memset(&app, 0, sizeof(app));

    app.sockfd = connect_server();
    if (app.sockfd < 0) {
        fprintf(stderr, "Cannot connect to server at 127.0.0.1:%d\n", SERVER_PORT);
        return 1;
    }

    build_login_window(&app);
    gtk_main();

    if (app.gantt_tasks) {
        gantt_tasks_clear(&app);
        g_ptr_array_free(app.gantt_tasks, TRUE);
    }
    close(app.sockfd);
    return 0;
}