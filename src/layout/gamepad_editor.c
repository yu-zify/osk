#include "gamepad_editor.h"
#include "gamepad_layout.h"
#include "layout.h"

#include <string.h>

/* ===== 摇杆方向绑定面板 =====
 *
 * 只做一件事：给某个摇杆配置四个方向各自绑定哪个键。
 * 其余编辑操作（增删、改位置、改映射、改尺寸）都在画布上
 * 通过单击/拖动/长按完成，见 layout_game.c 的捕获层。
 */

typedef struct {
    App       *app;
    GpItem    *stick;
    GtkWidget *combo[4];      /* 上 / 下 / 左 / 右 */
    GtkWidget *lbl_status;
} BindUI;

/* 可选的键。只列常用键，全列会让下拉框长到没法用。 */
static const char *const g_choices[] = {
    "（不绑定）",
    "w", "a", "s", "d",
    "up", "down", "left", "right",
    "space", "shift", "ctrl", "alt",
    "enter", "esc", "tab",
};

#define CHOICE_NONE g_choices[0]

static GtkWidget *make_combo(const char *current) {
    GtkWidget *c = gtk_combo_box_text_new();
    gint active = 0;

    for (gsize i = 0; i < G_N_ELEMENTS(g_choices); ++i) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c), g_choices[i]);
        if (current && strcmp(g_choices[i], current) == 0)
            active = (gint)i;
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(c), active);
    return c;
}

static void on_apply(GtkWidget *w, gpointer data) {
    (void)w;
    BindUI *ui = data;
    if (!ui->stick) return;

    for (int d = 0; d < 4; ++d) {
        gchar *sel = gtk_combo_box_text_get_active_text(
            GTK_COMBO_BOX_TEXT(ui->combo[d]));

        g_free(ui->stick->dir_id[d]);
        ui->stick->dir_id[d] =
            (sel && strcmp(sel, CHOICE_NONE) != 0) ? g_strdup(sel) : NULL;
        g_free(sel);
    }

    gamepad_save();

    /* 重建画布，让绑定立刻生效 */
    if (ui->app) app_rebuild_layout(ui->app);

    gtk_label_set_text(GTK_LABEL(ui->lbl_status), "已应用并保存");
}

static void bind_ui_free(gpointer p) {
    g_free(p);
}

static void on_destroy(GtkWidget *w, gpointer data) {
    App *app = data;
    if (app && app->window)
        g_object_set_data(G_OBJECT(app->window), "osk-stick-binds", NULL);
    (void)w;
}

void gamepad_stick_binds_open(App *app, GpItem *stick) {
    if (!app || !stick) return;

    GtkWidget *existing = g_object_get_data(G_OBJECT(app->window),
                                             "osk-stick-binds");
    if (existing) {
        gtk_window_present(GTK_WINDOW(existing));
        return;
    }

    BindUI *ui = g_new0(BindUI, 1);
    ui->app = app;
    ui->stick = stick;

    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "摇杆方向绑定");
    gtk_window_set_default_size(GTK_WINDOW(win), 320, 300);
    gtk_window_set_keep_above(GTK_WINDOW(win), TRUE);
    gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER);

    /* 与主窗口一致：无系统装饰，自绘白边 */
    app_setup_subwindow(win);

    g_object_set_data(G_OBJECT(app->window), "osk-stick-binds", win);
    g_object_set_data_full(G_OBJECT(win), "osk-bind-ui", ui, bind_ui_free);
    g_signal_connect(win, "destroy", G_CALLBACK(on_destroy), app);

    /* 统一外壳：标题栏（可拖动）+ 关闭按钮 */
    GtkWidget *content = app_subwindow_shell(win, "摇杆方向绑定");

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 14);
    gtk_box_pack_start(GTK_BOX(content), box, TRUE, TRUE, 0);

    /* 内容装好后再挂白边 */
    app_finish_subwindow(win);

    GtkWidget *hint = gtk_label_new("摇杆推开到这个方向时发送对应按键");
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 0);

    static const char *const dir_names[] = { "上", "下", "左", "右" };
    for (int d = 0; d < 4; ++d) {
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

        GtkWidget *l = gtk_label_new(dir_names[d]);
        gtk_widget_set_size_request(l, 32, -1);
        gtk_widget_set_halign(l, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(row), l, FALSE, FALSE, 0);

        ui->combo[d] = make_combo(stick->dir_id[d]);
        gtk_box_pack_start(GTK_BOX(row), ui->combo[d], TRUE, TRUE, 0);

        gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
    }

    GtkWidget *apply = gtk_button_new_with_label("应用");
    gtk_widget_set_can_focus(apply, FALSE);
    g_signal_connect(apply, "clicked", G_CALLBACK(on_apply), ui);
    gtk_box_pack_start(GTK_BOX(box), apply, FALSE, FALSE, 0);

    ui->lbl_status = gtk_label_new("");
    gtk_widget_set_halign(ui->lbl_status, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), ui->lbl_status, FALSE, FALSE, 0);

    gtk_widget_show_all(win);
}
