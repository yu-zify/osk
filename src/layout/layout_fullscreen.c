#include "layout.h"
#include "util.h"

static const char *const row_0[] = {
    "esc","`","1","2","3","4","5","6","7","8","9","0","-","=","backspace", NULL
};
static const char *const row_1[] = {
    "tab","q","w","e","r","t","y","u","i","o","p","[","]","\\", NULL
};
static const char *const row_2[] = {
    "caps","a","s","d","f","g","h","j","k","l",";","'","enter", NULL
};
static const char *const row_3[] = {
    "shift","z","x","c","v","b","n","m",",",".","/","shift", NULL
};
static const char *const row_4[] = {
    "ctrl","alt","meta","esc","space","enter", NULL
};

static GtkWidget *fullscreen_create(App *app) {
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

    /* 测试带：故意留 60px 空白，用来验证穿透 */
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(spacer, -1, 60);
    gtk_box_pack_start(GTK_BOX(root), spacer, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, row_0), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, row_1), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, row_2), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, row_3), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, row_4), TRUE, TRUE, 0);
    return root;
}

static const LayoutOps fullscreen_ops = {
    .name = "fullscreen",
    .display_name = "全屏",
    .transparent = TRUE,       /* 新增 */
    .create = fullscreen_create,
};

const LayoutOps *layout_fullscreen_ops(void) {
    return &fullscreen_ops;
}