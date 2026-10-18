#include "layout.h"
#include "util.h"

#define KEY_SIZE 64
#define SPACING  6
#define MARGIN   16

static GtkWidget *make_row(App *app, const char *const *ids) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, SPACING);
    for (int i = 0; ids[i]; ++i) {
        KeyButton *kb = layout_make_key(app, ids[i]);
        if (!kb) continue;
        GtkWidget *w = key_button_widget(kb);
        gtk_widget_set_size_request(w, KEY_SIZE, KEY_SIZE);

        /* 关键：加透明 class */
        gtk_style_context_add_class(gtk_widget_get_style_context(w),
                                     "osk-transparent-key");

        gtk_box_pack_start(GTK_BOX(row), w, FALSE, FALSE, 0);
    }
    return row;
}

static GtkWidget *make_col(App *app, const char *const *const *rows) {
    GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, SPACING);
    for (int r = 0; rows[r]; ++r)
        gtk_box_pack_start(GTK_BOX(col), make_row(app, rows[r]), FALSE, FALSE, 0);
    return col;
}

static void attach(GtkWidget *overlay, GtkWidget *child,
                    GtkAlign halign, GtkAlign valign,
                    int ms, int me, int mt, int mb) {
    gtk_widget_set_halign(child, halign);
    gtk_widget_set_valign(child, valign);
    if (ms) gtk_widget_set_margin_start(child, ms);
    if (me) gtk_widget_set_margin_end(child, me);
    if (mt) gtk_widget_set_margin_top(child, mt);
    if (mb) gtk_widget_set_margin_bottom(child, mb);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), child);
}

/* 各组键定义 */
static const char *const top_left_row[]   = {"esc", "tab", NULL};
static const char *const top_center_row[] = {"1","2","3","4","5","6","7","8","9", NULL};
static const char *const top_right_row[]  = {"backspace", "enter", NULL};

static const char *const wasd_r1[] = {"w", NULL};
static const char *const wasd_r2[] = {"a", "s", "d", NULL};
static const char *const *const wasd_rows[] = {wasd_r1, wasd_r2, NULL};

static const char *const act_r1[] = {"e", "q", "r", NULL};
static const char *const act_r2[] = {"f", "g", "h", NULL};
static const char *const *const act_rows[] = {act_r1, act_r2, NULL};

static const char *const mod_row[] = {"shift", "ctrl", "alt", "space", NULL};

static const char *const arr_r1[] = {"up", NULL};
static const char *const arr_r2[] = {"left", "down", "right", NULL};
static const char *const *const arr_rows[] = {arr_r1, arr_r2, NULL};

static GtkWidget *game_create(App *app) {
    GtkWidget *overlay = gtk_overlay_new();

    /* 底 child：撑满整个窗口的透明容器 */
    GtkWidget *base = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(overlay), base);

    /* 左上：Esc Tab */
    attach(overlay, make_row(app, top_left_row),
            GTK_ALIGN_START, GTK_ALIGN_START, MARGIN, 0, MARGIN, 0);

    /* 顶部中间：1-9 */
    attach(overlay, make_row(app, top_center_row),
            GTK_ALIGN_CENTER, GTK_ALIGN_START, 0, 0, MARGIN, 0);

    /* 右上：Backspace Enter */
    attach(overlay, make_row(app, top_right_row),
            GTK_ALIGN_END, GTK_ALIGN_START, 0, MARGIN, MARGIN, 0);

    /* 左侧中部：WASD */
    attach(overlay, make_col(app, wasd_rows),
            GTK_ALIGN_START, GTK_ALIGN_CENTER, MARGIN, 0, 0, 0);

    /* 右侧中部：E Q R / F G H */
    attach(overlay, make_col(app, act_rows),
            GTK_ALIGN_END, GTK_ALIGN_CENTER, 0, MARGIN, 0, 0);

    /* 底部中间：Shift Ctrl Alt Space */
    attach(overlay, make_row(app, mod_row),
            GTK_ALIGN_CENTER, GTK_ALIGN_END, 0, 0, 0, MARGIN);

    /* 右下角：方向键 */
    attach(overlay, make_col(app, arr_rows),
            GTK_ALIGN_END, GTK_ALIGN_END, 0, MARGIN, 0, MARGIN);

    return overlay;
}

static const LayoutOps game_ops = {
    .name = "game",
    .display_name = "游戏",
    .transparent = TRUE,          /* 输入穿透 */
    .visual_transparent = TRUE,   /* 视觉透明 */
    .create = game_create,
};

const LayoutOps *layout_game_ops(void) {
    return &game_ops;
}