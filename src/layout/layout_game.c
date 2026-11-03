#include "layout.h"
#include "util.h"

static const char *const top_left_row[]   = {"esc", "tab", NULL};
static const char *const top_center_row[] = {"1","2","3","4","5","6","7","8","9", NULL};
static const char *const top_right_row[]  = {"backspace", "enter", NULL};

static const char *const wasd_r1[] = {"w", NULL};
static const char *const wasd_r2[] = {"a", "s", "d", NULL};
static const char *const *const wasd_rows[] = {wasd_r1, wasd_r2, NULL};

static const char *const act_r1[] = {"e", "q", "r", NULL};
static const char *const act_r2[] = {"f", "g", "h", NULL};
static const char *const *const act_rows[] = {act_r1, act_r2, NULL};

static const char *const mod_row[] = {"shift", "ctrl", "alt", "space*3", NULL};

static const char *const arr_r1[] = {"up", NULL};
static const char *const arr_r2[] = {"left", "down", "right", NULL};
static const char *const *const arr_rows[] = {arr_r1, arr_r2, NULL};

static GtkWidget *make_row(App *app, const char *const *ids) {
    GtkWidget *row = layout_build_row(app, ids);
    GList *children = gtk_container_get_children(GTK_CONTAINER(row));
    for (GList *ch = children; ch; ch = ch->next)
        gtk_style_context_add_class(
            gtk_widget_get_style_context(GTK_WIDGET(ch->data)),
            "osk-transparent-key");
    g_list_free(children);
    return row;
}

static GtkWidget *make_col(App *app, const char *const *const *rows) {
    GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL,
                                 app->config.key_spacing);
    for (int r = 0; rows[r]; ++r)
        gtk_box_pack_start(GTK_BOX(col), make_row(app, rows[r]),
                           FALSE, FALSE, 0);
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

static GtkWidget *game_create(App *app) {
    GtkWidget *overlay = gtk_overlay_new();

    GtkWidget *base = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(overlay), base);

    attach(overlay, make_row(app, top_left_row),
            GTK_ALIGN_START, GTK_ALIGN_START, 16, 0, 16, 0);
    attach(overlay, make_row(app, top_center_row),
            GTK_ALIGN_CENTER, GTK_ALIGN_START, 0, 0, 16, 0);
    attach(overlay, make_row(app, top_right_row),
            GTK_ALIGN_END, GTK_ALIGN_START, 0, 16, 16, 0);

    attach(overlay, make_col(app, wasd_rows),
            GTK_ALIGN_START, GTK_ALIGN_CENTER, 16, 0, 0, 0);
    attach(overlay, make_col(app, act_rows),
            GTK_ALIGN_END, GTK_ALIGN_CENTER, 0, 16, 0, 0);
    attach(overlay, make_row(app, mod_row),
            GTK_ALIGN_CENTER, GTK_ALIGN_END, 0, 0, 0, 16);
    attach(overlay, make_col(app, arr_rows),
            GTK_ALIGN_END, GTK_ALIGN_END, 0, 16, 0, 16);

    return overlay;
}

static const LayoutOps game_ops = {
    .name = "game",
    .display_name = "游戏",
    .transparent = TRUE,          /* 输入穿透 */
    .visual_transparent = TRUE,   /* 视觉透明 */
    .hide_drag = TRUE,            /* 铺满屏幕，顶栏不放拖动区 */
    .create = game_create,
};

const LayoutOps *layout_game_ops(void) {
    return &game_ops;
}
