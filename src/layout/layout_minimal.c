#include "layout.h"
#include "util.h"

/* ===== 第一页：字母主键区 ===== */
static const char *const p1_row_1[] = {
    "q","w","e","r","t","y","u","i","o","p", NULL
};
static const char *const p1_row_2[] = {
    "a","s","d","f","g","h","j","k","l","backspace", NULL
};
static const char *const p1_row_3[] = {
    "tab","z","x","c","v","b","n","m","up","enter", NULL
};
static const char *const p1_row_4[] = {
    "esc","shift","ctrl","alt","space*2","win","left","down","right", NULL
};

/* ===== 第二页：功能键 + 控制键 + 符号键 =====
 * 符号键复用 keylib 里已有的定义（它们本身带 Shift 形态，
 * Shift 激活时中间会显示 ! @ # 等上档符号）。 */
static const char *const p2_row_f1[] = {
    "f1","f2","f3","f4","f5","f6","1","2","3","backspace", NULL
};
static const char *const p2_row_f2[] = {
    "f7","f8","f9","f10","f11","f12","4","5","6","enter", NULL
};
static const char *const p2_row_1[] = {
    "tab","`","-","=","[","]","7","8","9","home", NULL
};
static const char *const p2_row_2[] = {
    "shift","\\",";","'",",",".","/","0","up","end", NULL
};
static const char *const p2_row_3[] = {
    "esc","ctrl","alt","win","space*3","left","down","right", NULL
};

static GtkWidget *make_column(App *app, const char *const *const *rows,
                               int count) {
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL,
                                  app->config.key_spacing);
    gtk_widget_set_halign(root, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(root, GTK_ALIGN_CENTER);
    for (int i = 0; i < count; ++i)
        gtk_box_pack_start(GTK_BOX(root), layout_build_row(app, rows[i]),
                           FALSE, FALSE, 0);
    return root;
}

static GtkWidget *minimal_create(App *app) {
    static const char *const *const page1[] = {
        p1_row_1, p1_row_2, p1_row_3, p1_row_4
    };
    static const char *const *const page2[] = {
        p2_row_f1, p2_row_f2, p2_row_1, p2_row_2,
        p2_row_3
    };

    if (app->page == 1)
        return make_column(app, page2, G_N_ELEMENTS(page2));
    return make_column(app, page1, G_N_ELEMENTS(page1));
}

static const LayoutOps minimal_ops = {
    .name = "minimal",
    .display_name = "最简",
    .transparent = FALSE,
    .pages = 2,
    .create = minimal_create,
};

const LayoutOps *layout_minimal_ops(void) {
    return &minimal_ops;
}
