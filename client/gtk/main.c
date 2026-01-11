#include <gtk/gtk.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "../common.h"
#include "../protocol.h"
#include "net.h"

// Struct App: giữ toàn bộ state của ứng dụng client
typedef struct {
    int sockfd;              // socket TCP kết nối tới server
    char username[64];       // username đã login
    int current_project_id;  // project đang chọn (nếu cần)
    int last_chat_id;        // id cuối cùng của message chat đã nhận

    // windows chính
    GtkWidget *login_win;    // cửa sổ login
    GtkWidget *main_win;     // cửa sổ main (sau khi login)

    // login widgets
    GtkEntry *entry_user;    // ô nhập username
    GtkEntry *entry_pass;    // ô nhập password
    GtkLabel *login_status;  // label hiển thị trạng thái login/register

    // projects widgets
    GtkListStore *projects_store;   // model danh sách project (id, name)
    GtkTreeView *projects_view;     // TreeView hiển thị project
    GtkEntry *create_project_entry; // ô nhập tên project mới
    GtkEntry *invite_user_entry;    // ô nhập username để mời vào project

    // tasks widgets
    GtkComboBoxText *tasks_project_combo; // combo chọn project để load tasks
    GtkListStore *tasks_store;           // model danh sách task
    GtkTreeView *tasks_view;            // TreeView hiển thị tasks

    // các control nhập/chỉnh task
    GtkEntry *task_title_entry;   // tiêu đề task
    GtkEntry *task_desc_entry;    // mô tả task
    GtkEntry *assign_user_entry;  // username assignee (tạo/assign)
    GtkEntry *task_id_entry;      // nhập ID task thủ công khi không chọn dòng
    GtkComboBoxText *status_combo;// (dự phòng) chọn status
    GtkSpinButton *progress_spin; // chọn % progress khi update
    GtkEntry *start_date_entry;   // ngày bắt đầu (YYYY-MM-DD)
    GtkEntry *end_date_entry;     // ngày kết thúc (YYYY-MM-DD)

    // comments/attachments widgets
    GtkTextView *comment_text;         // ô nhập nội dung comment
    GtkEntry *comment_task_id_entry;   // nhập Task ID cho comment
    GtkTextView *comments_list_text;   // TextView hiển thị list comment

    GtkEntry *attach_task_id_entry;    // Task ID cho attachment
    GtkEntry *attach_filename_entry;   // tên file (nếu cần)
    GtkEntry *attach_filepath_entry;   // đường dẫn file trên máy client
    GtkTextView *attachments_list_text;// TextView list attachments

    // Gantt
    GtkDrawingArea *gantt_area;   // vùng vẽ Gantt
    int gantt_project_id;         // project đang hiển thị Gantt
    char gantt_cache[4096];       // cache raw payload Gantt từ server

    // chat
    GtkComboBoxText *chat_project_combo; // chọn project cho chat
    GtkTextView *chat_view;              // TextView hiển thị nội dung chat
    GtkEntry *chat_entry;                // ô nhập tin nhắn chat
} App;

// forward cho 2 handler comment/attachment (được dùng ở phần dưới)
static void on_btn_list_comments(GtkButton *btn, gpointer user_data);
static void on_btn_list_attachments(GtkButton *btn, gpointer user_data);

// Hàm tiện ích hiển thị message dialog (info/error/warning)
static void show_msg(GtkWindow *parent, GtkMessageType type,
                     const char *title, const char *msg) {
    GtkWidget *d = gtk_message_dialog_new(parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        type,
        GTK_BUTTONS_OK,
        "%s", msg ? msg : "");
    gtk_window_set_title(GTK_WINDOW(d), title);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

// Tạo socket và connect tới server 127.0.0.1:SERVER_PORT
static int connect_server(void) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        return -1;
    }
    return sockfd;
}

// Clear toàn bộ model projects
static void projects_store_clear(App *a) {
    gtk_list_store_clear(a->projects_store);
}

// Clear toàn bộ model tasks
static void tasks_store_clear(App *a) {
    gtk_list_store_clear(a->tasks_store);
}

// Khai báo trước một số hàm refresh
static void refresh_projects(App *a);
static void refresh_tasks(App *a);
static void refresh_gantt(App *a, int project_id);

// Lấy project id đang được chọn trong TreeView projects_view
static int get_selected_project_id(App *a) {
    GtkTreeSelection *sel = gtk_tree_view_get_selection(a->projects_view);
    GtkTreeIter iter;
    GtkTreeModel *model;
    if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
        int pid = 0;
        gtk_tree_model_get(model, &iter, 0, &pid, -1); // cột 0 = id
        return pid;
    }
    return 0;
}

// Lấy task id đang được chọn trong TreeView tasks_view
static int get_selected_task_id(App *a) {
    GtkTreeSelection *sel = gtk_tree_view_get_selection(a->tasks_view);
    GtkTreeIter iter;
    GtkTreeModel *model;
    if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
        int tid = 0;
        gtk_tree_model_get(model, &iter, 0, &tid, -1); // cột 0 = id
        return tid;
    }
    return 0;
}

// Double‑click vào một dòng task -> lấy detail task từ server + sync spin progress
static void on_task_row_activated(GtkTreeView *view,
                                  GtkTreePath *path,
                                  GtkTreeViewColumn *col,
                                  gpointer user_data) {
    App *a = (App*)user_data;
    int tid = get_selected_task_id(a);
    if (tid <= 0) return;

    // gửi CMD_LIST_TASK_DETAIL|tid
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "%s|%d\n", CMD_LIST_TASK_DETAIL, tid);
    char payload[2048] = {0};
    int code = net_request(a->sockfd, cmd, payload, sizeof(payload));
    if (code != 0) {
        show_msg(GTK_WINDOW(a->main_win), GTK_MESSAGE_ERROR, "Error", payload);
        return;
    }
    if (!payload[0] || strcmp(payload, "No detail") == 0) {
        show_msg(GTK_WINDOW(a->main_win), GTK_MESSAGE_INFO, "Task Detail", "No detail");
        return;
    }

    // payload: id|project_id|title|description|Assignee:..|Status:..|Progress:..|Start:..|End:...
    // Tìm trường Progress:.. trong payload để set giá trị cho spin button
    {
        char *tmp = g_strdup(payload);
        char *save = NULL;
        for (char *tok = strtok_r(tmp, "|", &save);
             tok;
             tok = strtok_r(NULL, "|", &save)) {
            if (g_str_has_prefix(tok, "Progress:")) {
                int p = atoi(tok + 9);
                if (p < 0) p = 0;
                else if (p > 100) p = 100;
                if (a->progress_spin)
                    gtk_spin_button_set_value(a->progress_spin, p);
                break;
            }
        }
        g_free(tmp);
    }

    // show detail dạng text luôn
    show_msg(GTK_WINDOW(a->main_win), GTK_MESSAGE_INFO, "Task Detail", payload);
}

