#include "layout.h"
#include "util.h"

/* ===== 第一页：完整字母区 ===== */
static const char *const p1_row_number[] = {
    "`","1","2","3","4","5","6","7","8","9","0","-","=","backspace", NULL
};
static const char *const p1_row_upper[] = {
    "tab","q","w","e","r","t","y","u","i","o","p","[","]", "\\", NULL
};
static const char *const p1_row_middle[] = {
    "caps","a","s","d","f","g","h","j","k","l",";","'","enter*2", NULL
};
static const char *const p1_row_lower[] = {
    "shift*2","z","x","c","v","b","n","m",",",".","/","up","shift", NULL
};
static const char *const p1_row_special[] = {
    "esc","ctrl","alt","win","space*5","alt","ctrl","left","down","right", NULL
};

/* ===== 第二页：功能键 + 控制键 + 符号键 ===== */
static const char *const p2_row_f1[] = {
    "f1","f2","f3","f4","f5","f6", NULL
};
static const char *const p2_row_f2[] = {
    "f7","f8","f9","f10","f11","f12", NULL
};
static const char *const p2_row_1[] = {
    "esc","tab","backspace","enter", NULL
};
static const char *const p2_row_2[] = {
    "home","end","up","down","left","right", NULL
};
static const char *const p2_row_3[] = {
    "1","2","3","4","5","6","7","8","9","0", NULL
};
static const char *const p2_row_4[] = {
    "`","-","=","[","]","\\",";","'",",",".","/", NULL
};
static const char *const p2_row_5[] = {
    "shift","caps","ctrl","alt","meta","space*3", NULL
};

static GtkWidget *standard_create(App *app) {
    static const char *const *const page1[] = {
        p1_row_number, p1_row_upper, p1_row_middle,
        p1_row_lower, p1_row_special
    };
    static const char *const *const page2[] = {
        p2_row_f1, p2_row_f2, p2_row_1, p2_row_2,
        p2_row_3, p2_row_4, p2_row_5
    };

    const char *const *const *rows =
        (app->page == 1) ? page2 : page1;
    gint count = (app->page == 1)
                 ? (gint)G_N_ELEMENTS(page2)
                 : (gint)G_N_ELEMENTS(page1);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL,
                                  app->config.key_spacing);
    gtk_widget_set_halign(root, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(root, GTK_ALIGN_CENTER);
    for (gint i = 0; i < count; ++i)
        gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, rows[i]),
                           FALSE, FALSE, 0);
    return root;
}

static const LayoutOps standard_ops = {
    .name = "standard",
    .display_name = "标准",
    .transparent = FALSE,
    .pages = 2,
    .create = standard_create,
};

const LayoutOps *layout_standard_ops(void) {
    return &standard_ops;
}
