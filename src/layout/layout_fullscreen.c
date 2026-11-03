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
    "ctrl","alt","meta","esc","left","down","up","right","space*9", NULL
};

static GtkWidget *fullscreen_create(App *app) {
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL,
                                  app->config.key_spacing);
    gtk_widget_set_halign(root, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(root, GTK_ALIGN_CENTER);

    gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, row_0), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, row_1), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, row_2), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, row_3), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, row_4), FALSE, FALSE, 0);
    return root;
}

static const LayoutOps fullscreen_ops = {
    .name = "fullscreen",
    .display_name = "全屏",
    .transparent = TRUE,       /* 输入穿透 */
    .visual_transparent = TRUE,
    .hide_drag = TRUE,         /* 铺满屏幕，顶栏不放拖动区 */
    .create = fullscreen_create,
};

const LayoutOps *layout_fullscreen_ops(void) {
    return &fullscreen_ops;
}