// Đổ payload danh sách project vào 1 GtkComboBoxText (id|name mỗi dòng)
static void fill_projects_combo(GtkComboBoxText *combo, const char *projects_payload) {
    gtk_combo_box_text_remove_all(combo);
    if (!projects_payload || !*projects_payload) return;

    char *dup = g_strdup(projects_payload);
    char *save = NULL;
    for (char *line = strtok_r(dup, "\n", &save);
         line;
         line = strtok_r(NULL, "\n", &save)) {
        if (!*line) continue;
        char *sep = strchr(line, '|');
        if (!sep) continue;
        *sep = '\0';
        const char *id = line;
        const char *name = sep + 1;
        char label[256];
        // hiển thị "id - name" cho dễ nhìn
        snprintf(label, sizeof(label), "%s - %s", id, name);
        gtk_combo_box_text_append(combo, id, label);
    }
    g_free(dup);

    // auto chọn item đầu tiên nếu combo không rỗng
    GtkTreeModel *m = gtk_combo_box_get_model(GTK_COMBO_BOX(combo));
    if (gtk_tree_model_iter_n_children(m, NULL) > 0) {
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    }
}

// Gửi LIST_PROJECT, đổ vào TreeView + combobox tasks/chat
static void refresh_projects(App *a) {
    char payload[4096] = {0};
    int code = net_request(a->sockfd, CMD_LIST_PROJECT "\n", payload, sizeof(payload));
    projects_store_clear(a);

    if (code != 0) {
        show_msg(GTK_WINDOW(a->main_win), GTK_MESSAGE_ERROR, "Error", payload);
        return;
    }

    if (!payload[0] || strcmp(payload, "No projects") == 0) {
        // không có project: clear combo tasks/chat, không báo lỗi
        gtk_label_set_text(a->login_status, "");
        gtk_combo_box_text_remove_all(a->tasks_project_combo);
        gtk_combo_box_text_remove_all(a->chat_project_combo);
        return;
    }

    // parse payload -> projects_store
    char *dup = g_strdup(payload);
    char *save = NULL;
    for (char *line = strtok_r(dup, "\n", &save);
         line;
         line = strtok_r(NULL, "\n", &save)) {
        if (!*line) continue;
        char *sep = strchr(line, '|');
        if (!sep) continue;
        *sep = '\0';
        int id = atoi(line);
        const char *name = sep + 1;
        GtkTreeIter iter;
        gtk_list_store_append(a->projects_store, &iter);
        gtk_list_store_set(a->projects_store, &iter,
                           0, id,
                           1, name,
                           -1);
    }
    g_free(dup);

    // cập nhật combobox dùng cùng payload
    fill_projects_combo(a->tasks_project_combo, payload);
    fill_projects_combo(a->chat_project_combo, payload);
}

// Gửi LIST_TASK cho project combo đang chọn, đổ vào tasks_store
static void refresh_tasks(App *a) {
    const char *pid = gtk_combo_box_text_get_active_text(a->tasks_project_combo);
    if (!pid) {
        tasks_store_clear(a);
        return;
    }

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "%s|%s\n", CMD_LIST_TASK, pid);

    char payload[4096] = {0};
    int code = net_request(a->sockfd, cmd, payload, sizeof(payload));

    tasks_store_clear(a);

    if (code != 0) {
        show_msg(GTK_WINDOW(a->main_win), GTK_MESSAGE_ERROR, "Error", payload);
        return;
    }
    if (!payload[0] || strcmp(payload, "No tasks") == 0) return;

    // mỗi dòng: id|title|Assignee:..|Status:..|Progress:n|Start:..|End:..
    char *dup = g_strdup(payload);
    char *save = NULL;

    for (char *line = strtok_r(dup, "\n", &save);
         line;
         line = strtok_r(NULL, "\n", &save)) {
        if (!*line) continue;

        char *parts[8] = {0};
        int pc = 0;
        char *save2 = NULL;
        for (char *t = strtok_r(line, "|", &save2);
             t && pc < 8;
             t = strtok_r(NULL, "|", &save2)) {
            t = g_strstrip(t);  // trim khoảng trắng hai đầu
            parts[pc++] = t;
        }
        if (pc < 2) continue;

        int id = atoi(parts[0]);
        const char *title = parts[1];

        const char *assignee = "";
        const char *progress = "0";
        const char *start = "";
        const char *end = "";

        // scan các phần còn lại để tìm Assignee/Progress/Start/End
        for (int i = 2; i < pc; i++) {
            char *val = g_strstrip(parts[i]);
            if (g_str_has_prefix(val, "Assignee:")) assignee = val + 9;
            else if (g_str_has_prefix(val, "Progress:")) progress = val + 9;
            else if (g_str_has_prefix(val, "Start:")) start = val + 6;
            else if (g_str_has_prefix(val, "End:")) end = val + 4;
        }

        // hiển thị Status = progress%
        char status_display[32];
        snprintf(status_display, sizeof(status_display), "%s%%",
                 progress && *progress ? progress : "0");

        GtkTreeIter iter;
        gtk_list_store_append(a->tasks_store, &iter);
        gtk_list_store_set(a->tasks_store, &iter,
            0, id,
            1, title ? title : "",
            2, assignee ? assignee : "",
            3, status_display,          // Status = progress%
            4, start ? start : "",
            5, end ? end : "",
            -1);
    }

    g_free(dup);
}

