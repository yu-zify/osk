#include "layout.h"
#include "util.h"
#include "widgets/touchpad.h"
#include "../backend/uinput_touchpad.h"

static GtkWidget *touchpad_create(App *app) {
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(root), 4);

    /* 触控板占满大部分区域 */
    TouchpadWidget *tw = touchpad_widget_new(app->touchpad);
    GtkWidget *area = touchpad_widget_gtk(tw);
    gtk_widget_set_size_request(area, -1, 240);
    gtk_box_pack_start(GTK_BOX(root), area, TRUE, TRUE, 0);

    /* 底部一行功能键 */
    static const char *const bottom_row[] = {
        "esc", "tab", "backspace", "enter", NULL
    };
    gtk_box_pack_start(GTK_BOX(root),
                        layout_build_row(app, bottom_row),
                        FALSE, FALSE, 0);

    return root;
}

static const LayoutOps touchpad_ops = {
    .name = "touchpad",
    .display_name = "触控板",
    .transparent = FALSE,
    .create = touchpad_create,
};

const LayoutOps *layout_touchpad_ops(void) {
    return &touchpad_ops;
}