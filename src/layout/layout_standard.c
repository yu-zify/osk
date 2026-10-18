#include "layout.h"
#include "util.h"

static const char *const row_number[] = {
    "`","1","2","3","4","5","6","7","8","9","0","-","=", NULL
};
static const char *const row_upper[] = {
    "q","w","e","r","t","y","u","i","o","p","[","]", "\\", NULL
};
static const char *const row_middle[] = {
    "caps","a","s","d","f","g","h","j","k","l",";","'", NULL
};
static const char *const row_lower[] = {
    "shift","z","x","c","v","b","n","m",",",".","/","shift", NULL
};
static const char *const row_special[] = {
    "ctrl","alt","meta","esc","space","enter","backspace", NULL
};

static GtkWidget *standard_create(App *app) {
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, row_number),  TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, row_upper),   TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, row_middle),  TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, row_lower),   TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, row_special), TRUE, TRUE, 0);
    return root;
}

static const LayoutOps standard_ops = {
    .name = "standard",
    .display_name = "标准",
    .transparent = FALSE,
    .create = standard_create,
};

const LayoutOps *layout_standard_ops(void) {
    return &standard_ops;
}