// Vẽ Gantt “đơn giản” dựa trên a->gantt_cache (dữ liệu text đã được refresh_gantt)
static gboolean gantt_draw_cb(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    App *a = (App*)user_data;
    GtkAllocation al;
    gtk_widget_get_allocation(widget, &al);

    // background trắng
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);

    // nếu không có dữ liệu Gantt
    if (!a->gantt_cache[0] || strcmp(a->gantt_cache, "No tasks") == 0) {
        cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
        cairo_move_to(cr, 10, 20);
        cairo_show_text(cr, "No tasks to show");
        return FALSE;
    }

    // Mỗi task = 1 dòng; mỗi ngày = 20px
    const int left = 140;   // lề trái (để ghi tên task)
    const int top = 20;
    const int row_h = 26;   // cao mỗi dòng
    const int day_w = 20;   // rộng mỗi ngày

    // struct nội bộ cho 1 task hiển thị trên Gantt
    typedef struct {
        char title[128];
        char assignee[64];
        int start;      // ngày bắt đầu (1..31)
        int end;        // ngày kết thúc
        char status[32];
        int progress;   // %
    } T;

    T tasks[128];
    int n = 0;

    // parse a->gantt_cache: id|title|Status:..|Progress:..|Start:..|End:..|Assignee:..
    char *dup = g_strdup(a->gantt_cache);
    char *save = NULL;
    for (char *line = strtok_r(dup, "\n", &save);
         line && n < 128;
         line = strtok_r(NULL, "\n", &save)) {
        if (!*line) continue;
        char *parts[8] = {0};
        int pc = 0;
        char *save2 = NULL;
        for (char *t = strtok_r(line, "|", &save2);
             t && pc < 8;
             t = strtok_r(NULL, "|", &save2))
            parts[pc++] = t;
        if (pc < 2) continue;

        // mặc định
        strncpy(tasks[n].title, parts[1], sizeof(tasks[n].title)-1);
        strncpy(tasks[n].assignee, "", sizeof(tasks[n].assignee)-1);
        strncpy(tasks[n].status, "NOT_STARTED", sizeof(tasks[n].status)-1);
        tasks[n].progress = 0;
        int st = -1, en = -1;

        // scan các field status/progress/assignee/start/end
        for (int i=2;i<pc;i++) {
            if (g_str_has_prefix(parts[i], "Status:")) {
                strncpy(tasks[n].status, parts[i]+7, sizeof(tasks[n].status)-1);
            } else if (g_str_has_prefix(parts[i], "Progress:")) {
                int p = atoi(parts[i]+9);
                if (p < 0) p = 0;
                if (p > 100) p = 100;
                tasks[n].progress = p;
            } else if (g_str_has_prefix(parts[i], "Assignee:")) {
                strncpy(tasks[n].assignee, parts[i]+9, sizeof(tasks[n].assignee)-1);
            } else if (g_str_has_prefix(parts[i], "Start:")) {
                const char *d = parts[i]+6;
                // đơn giản: lấy day-of-month (2 ký tự cuối) làm day index
                if (d && strlen(d) >= 10)
                    st = atoi(d+8);
            } else if (g_str_has_prefix(parts[i], "End:")) {
                const char *d = parts[i]+4;
                if (d && strlen(d) >= 10)
                    en = atoi(d+8);
            }
        }
        if (st < 0) st = n + 1;     // nếu thiếu -> gán tạm theo thứ tự
        if (en < 0) en = st + 1;
        if (en < st) en = st;

        tasks[n].start = st;
        tasks[n].end = en;
        n++;
    }
    g_free(dup);

    // vẽ grid ngày 1..31
    cairo_set_source_rgb(cr, 0.85, 0.85, 0.85);
    for (int d=1; d<=31; d++) {
        int x = left + (d-1)*day_w;
        cairo_move_to(cr, x, top-12);
        cairo_line_to(cr, x, top + n*row_h);
    }
    cairo_stroke(cr);

    // nhãn ngày (1,3,5,...) phía trên
    cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
    for (int d=1; d<=31; d+=2) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", d);
        cairo_move_to(cr, left + (d-1)*day_w + 2, top-2);
        cairo_show_text(cr, buf);
    }

    // vẽ từng task
    for (int i=0; i<n; i++) {
        int y = top + i*row_h;

        // label: "title (assignee) - progress%"
        cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
        cairo_move_to(cr, 10, y+16);
        char label[256];
        if (tasks[i].assignee[0])
            snprintf(label, sizeof(label), "%s (%s) - %d%%",
                     tasks[i].title, tasks[i].assignee, tasks[i].progress);
        else
            snprintf(label, sizeof(label), "%s - %d%%",
                     tasks[i].title, tasks[i].progress);
        cairo_show_text(cr, label);

        int x1 = left + (tasks[i].start-1)*day_w;
        int x2 = left + (tasks[i].end-1)*day_w;
        int w = (x2 - x1) + day_w;

        // màu bar theo status: DONE / IN_PROGRESS / còn lại
        if (g_strcmp0(tasks[i].status, "DONE") == 0)
            cairo_set_source_rgb(cr, 0.2, 0.6, 0.2);   // xanh
        else if (g_strcmp0(tasks[i].status, "IN_PROGRESS") == 0)
            cairo_set_source_rgb(cr, 0.85, 0.5, 0.1);  // cam
        else
            cairo_set_source_rgb(cr, 0.6, 0.6, 0.6);   // xám

        cairo_rectangle(cr, x1, y+6, w, 14);
        cairo_fill(cr);
    }

    return FALSE;
}

// Gửi LIST_TASK_GANTT cho project, cache kết quả, rồi yêu cầu vẽ lại Gantt
static void refresh_gantt(App *a, int project_id) {
    a->gantt_project_id = project_id;
    a->gantt_cache[0] = '\0';

    if (project_id <= 0) {
        // không có project -> clear vùng vẽ
        gtk_widget_queue_draw(GTK_WIDGET(a->gantt_area));
        return;
    }

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "%s|%d\n", CMD_LIST_TASK_GANTT, project_id);
    char payload[4096] = {0};
    int code = net_request(a->sockfd, cmd, payload, sizeof(payload));
    if (code != 0) {
        // lỗi -> để cache rỗng
        strncpy(a->gantt_cache, "", sizeof(a->gantt_cache)-1);
    } else {
        // lưu payload raw vào cache
        strncpy(a->gantt_cache, payload, sizeof(a->gantt_cache)-1);
    }
    gtk_widget_queue_draw(GTK_WIDGET(a->gantt_area));
}

// Xử lý chung login / register tuỳ theo is_register
static void on_login_or_register(App *a, gboolean is_register) {
    const char *u = gtk_entry_get_text(a->entry_user);
    const char *p = gtk_entry_get_text(a->entry_pass);
    if (!u || !*u || !p || !*p) {
        gtk_label_set_text(a->login_status, "Nhập username/password");
        return;
    }

    // build command LOGIN hoặc REGISTER
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "%s|%s|%s\n",
             is_register ? CMD_REGISTER : CMD_LOGIN,
             u, p);

    char payload[4096] = {0};
    int code = net_request(a->sockfd, cmd, payload, sizeof(payload));

    if (code != 0) {
        gtk_label_set_text(a->login_status, payload[0] ? payload : "Fail");
        return;
    }

    if (is_register) {
        // register ok -> chỉ báo rồi chờ user login
        gtk_label_set_text(a->login_status,
                           "Register OK. Bạn có thể login.");
        return;
    }

    // login ok
    strncpy(a->username, u, sizeof(a->username)-1);

    // show main, hide login
    gtk_widget_hide(a->login_win);
    gtk_widget_show_all(a->main_win);

    // load project/tasks ban đầu
    refresh_projects(a);
    refresh_tasks(a);

    // set Gantt default theo project đang active trong tasks_project_combo
    int pid = 0;
    const char *pid_str = gtk_combo_box_text_get_active_text(a->tasks_project_combo);
    if (pid_str) pid = atoi(pid_str);
    refresh_gantt(a, pid);

    a->last_chat_id = 0;
}

// Button Register trên màn login
static void on_btn_register(GtkButton *btn, gpointer user_data) {
    (void)btn;
    on_login_or_register((App*)user_data, TRUE);
}

// Button Login trên màn login
static void on_btn_login(GtkButton *btn, gpointer user_data) {
    (void)btn;
    on_login_or_register((App*)user_data, FALSE);
}

// Tạo project mới từ ô nhập create_project_entry
static void on_btn_create_project(GtkButton *btn, gpointer user_data) {
    (void)btn;
    App *a = (App*)user_data;
    const char *name = gtk_entry_get_text(a->create_project_entry);
    if (!name || !*name) return;

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s|%s\n", CMD_CREATE_PROJECT, name);
    char payload[512]={0};
    int code = net_request(a->sockfd, cmd, payload, sizeof(payload));
    if (code != 0)
        show_msg(GTK_WINDOW(a->main_win), GTK_MESSAGE_ERROR, "Error", payload);

    gtk_entry_set_text(a->create_project_entry, "");
    refresh_projects(a);
}

// Mời user vào project đang chọn trong TreeView
static void on_btn_invite(GtkButton *btn, gpointer user_data) {
    (void)btn;
    App *a = (App*)user_data;
    int pid = get_selected_project_id(a);
    if (pid <= 0) {
        show_msg(GTK_WINDOW(a->main_win), GTK_MESSAGE_WARNING, "Info",
                 "Chọn 1 project trước");
        return;
    }
    const char *user = gtk_entry_get_text(a->invite_user_entry);
    if (!user || !*user) return;

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s|%d|%s\n", CMD_INVITE_MEMBER, pid, user);
    char payload[512]={0};
    int code = net_request(a->sockfd, cmd, payload, sizeof(payload));
    if (code != 0)
        show_msg(GTK_WINDOW(a->main_win), GTK_MESSAGE_ERROR, "Error", payload);

    gtk_entry_set_text(a->invite_user_entry, "");
    refresh_projects(a);
}

