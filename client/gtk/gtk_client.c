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

    GtkTextBuffer *chat_buffer;
    GtkWidget *entry_chat;

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

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Create Project", GTK_WINDOW(app->main_win),
        GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "Create", GTK_RESPONSE_OK, NULL);

    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Project name");
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 8);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char *name = gtk_entry_get_text(GTK_ENTRY(entry));
        if (!name || !*name) {
            show_error(GTK_WINDOW(dialog), "Project name is required");
        } else {
            char line[BUF_SIZE];
            snprintf(line, sizeof(line), "%s|%s\n", CMD_CREATE_PROJECT, name);
            char payload[BUF_SIZE] = {0};
            int code = net_request(app->sockfd, line, payload, sizeof(payload));
            if (code != 0) show_error(GTK_WINDOW(app->main_win), payload[0] ? payload : "Create project failed");
            projects_load(app);
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
        const char *assignee = (k>=3)? parts[2] : "Assignee:None";
        const char *status = (k>=4)? parts[3] : "Status:NOT_STARTED";
        const char *start = (k>=5)? parts[4] : "Start:";
        const char *end = (k>=6)? parts[5] : "End:";

        GtkTreeIter it;
        gtk_list_store_append(app->task_store, &it);
        gtk_list_store_set(app->task_store, &it,
            0, id,
            1, title,
            2, assignee,
            3, status,
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

static void on_task_create(GtkButton *btn, gpointer user_data) {
    (void)btn;
    App *app = user_data;
    int pid = combo_get_selected_project_id(GTK_COMBO_BOX_TEXT(app->combo_projects_tasks));
    if (pid <= 0) { show_error(GTK_WINDOW(app->main_win), "Select a project first"); return; }

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Create Task", GTK_WINDOW(app->main_win),
        GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "Create", GTK_RESPONSE_OK, NULL);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *e_title = gtk_entry_new();
    GtkWidget *e_desc = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(e_title), "Task title");
    gtk_entry_set_placeholder_text(GTK_ENTRY(e_desc), "Description");
    gtk_box_pack_start(GTK_BOX(box), e_title, FALSE, FALSE, 8);
    gtk_box_pack_start(GTK_BOX(box), e_desc, FALSE, FALSE, 8);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char *title = gtk_entry_get_text(GTK_ENTRY(e_title));
        const char *desc = gtk_entry_get_text(GTK_ENTRY(e_desc));
        if (!title || !*title || !desc || !*desc) {
            show_error(GTK_WINDOW(app->main_win), "Title and description are required");
        } else {
            char line[BUF_SIZE];
            snprintf(line, sizeof(line), "%s|%d|%s|%s\n", CMD_CREATE_TASK, pid, title, desc);
            char payload[BUF_SIZE] = {0};
            int code = net_request(app->sockfd, line, payload, sizeof(payload));
            if (code != 0) show_error(GTK_WINDOW(app->main_win), payload[0] ? payload : "Create task failed");
            tasks_load(app, pid);
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

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Update Status", GTK_WINDOW(app->main_win),
        GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "Update", GTK_RESPONSE_OK, NULL);
    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    GtkWidget *combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "NOT_STARTED");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "IN_PROGRESS");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), "DONE");
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    gtk_box_pack_start(GTK_BOX(box), combo, FALSE, FALSE, 8);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char *status = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(combo));
        char line[BUF_SIZE];
        snprintf(line, sizeof(line), "%s|%d|%s\n", CMD_UPDATE_TASK_STATUS, task_id, status);
        g_free((gpointer)status);
        char payload[BUF_SIZE] = {0};
        int code = net_request(app->sockfd, line, payload, sizeof(payload));
        if (code != 0) show_error(GTK_WINDOW(app->main_win), payload[0] ? payload : "Update status failed");
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
    typedef struct { int id; char title[128]; char status[32]; char assignee[64]; int sday; int eday; } T;
    GArray *items = g_array_new(FALSE, FALSE, sizeof(T));

    for (guint i=0;i<app->gantt_tasks->len;i++) {
        const char *ln = g_ptr_array_index(app->gantt_tasks, i);
        // format: id|title|Status:..|Start:..|End:..|Assignee:..
        char *tmp = g_strdup(ln);
        char *parts[8]={0}; int k=0; char *sv=NULL;
        for (char *p=strtok_r(tmp,"|",&sv); p && k<8; p=strtok_r(NULL,"|",&sv)) parts[k++]=p;
        if (k < 6) { g_free(tmp); continue; }
        T t; memset(&t,0,sizeof(t));
        t.id = atoi(parts[0]);
        snprintf(t.title, sizeof(t.title), "%s", parts[1]);
        snprintf(t.status, sizeof(t.status), "%s", parts[2]+7);
        const char *sd = parts[3]+6;
        const char *ed = parts[4]+4;
        snprintf(t.assignee, sizeof(t.assignee), "%s", parts[5]+9);

        int y,m,d;
        if (sscanf(sd, "%d-%d-%d", &y,&m,&d)==3) t.sday = y*372 + m*31 + d; else t.sday = 0;
        if (sscanf(ed, "%d-%d-%d", &y,&m,&d)==3) t.eday = y*372 + m*31 + d; else t.eday = t.sday;
        if (t.sday>0) {
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
    if (width < 100) width = 100;

    cairo_set_source_rgb(cr, 0.9,0.9,0.9);
    for (int g=0; g<=span; g++) {
        double x = left + (g*(width/span));
        cairo_move_to(cr, x, top-10);
        cairo_line_to(cr, x, top + items->len*row_h);
    }
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, 0,0,0);
    cairo_set_font_size(cr, 12);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

    for (guint i=0; i<items->len; i++) {
        T t = g_array_index(items, T, i);
        double y = top + i*row_h;

        char label[200];
        snprintf(label, sizeof(label), "#%d %s (%s)", t.id, t.title, t.status);
        cairo_set_source_rgb(cr, 0,0,0);
        cairo_move_to(cr, 10, y+12);
        cairo_show_text(cr, label);

        int s = (t.sday>0)? (t.sday - min_day) : 0;
        int e = (t.eday>0)? (t.eday - min_day) : s;
        if (e < s) e = s;

        double x1 = left + (s*(width/span));
        double x2 = left + ((e+1)*(width/span));
        double by = y + 6;

        if (g_strcmp0(t.status, "DONE") == 0) cairo_set_source_rgb(cr, 0.2,0.2,0.2);
        else if (g_strcmp0(t.status, "IN_PROGRESS") == 0) cairo_set_source_rgb(cr, 0.4,0.4,0.4);
        else cairo_set_source_rgb(cr, 0.6,0.6,0.6);

        cairo_rectangle(cr, x1, by, x2-x1, bar_h);
        cairo_fill(cr);
    }

    g_array_free(items, TRUE);
    return FALSE;
}

