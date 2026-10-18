#include "layout.h"
#include "util.h"

static const char *const row_1[] = {
    "q","w","e","r","t","y","u","i","o","p", NULL
};
static const char *const row_2[] = {
    "a","s","d","f","g","h","j","k","l", NULL
};
static const char *const row_3[] = {
    "shift","z","x","c","v","b","n","m","backspace", NULL
};
static const char *const row_4[] = {
    "space","enter", NULL
};

static GtkWidget *minimal_create(App *app) {
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, row_1), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, row_2), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, row_3), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, row_4), TRUE, TRUE, 0);
    return root;
}

static const LayoutOps minimal_ops = {
    .name = "minimal",
    .display_name = "最简",
    .transparent = FALSE,
    .create = minimal_create,
};

const LayoutOps *layout_minimal_ops(void) {
    return &minimal_ops;
}