// Button Refresh Projects
static void on_btn_refresh_projects(GtkButton *btn, gpointer user_data) {
    (void)btn;
    refresh_projects((App*)user_data);
}

// Khi chọn project khác trong combo tasks_project_combo
static void on_tasks_project_changed(GtkComboBox *combo, gpointer user_data) {
    (void)combo;
    App *a = (App*)user_data;
    // reload tasks
    refresh_tasks(a);
    // đồng thời refresh Gantt cho project mới
    const char *pid = gtk_combo_box_text_get_active_text(a->tasks_project_combo);
    refresh_gantt(a, pid ? atoi(pid) : 0);
    // có thể reset chat nếu muốn theo project
    // a->last_chat_id = 0;
}

// Tạo task mới (dùng nhiều entry: title/desc/assignee/start/end)
static void on_btn_create_task(GtkButton *btn, gpointer user_data) {
    (void)btn;
    App *a = (App*)user_data;
    const char *pid = gtk_combo_box_text_get_active_text(a->tasks_project_combo);
    if (!pid) return;

    const char *title = gtk_entry_get_text(a->task_title_entry);
    const char *desc = gtk_entry_get_text(a->task_desc_entry);
    const char *assignee = gtk_entry_get_text(a->assign_user_entry);
    const char *start = gtk_entry_get_text(a->start_date_entry);
    const char *end = gtk_entry_get_text(a->end_date_entry);

    if (!title || !*title) {
        show_msg(GTK_WINDOW(a->main_win), GTK_MESSAGE_WARNING, "Info", "Nhập Task title");
        return;
    }
    if (!assignee || !*assignee) {
        show_msg(GTK_WINDOW(a->main_win), GTK_MESSAGE_WARNING, "Info",
                 "Nhập username người được phân công (Assignee)");
        return;
    }
    if (!start || !*start || !end || !*end) {
        show_msg(GTK_WINDOW(a->main_win), GTK_MESSAGE_WARNING, "Info",
                 "Bắt buộc nhập Start/End (YYYY-MM-DD) để vẽ Gantt");
        return;
    }
    if (!desc) desc = "";

    // CMD_CREATE_TASK|pid|title|desc|assignee|start|end
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "%s|%s|%s|%s|%s|%s|%s\n",
             CMD_CREATE_TASK, pid, title, desc, assignee, start, end);
    char payload[512]={0};
    int code = net_request(a->sockfd, cmd, payload, sizeof(payload));
    if (code != 0)
        show_msg(GTK_WINDOW(a->main_win), GTK_MESSAGE_ERROR, "Error", payload);

    // clear form
    gtk_entry_set_text(a->task_title_entry, "");
    gtk_entry_set_text(a->task_desc_entry, "");
    gtk_entry_set_text(a->assign_user_entry, "");
    gtk_entry_set_text(a->start_date_entry, "");
    gtk_entry_set_text(a->end_date_entry, "");

    // reload tasks + gantt
    refresh_tasks(a);
    refresh_gantt(a, atoi(pid));
}

// Assign task cho user (từ dòng chọn hoặc nhập Task ID)
static void on_btn_assign_task(GtkButton *btn, gpointer user_data) {
    (void)btn;
    App *a = (App*)user_data;
    int tid = get_selected_task_id(a);
    if (tid <= 0) {
        // cho phép nhập tay task id
        const char *t = gtk_entry_get_text(a->task_id_entry);
        if (t && *t) tid = atoi(t);
    }
    if (tid <= 0) {
        show_msg(GTK_WINDOW(a->main_win), GTK_MESSAGE_WARNING, "Info",
                 "Chọn task hoặc nhập Task ID");
        return;
    }
    const char *user = gtk_entry_get_text(a->assign_user_entry);
    if (!user || !*user) return;

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s|%d|%s\n", CMD_ASSIGN_TASK, tid, user);
    char payload[512]={0};
    int code = net_request(a->sockfd, cmd, payload, sizeof(payload));
    if (code != 0)
        show_msg(GTK_WINDOW(a->main_win), GTK_MESSAGE_ERROR, "Error", payload);

    refresh_tasks(a);
}

// Update % progress cho task (từ dòng chọn hoặc nhập Task ID + spin)
static void on_btn_update_progress(GtkButton *btn, gpointer user_data) {
    (void)btn;
    App *a = (App*)user_data;
    int tid = get_selected_task_id(a);
    if (tid <= 0) {
        const char *t = gtk_entry_get_text(a->task_id_entry);
        if (t && *t) tid = atoi(t);
    }
    if (tid <= 0) {
        show_msg(GTK_WINDOW(a->main_win), GTK_MESSAGE_WARNING, "Info",
                 "Chọn task hoặc nhập Task ID");
        return;
    }

    int progress = (int)gtk_spin_button_get_value(a->progress_spin);
    if (progress < 0) progress = 0;
    if (progress > 100) progress = 100;

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "%s|%d|%d\n", CMD_UPDATE_TASK_PROGRESS, tid, progress);
    char payload[512] = {0};
    int code = net_request(a->sockfd, cmd, payload, sizeof(payload));
    if (code != 0)
        show_msg(GTK_WINDOW(a->main_win), GTK_MESSAGE_ERROR, "Error", payload);

    // reload tasks + Gantt
    refresh_tasks(a);
    const char *pid = gtk_combo_box_text_get_active_text(a->tasks_project_combo);
    if (pid) refresh_gantt(a, atoi(pid));
}

// Set Start/End cho task (dùng entry task_id_entry + start_date_entry/end_date_entry)
static void on_btn_set_dates(GtkButton *btn, gpointer user_data) {
    (void)btn;
    App *a = (App*)user_data;
    int tid = get_selected_task_id(a);
    if (tid <= 0) {
        const char *t = gtk_entry_get_text(a->task_id_entry);
        if (t && *t) tid = atoi(t);
    }
    const char *start = gtk_entry_get_text(a->start_date_entry);
    const char *end = gtk_entry_get_text(a->end_date_entry);
    if (tid <= 0 || !start || !*start || !end || !*end) {
        show_msg(GTK_WINDOW(a->main_win), GTK_MESSAGE_WARNING, "Info",
                 "Nhập Task ID + Start/End (YYYY-MM-DD)");
        return;
    }

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "%s|%d|%s|%s\n",
             CMD_SET_TASK_DATES, tid, start, end);
    char payload[512]={0};
    int code = net_request(a->sockfd, cmd, payload, sizeof(payload));
    if (code != 0)
        show_msg(GTK_WINDOW(a->main_win), GTK_MESSAGE_ERROR, "Error", payload);

    refresh_tasks(a);
    const char *pid = gtk_combo_box_text_get_active_text(a->tasks_project_combo);
    if (pid) refresh_gantt(a, atoi(pid));
}