// -------------------- Chat tab --------------------
static gboolean chat_poll(gpointer user_data) {
    App *app = user_data;

    int pid = combo_get_selected_project_id(GTK_COMBO_BOX_TEXT(app->combo_projects_chat));
        g_print("chat_poll pid=%d last_chat_id=%d\n", pid, app->last_chat_id);
    if (pid <= 0) return TRUE;

    // Chỉ lấy message mới hơn last_chat_id
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

            char msg[1024];
            snprintf(msg, sizeof(msg), "[%s] %s: %s\n", p[3], p[1], p[2]);
            GtkTextIter end;
            gtk_text_buffer_get_end_iter(app->chat_buffer, &end);
            gtk_text_buffer_insert(app->chat_buffer, &end, msg, -1);
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

    // Lấy ngay message mới (id > last_chat_id cũ)
    chat_poll(app);
}

static void on_chat_project_changed(GtkComboBox *combo, gpointer user_data) {
    App *app = user_data;
    (void)combo;

    // XÓA SẠCH chat cũ
    gtk_text_buffer_set_text(app->chat_buffer, "", -1);
    // Reset để project mới load lại từ đầu
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
    GtkWidget *btn_create = gtk_button_new_with_label("Create");
    GtkWidget *btn_invite = gtk_button_new_with_label("Invite");
    gtk_box_pack_start(GTK_BOX(hbox), btn_refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), btn_create, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), btn_invite, FALSE, FALSE, 0);

    g_signal_connect(btn_refresh, "clicked", G_CALLBACK(on_project_refresh), app);
    g_signal_connect(btn_create, "clicked", G_CALLBACK(on_project_create), app);
    g_signal_connect(btn_invite, "clicked", G_CALLBACK(on_project_invite), app);

    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

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

    gtk_box_pack_start(GTK_BOX(top), gtk_label_new("Project:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), app->combo_projects_tasks, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), btn_refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), btn_create, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), btn_assign, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), btn_status, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), btn_dates, FALSE, FALSE, 0);

    g_signal_connect(btn_refresh, "clicked", G_CALLBACK(on_tasks_refresh), app);
    g_signal_connect(app->combo_projects_tasks, "changed", G_CALLBACK(on_tasks_project_changed), app);
    g_signal_connect(btn_create, "clicked", G_CALLBACK(on_task_create), app);
    g_signal_connect(btn_assign, "clicked", G_CALLBACK(on_task_assign), app);
    g_signal_connect(btn_status, "clicked", G_CALLBACK(on_task_update_status), app);
    g_signal_connect(btn_dates, "clicked", G_CALLBACK(on_task_set_dates), app);

    app->task_store = gtk_list_store_new(6, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
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

    GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    app->combo_projects_chat = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(top), gtk_label_new("Project:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), app->combo_projects_chat, FALSE, FALSE, 0);
    g_signal_connect(app->combo_projects_chat, "changed", G_CALLBACK(on_chat_project_changed), app);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *tv = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
    app->chat_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv));
    gtk_container_add(GTK_CONTAINER(scroll), tv);
    gtk_widget_set_vexpand(scroll, TRUE);

    GtkWidget *bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    app->entry_chat = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->entry_chat), "Type message...");
    GtkWidget *btn_send = gtk_button_new_with_label("Send");
    gtk_box_pack_start(GTK_BOX(bottom), app->entry_chat, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(bottom), btn_send, FALSE, FALSE, 0);
    g_signal_connect(btn_send, "clicked", G_CALLBACK(on_chat_send), app);

    gtk_box_pack_start(GTK_BOX(vbox), top, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), bottom, FALSE, FALSE, 0);

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
