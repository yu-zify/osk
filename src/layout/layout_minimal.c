#include "layout.h"
#include "util.h"

static const char *const row_1[] = {
    "q","w","e","r","t","y","u","i","o","p", NULL
};
static const char *const row_2[] = {
    "a","s","d","f","g","h","j","k","l","backspace", NULL
};
static const char *const row_3[] = {
    "shift","z","x","c","v","b","n","m","up","enter", NULL
};
static const char *const row_4[] = {
    "esc","ctrl","alt","space*3","left","down","right", NULL
};

static GtkWidget *minimal_create(App *app) {
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL,
                                  app->config.key_spacing);
    gtk_widget_set_halign(root, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(root, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, row_1), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, row_2), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, row_3), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, row_4), FALSE, FALSE, 0);
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