// Tiện ích set text cho 1 GtkTextView
static void set_textview(GtkTextView *tv, const char *text) {
    GtkTextBuffer *b = gtk_text_view_get_buffer(tv);
    gtk_text_buffer_set_text(b, text ? text : "", -1);
}

// Escape 1 field để gửi qua protocol (tránh trùng ký tự phân cách |, \n,...)
static char* escape_field(const char *s) {
    if (!s) return g_strdup("");
    GString *g = g_string_new("");
    for (const char *p = s; *p; p++) {
        if (*p == '\\')          // escape lại dấu backslash
            g_string_append(g, "\\\\");
        else if (*p == '|')      // escape dấu phân cách field '|'
            g_string_append(g, "\\|");
        else if (*p == '\n')     // escape newline thành \n
            g_string_append(g, "\\n");
        else if (*p == '\r')
            ;                    // bỏ qua \r
        else
            g_string_append_c(g, *p);
    }
    // trả về char* (caller phải g_free())
    return g_string_free(g, FALSE);
}

// Giải escape ngược lại ngay trên buffer (in-place)
static void unescape_inplace(char *s) {
    if (!s) return;
    char *w = s;   // con trỏ ghi
    for (char *p = s; *p; p++) {
        if (*p == '\\') {
            // gặp backslash -> đọc ký tự tiếp theo để xác định escape
            p++;
            if (!*p) break;
            if (*p == 'n')        // \n -> newline thực
                *w++ = '\n';
            else if (*p == '|')   // \| -> '|'
                *w++ = '|';
            else if (*p == '\\')  // \\ -> '\'
                *w++ = '\\';
            else {                // các escape không biết -> giữ nguyên "\x"
                *w++ = '\\';
                *w++ = *p;
            }
        } else {
            // ký tự thường -> copy
            *w++ = *p;
        }
    }
    *w = '\0';
}

// Thêm comment cho task (dùng ô Task ID + TextView comment_text)
static void on_btn_add_comment(GtkButton *btn, gpointer user_data) {
    (void)btn;
    App *a = (App*)user_data;

    const char *tid_s = gtk_entry_get_text(a->comment_task_id_entry);
    if (!tid_s || !*tid_s) return;
    int tid = atoi(tid_s);

    GtkTextBuffer *buf = gtk_text_view_get_buffer(a->comment_text);
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buf, &start, &end);
    char *content = gtk_text_buffer_get_text(buf, &start, &end, FALSE);
    if (!content || !*content) { g_free(content); return; }

    // escape nội dung comment trước khi gửi
    char *safe = escape_field(content);

    // CMD_ADD_COMMENT|task_id|escaped_content
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "%s|%d|%s\n", CMD_ADD_COMMENT, tid, safe);

    g_free(safe);

    char payload[5120] = {0};
    int code = net_request(a->sockfd, cmd, payload, sizeof(payload));
    if (code != 0)
        show_msg(GTK_WINDOW(a->main_win), GTK_MESSAGE_ERROR, "Error", payload);

    // clear nội dung ô nhập
    gtk_text_buffer_set_text(buf, "", -1);
    g_free(content);

    // reload lại danh sách comments
    on_btn_list_comments(NULL, user_data);
}

// Load list comments cho 1 task, hiển thị trong comments_list_text
static void on_btn_list_comments(GtkButton *btn, gpointer user_data) {
    (void)btn;
    App *a = (App*)user_data;

    const char *tid_s = gtk_entry_get_text(a->comment_task_id_entry);
    if (!tid_s || !*tid_s) return;
    int tid = atoi(tid_s);

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "%s|%d\n", CMD_LIST_COMMENTS, tid);

    char payload[40960] = {0};
    int code = net_request(a->sockfd, cmd, payload, sizeof(payload));
    if (code != 0) {
        show_msg(GTK_WINDOW(a->main_win), GTK_MESSAGE_ERROR, "Error", payload);
        return;
    }

    if (!payload[0] || strcmp(payload, "No comments") == 0) {
        set_textview(a->comments_list_text, "No comments\n");
        return;
    }

    // payload: id|user|content_escaped|timestamp mỗi dòng
    GString *out = g_string_new("");
    char *dup = g_strdup(payload);
    char *save = NULL;

    for (char *line = strtok_r(dup, "\n", &save);
         line;
         line = strtok_r(NULL, "\n", &save)) {
        if (!*line) continue;

        char *save2 = NULL;
        char *id     = strtok_r(line, "|", &save2);
        char *user   = strtok_r(NULL, "|", &save2);
        char *content= strtok_r(NULL, "|", &save2);
        char *ts     = strtok_r(NULL, "|", &save2);
        if (!id || !user || !content || !ts) continue;

        // giải escape nội dung trước khi in
        unescape_inplace(content);
        g_string_append_printf(out, "[%s] %s: %s\n", ts, user, content);
    }

    set_textview(a->comments_list_text, out->str);
    g_string_free(out, TRUE);
    g_free(dup);
}

// Thêm attachment cho task (dùng các entry attach_* ở tab Detail)
static void on_btn_add_attachment(GtkButton *btn, gpointer user_data) {
    (void)btn;
    App *a = (App*)user_data;

    const char *tid_s   = gtk_entry_get_text(a->attach_task_id_entry);
    const char *filename= gtk_entry_get_text(a->attach_filename_entry);
    const char *filepath= gtk_entry_get_text(a->attach_filepath_entry);
    if (!tid_s || !*tid_s || !filename || !*filename || !filepath || !*filepath)
        return;

    int tid = atoi(tid_s);

    // escape filename + filepath trước khi gửi
    char *safe_fn = escape_field(filename);
    char *safe_fp = escape_field(filepath);

    // CMD_ADD_ATTACHMENT|tid|file_name|file_path
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "%s|%d|%s|%s\n",
             CMD_ADD_ATTACHMENT, tid, safe_fn, safe_fp);

    g_free(safe_fn);
    g_free(safe_fp);

    char payload[5120] = {0};
    int code = net_request(a->sockfd, cmd, payload, sizeof(payload));
    if (code != 0)
        show_msg(GTK_WINDOW(a->main_win), GTK_MESSAGE_ERROR, "Error", payload);

    // reload danh sách attachment
    on_btn_list_attachments(NULL, user_data);
}

// Load list attachments cho task, hiển thị dạng text (ts, filename, path)
static void on_btn_list_attachments(GtkButton *btn, gpointer user_data) {
    (void)btn;
    App *a = (App*)user_data;

    const char *tid_s = gtk_entry_get_text(a->attach_task_id_entry);
    if (!tid_s || !*tid_s) return;
    int tid = atoi(tid_s);

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "%s|%d\n", CMD_LIST_ATTACHMENTS, tid);

    char payload[40960] = {0};
    int code = net_request(a->sockfd, cmd, payload, sizeof(payload));
    if (code != 0) {
        show_msg(GTK_WINDOW(a->main_win), GTK_MESSAGE_ERROR, "Error", payload);
        return;
    }

    if (!payload[0] || strcmp(payload, "No attachments") == 0) {
        set_textview(a->attachments_list_text, "No attachments\n");
        return;
    }

    // payload: id|filename_esc|filepath_esc|timestamp mỗi dòng
    GString *out = g_string_new("");
    char *dup = g_strdup(payload);
    char *save = NULL;

    for (char *line = strtok_r(dup, "\n", &save);
         line;
         line = strtok_r(NULL, "\n", &save)) {
        if (!*line) continue;

        char *save2    = NULL;
        char *id       = strtok_r(line, "|", &save2);
        char *filename = strtok_r(NULL, "|", &save2);
        char *filepath = strtok_r(NULL, "|", &save2);
        char *ts       = strtok_r(NULL, "|", &save2);
        if (!id || !filename || !filepath || !ts) continue;

        // giải escape filename, filepath
        unescape_inplace(filename);
        unescape_inplace(filepath);
        g_string_append_printf(out, "[%s] %s (%s)\n", ts, filename, filepath);
    }

    set_textview(a->attachments_list_text, out->str);
    g_string_free(out, TRUE);
    g_free(dup);
}

