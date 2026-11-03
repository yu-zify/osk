#include "layout.h"
#include "util.h"

static const char *const row_number[] = {
    "`","1","2","3","4","5","6","7","8","9","0","-","=","backspace", NULL
};
static const char *const row_upper[] = {
    "tab","q","w","e","r","t","y","u","i","o","p","[","]", "\\", NULL
};
static const char *const row_middle[] = {
    "caps","a","s","d","f","g","h","j","k","l",";","'","enter", NULL
};
static const char *const row_lower[] = {
    "shift","z","x","c","v","b","n","m",",",".","/","shift", NULL
};
static const char *const row_special[] = {
    "ctrl","alt","meta","esc","left","down","up","right","space*5" , NULL
};

static GtkWidget *standard_create(App *app) {
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL,
                                  app->config.key_spacing);
    gtk_widget_set_halign(root, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(root, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, row_number), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, row_upper),  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, row_middle), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, row_lower),  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, row_special), FALSE, FALSE, 0);
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