// ----- Chat polling -----
// poll_chat được gọi định kỳ bằng g_timeout_add, load chat mới cho project đang chọn
static gboolean poll_chat(gpointer user_data) {
    App *a = (App*)user_data;

    // lấy project id từ combo chat (dùng active_id)
    const char *pid = gtk_combo_box_get_active_id(GTK_COMBO_BOX(a->chat_project_combo));
    if (!pid) return TRUE;  // không có project -> vẫn trả TRUE để timer tiếp tục chạy

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "%s|%s|%d\n",
             CMD_LIST_CHAT, pid, a->last_chat_id);
    char payload[4096]={0};
    int code = net_request(a->sockfd, cmd, payload, sizeof(payload));
    if (code != 0) return TRUE;
    if (!payload[0]) return TRUE;

    // append các dòng mới vào chat_view, update last_chat_id
    GtkTextBuffer *b = gtk_text_view_get_buffer(a->chat_view);
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(b, &end);

    char *dup = g_strdup(payload);
    char *save = NULL;
    for (char *line = strtok_r(dup, "\n", &save);
         line;
         line = strtok_r(NULL, "\n", &save)) {
        if (!*line) continue;
        // line: id|username|content|created_at
        char *p1 = strtok(line, "|");
        char *p2 = strtok(NULL, "|");
        char *p3 = strtok(NULL, "|");
        char *p4 = strtok(NULL, "|");
        if (!p1 || !p2 || !p3 || !p4) continue;
        int id = atoi(p1);
        if (id > a->last_chat_id) a->last_chat_id = id;
        char row[1024];
        snprintf(row, sizeof(row), "[%s] %s: %s\n", p4, p2, p3);
        gtk_text_buffer_insert(b, &end, row, -1);
    }
    g_free(dup);

    return TRUE; // tiếp tục timer
}

// Xoá toàn bộ nội dung chat_view
static void chat_clear(App *a) {
    GtkTextBuffer *b = gtk_text_view_get_buffer(a->chat_view);
    gtk_text_buffer_set_text(b, "", -1);
}

// Khi đổi project trong tab Chat
static void on_chat_project_changed(GtkComboBox *combo, gpointer user_data) {
    (void)combo;
    App *a = (App*)user_data;

    a->last_chat_id = 0; // reset id
    chat_clear(a);
    poll_chat(a);        // load luôn chat cho project mới
}

// Gửi tin nhắn chat cho project đang chọn
static void on_btn_send_chat(GtkButton *btn, gpointer user_data) {
    (void)btn;
    App *a = (App*)user_data;

    const char *pid = gtk_combo_box_get_active_id(GTK_COMBO_BOX(a->chat_project_combo));
    const char *msg = gtk_entry_get_text(a->chat_entry);
    if (!pid || !msg || !*msg) return;

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "%s|%s|%s\n",
             CMD_SEND_CHAT, pid, msg);
    char payload[512]={0};
    int code = net_request(a->sockfd, cmd, payload, sizeof(payload));
    if (code != 0)
        show_msg(GTK_WINDOW(a->main_win), GTK_MESSAGE_ERROR, "Error", payload);

    gtk_entry_set_text(a->chat_entry, "");
    // sau khi gửi, poll luôn 1 lần để thấy tin nhắn mới
    poll_chat(a);
}

// Tạo 1 GtkTreeView chung cho store với danh sách tên cột
static GtkWidget* make_tree_view(GtkListStore *store,
                                 const char **cols, int ncols) {
    GtkWidget *view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    for (int i=0;i<ncols;i++) {
        GtkCellRenderer *r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *c =
            gtk_tree_view_column_new_with_attributes(cols[i], r,
                                                     "text", i, NULL);
        gtk_tree_view_append_column(GTK_TREE_VIEW(view), c);
    }
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), TRUE);
    return view;
}

// Xây UI màn login
static GtkWidget* build_login(App *a) {
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "QLCV - Login");
    gtk_window_set_default_size(GTK_WINDOW(win), 360, 180);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_add(GTK_CONTAINER(win), box);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_box_pack_start(GTK_BOX(box), grid, TRUE, TRUE, 0);

    GtkWidget *l1 = gtk_label_new("Username:");
    GtkWidget *l2 = gtk_label_new("Password:");
    a->entry_user = GTK_ENTRY(gtk_entry_new());
    a->entry_pass = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_visibility(a->entry_pass, FALSE); // password hidden

    gtk_grid_attach(GTK_GRID(grid), l1, 0,0,1,1);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(a->entry_user), 1,0,1,1);
    gtk_grid_attach(GTK_GRID(grid), l2, 0,1,1,1);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(a->entry_pass), 1,1,1,1);

    GtkWidget *btn_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *btn_reg   = gtk_button_new_with_label("Register");
    GtkWidget *btn_login = gtk_button_new_with_label("Login");
    gtk_box_pack_start(GTK_BOX(btn_row), btn_reg, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btn_row), btn_login, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), btn_row, FALSE, FALSE, 0);

    a->login_status = GTK_LABEL(gtk_label_new(""));
    gtk_box_pack_start(GTK_BOX(box),
                       GTK_WIDGET(a->login_status),
                       FALSE, FALSE, 0);

    g_signal_connect(btn_reg,   "clicked", G_CALLBACK(on_btn_register), a);
    g_signal_connect(btn_login, "clicked", G_CALLBACK(on_btn_login),    a);

    return win;
}

// Xây toàn bộ UI chính (notebook: Projects / Tasks / Comments & Files / Gantt / Chat)
static GtkWidget* build_main(App *a) {
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "QLCV - GTK Client");
    gtk_window_set_default_size(GTK_WINDOW(win), 980, 640);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_add(GTK_CONTAINER(win), root);
    gtk_container_set_border_width(GTK_CONTAINER(root), 8);

    GtkWidget *tabs = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(root), tabs, TRUE, TRUE, 0);

    // --- Projects tab ---
    a->projects_store = gtk_list_store_new(2, G_TYPE_INT, G_TYPE_STRING);
    const char *pcols[] = {"ID", "Name"};
    a->projects_view = GTK_TREE_VIEW(make_tree_view(a->projects_store, pcols, 2));

    GtkWidget *proj_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *proj_sc = gtk_scrolled_window_new(NULL,NULL);
    gtk_container_add(GTK_CONTAINER(proj_sc), GTK_WIDGET(a->projects_view));
    gtk_box_pack_start(GTK_BOX(proj_box), proj_sc, TRUE, TRUE, 0);

    GtkWidget *proj_actions = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(proj_actions), 8);
    gtk_grid_set_column_spacing(GTK_GRID(proj_actions), 8);

    a->create_project_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(a->create_project_entry, "New project name");
    GtkWidget *btn_create_project = gtk_button_new_with_label("Create");

    a->invite_user_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(a->invite_user_entry,
                                   "Username to invite (select project above)");
    GtkWidget *btn_invite      = gtk_button_new_with_label("Invite");
    GtkWidget *btn_refresh_p   = gtk_button_new_with_label("Refresh");

    gtk_grid_attach(GTK_GRID(proj_actions), GTK_WIDGET(a->create_project_entry), 0,0,2,1);
    gtk_grid_attach(GTK_GRID(proj_actions), btn_create_project, 2,0,1,1);
    gtk_grid_attach(GTK_GRID(proj_actions), GTK_WIDGET(a->invite_user_entry), 0,1,2,1);
    gtk_grid_attach(GTK_GRID(proj_actions), btn_invite, 2,1,1,1);
    gtk_grid_attach(GTK_GRID(proj_actions), btn_refresh_p, 2,2,1,1);

    gtk_box_pack_start(GTK_BOX(proj_box), proj_actions, FALSE, FALSE, 0);

    g_signal_connect(btn_create_project, "clicked", G_CALLBACK(on_btn_create_project), a);
    g_signal_connect(btn_invite,        "clicked", G_CALLBACK(on_btn_invite),         a);
    g_signal_connect(btn_refresh_p,     "clicked", G_CALLBACK(on_btn_refresh_projects), a);

    gtk_notebook_append_page(GTK_NOTEBOOK(tabs),
                             proj_box, gtk_label_new("Projects"));

    // --- Tasks tab ---
    GtkWidget *task_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *task_top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(task_box), task_top, FALSE, FALSE, 0);

    a->tasks_project_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_box_pack_start(GTK_BOX(task_top), gtk_label_new("Project:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(task_top), GTK_WIDGET(a->tasks_project_combo), FALSE, FALSE, 0);

    GtkWidget *btn_refresh_tasks = gtk_button_new_with_label("Refresh tasks");
    gtk_box_pack_end(GTK_BOX(task_top), btn_refresh_tasks, FALSE, FALSE, 0);

    a->tasks_store = gtk_list_store_new(6,
        G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    const char *tcols[] = {"ID","Title","Assignee","Status","Start","End"};
    a->tasks_view = GTK_TREE_VIEW(make_tree_view(a->tasks_store, tcols, 6));

    // Double click để xem detail
    g_signal_connect(a->tasks_view, "row-activated",
                     G_CALLBACK(on_task_row_activated), a);

    GtkWidget *task_sc = gtk_scrolled_window_new(NULL,NULL);
    gtk_container_add(GTK_CONTAINER(task_sc), GTK_WIDGET(a->tasks_view));
    gtk_box_pack_start(GTK_BOX(task_box), task_sc, TRUE, TRUE, 0);

    GtkWidget *task_form = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(task_form), 6);
    gtk_grid_set_column_spacing(GTK_GRID(task_form), 6);

    a->task_title_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(a->task_title_entry, "Task title");
    a->task_desc_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(a->task_desc_entry, "Description");
    GtkWidget *btn_create_task = gtk_button_new_with_label("Create task");

    a->task_id_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(a->task_id_entry,
                                   "Task ID (optional if select row)");
    a->assign_user_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(a->assign_user_entry, "Assign username");
    GtkWidget *btn_assign = gtk_button_new_with_label("Assign");

    // spin % progress
    GtkAdjustment *adj = gtk_adjustment_new(0, 0, 100, 1, 10, 0);
    a->progress_spin = GTK_SPIN_BUTTON(gtk_spin_button_new(adj, 1, 0));
    gtk_spin_button_set_numeric(a->progress_spin, TRUE);
    GtkWidget *btn_status = gtk_button_new_with_label("Update %");

    a->start_date_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(a->start_date_entry, "Start YYYY-MM-DD");
    a->end_date_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(a->end_date_entry, "End YYYY-MM-DD");
    GtkWidget *btn_dates = gtk_button_new_with_label("Set dates");

    int r=0;
    gtk_grid_attach(GTK_GRID(task_form), GTK_WIDGET(a->task_title_entry), 0,r,2,1);
    gtk_grid_attach(GTK_GRID(task_form), GTK_WIDGET(a->task_desc_entry),  2,r,2,1);
    gtk_grid_attach(GTK_GRID(task_form), btn_create_task,                 4,r,1,1);
    r++;
    gtk_grid_attach(GTK_GRID(task_form), GTK_WIDGET(a->task_id_entry),    0,r,1,1);
    gtk_grid_attach(GTK_GRID(task_form), GTK_WIDGET(a->assign_user_entry),1,r,2,1);
    gtk_grid_attach(GTK_GRID(task_form), btn_assign,                      3,r,1,1);
    gtk_grid_attach(GTK_GRID(task_form), GTK_WIDGET(a->progress_spin),    4,r,1,1);
    r++;
    gtk_grid_attach(GTK_GRID(task_form), btn_status,                      4,r-1,1,1);
    gtk_grid_attach(GTK_GRID(task_form), GTK_WIDGET(a->start_date_entry), 0,r,2,1);
    gtk_grid_attach(GTK_GRID(task_form), GTK_WIDGET(a->end_date_entry),   2,r,2,1);
    gtk_grid_attach(GTK_GRID(task_form), btn_dates,                       4,r,1,1);

    gtk_box_pack_start(GTK_BOX(task_box), task_form, FALSE, FALSE, 0);

    g_signal_connect(a->tasks_project_combo, "changed",
                     G_CALLBACK(on_tasks_project_changed), a);
    g_signal_connect(btn_refresh_tasks, "clicked",
                     G_CALLBACK(on_tasks_project_changed), a);
    g_signal_connect(btn_create_task, "clicked",
                     G_CALLBACK(on_btn_create_task), a);
    g_signal_connect(btn_assign, "clicked",
                     G_CALLBACK(on_btn_assign_task), a);
    g_signal_connect(btn_status, "clicked",
                     G_CALLBACK(on_btn_update_progress), a);
    g_signal_connect(btn_dates, "clicked",
                     G_CALLBACK(on_btn_set_dates), a);

    gtk_notebook_append_page(GTK_NOTEBOOK(tabs),
                             task_box, gtk_label_new("Tasks"));

    // --- Detail tab (Comments + Attachments) ---
    GtkWidget *detail_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *detail_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(detail_grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(detail_grid), 6);

    // Comments controls
    a->comment_task_id_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(a->comment_task_id_entry,
                                   "Task ID for comments");
    GtkWidget *btn_list_comments = gtk_button_new_with_label("Load comments");
    GtkWidget *btn_add_comment   = gtk_button_new_with_label("Add comment");

    a->comment_text = GTK_TEXT_VIEW(gtk_text_view_new());
    gtk_text_view_set_wrap_mode(a->comment_text, GTK_WRAP_WORD_CHAR);
    GtkWidget *comment_sc = gtk_scrolled_window_new(NULL,NULL);
    gtk_widget_set_size_request(comment_sc, -1, 100);
    gtk_container_add(GTK_CONTAINER(comment_sc),
                      GTK_WIDGET(a->comment_text));

    a->comments_list_text = GTK_TEXT_VIEW(gtk_text_view_new());
    gtk_text_view_set_editable(a->comments_list_text, FALSE);
    GtkWidget *comments_sc = gtk_scrolled_window_new(NULL,NULL);
    gtk_widget_set_size_request(comments_sc, -1, 160);
    gtk_container_add(GTK_CONTAINER(comments_sc),
                      GTK_WIDGET(a->comments_list_text));

    gtk_grid_attach(GTK_GRID(detail_grid), GTK_WIDGET(a->comment_task_id_entry), 0,0,2,1);
    gtk_grid_attach(GTK_GRID(detail_grid), btn_list_comments,                   2,0,1,1);
    gtk_grid_attach(GTK_GRID(detail_grid), btn_add_comment,                     3,0,1,1);
    gtk_grid_attach(GTK_GRID(detail_grid), gtk_label_new("New comment:"),      0,1,1,1);
    gtk_grid_attach(GTK_GRID(detail_grid), comment_sc,                          1,1,3,1);
    gtk_grid_attach(GTK_GRID(detail_grid), gtk_label_new("Comments:"),         0,2,1,1);
    gtk_grid_attach(GTK_GRID(detail_grid), comments_sc,                         1,2,3,1);

    // Attachments controls
    a->attach_task_id_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(a->attach_task_id_entry,
                                   "Task ID for attachments");
    a->attach_filename_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(a->attach_filename_entry, "filename");
    a->attach_filepath_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(a->attach_filepath_entry,
                                   "filepath (string) ");

    GtkWidget *btn_add_attach  = gtk_button_new_with_label("Add attachment");
    GtkWidget *btn_list_attach = gtk_button_new_with_label("Load attachments");

    a->attachments_list_text = GTK_TEXT_VIEW(gtk_text_view_new());
    gtk_text_view_set_editable(a->attachments_list_text, FALSE);
    GtkWidget *attach_sc = gtk_scrolled_window_new(NULL,NULL);
    gtk_widget_set_size_request(attach_sc, -1, 160);
    gtk_container_add(GTK_CONTAINER(attach_sc),
                      GTK_WIDGET(a->attachments_list_text));

    int rr = 3;
    gtk_grid_attach(GTK_GRID(detail_grid),
                    gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                    0,rr,4,1);
    rr++;
    gtk_grid_attach(GTK_GRID(detail_grid), GTK_WIDGET(a->attach_task_id_entry),  0,rr,1,1);
    gtk_grid_attach(GTK_GRID(detail_grid), GTK_WIDGET(a->attach_filename_entry), 1,rr,1,1);
    gtk_grid_attach(GTK_GRID(detail_grid), GTK_WIDGET(a->attach_filepath_entry), 2,rr,1,1);
    gtk_grid_attach(GTK_GRID(detail_grid), btn_add_attach,                       3,rr,1,1);
    rr++;
    gtk_grid_attach(GTK_GRID(detail_grid), btn_list_attach,                      3,rr,1,1);
    gtk_grid_attach(GTK_GRID(detail_grid), attach_sc,                            0,rr,3,1);

    gtk_box_pack_start(GTK_BOX(detail_box), detail_grid, TRUE, TRUE, 0);

    g_signal_connect(btn_add_comment,   "clicked", G_CALLBACK(on_btn_add_comment),      a);
    g_signal_connect(btn_list_comments, "clicked", G_CALLBACK(on_btn_list_comments),    a);
    g_signal_connect(btn_add_attach,    "clicked", G_CALLBACK(on_btn_add_attachment),   a);
    g_signal_connect(btn_list_attach,   "clicked", G_CALLBACK(on_btn_list_attachments), a);

    gtk_notebook_append_page(GTK_NOTEBOOK(tabs),
                             detail_box, gtk_label_new("Comments & Files"));

    // --- Gantt tab ---
    GtkWidget *gantt_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    a->gantt_area = GTK_DRAWING_AREA(gtk_drawing_area_new());
    gtk_widget_set_size_request(GTK_WIDGET(a->gantt_area), -1, 500);
    GtkWidget *gantt_sc = gtk_scrolled_window_new(NULL,NULL);
    gtk_container_add(GTK_CONTAINER(gantt_sc), GTK_WIDGET(a->gantt_area));
    gtk_box_pack_start(GTK_BOX(gantt_box), gantt_sc, TRUE, TRUE, 0);
    g_signal_connect(a->gantt_area, "draw", G_CALLBACK(gantt_draw_cb), a);

    GtkWidget *btn_refresh_gantt = gtk_button_new_with_label("Refresh gantt");
    gtk_box_pack_start(GTK_BOX(gantt_box), btn_refresh_gantt, FALSE, FALSE, 0);
    g_signal_connect(btn_refresh_gantt, "clicked",
                     G_CALLBACK(on_tasks_project_changed), a);

    gtk_notebook_append_page(GTK_NOTEBOOK(tabs),
                             gantt_box, gtk_label_new("Gantt"));

    // --- Chat tab ---
    GtkWidget *chat_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *chat_top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    a->chat_project_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_box_pack_start(GTK_BOX(chat_top), gtk_label_new("Project:"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(chat_top), GTK_WIDGET(a->chat_project_combo), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(chat_box), chat_top, FALSE, FALSE, 0);

    a->chat_view = GTK_TEXT_VIEW(gtk_text_view_new());
    gtk_text_view_set_editable(a->chat_view, FALSE);
    GtkWidget *chat_sc = gtk_scrolled_window_new(NULL,NULL);
    gtk_container_add(GTK_CONTAINER(chat_sc), GTK_WIDGET(a->chat_view));
    gtk_box_pack_start(GTK_BOX(chat_box), chat_sc, TRUE, TRUE, 0);

    GtkWidget *chat_send_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    a->chat_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(a->chat_entry, "Message...");
    GtkWidget *btn_send = gtk_button_new_with_label("Send");
    gtk_box_pack_start(GTK_BOX(chat_send_row), GTK_WIDGET(a->chat_entry), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(chat_send_row), btn_send, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(chat_box), chat_send_row, FALSE, FALSE, 0);

    g_signal_connect(btn_send, "clicked", G_CALLBACK(on_btn_send_chat), a);
    g_signal_connect(a->chat_project_combo, "changed",
                     G_CALLBACK(on_chat_project_changed), a);

    gtk_notebook_append_page(GTK_NOTEBOOK(tabs),
                             chat_box, gtk_label_new("Chat"));

    // Đăng ký timer poll chat mỗi 2s
    g_timeout_add(2000, poll_chat, a);

    return win;
}

// ----- main -----
int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    App *a = g_malloc0(sizeof(App));
    a->sockfd = connect_server();
    if (a->sockfd < 0) {
        fprintf(stderr, "Cannot connect to server on 127.0.0.1:%d\n",
                SERVER_PORT);
        return 1;
    }

    a->login_win = build_login(a);
    a->main_win  = build_main(a);
    gtk_widget_hide(a->main_win); // chỉ hiện sau khi login ok

    g_signal_connect(a->login_win, "destroy",
                     G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(a->main_win,  "destroy",
                     G_CALLBACK(gtk_main_quit), NULL);

    gtk_widget_show_all(a->login_win);
    gtk_main();

    if (a->sockfd >= 0) close(a->sockfd);
    g_free(a);
    return 0;
}
