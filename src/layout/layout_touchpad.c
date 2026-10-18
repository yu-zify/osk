#include "layout.h"
#include "util.h"
#include "widgets/touchpad.h"
#include "../backend/uinput_mouse.h"

typedef struct {
    OskMouse *mouse;
    guint     button;
} MouseBtnCtx;

static void on_mouse_btn_pressed(GtkButton *btn, gpointer data) {
    (void)btn;
    MouseBtnCtx *ctx = data;
    osk_mouse_button(ctx->mouse, ctx->button, TRUE);
}

static void on_mouse_btn_released(GtkButton *btn, gpointer data) {
    (void)btn;
    MouseBtnCtx *ctx = data;
    osk_mouse_button(ctx->mouse, ctx->button, FALSE);
}

static GtkWidget *make_mouse_button(App *app, const char *label, guint button) {
    GtkWidget *btn = gtk_button_new_with_label(label);
    gtk_widget_set_can_focus(btn, FALSE);
    gtk_widget_set_focus_on_click(btn, FALSE);
    gtk_widget_set_size_request(btn, -1, 48);
    gtk_widget_set_hexpand(btn, TRUE);

    MouseBtnCtx *ctx = g_new0(MouseBtnCtx, 1);
    ctx->mouse  = app->mouse;
    ctx->button = button;
    g_object_set_data_full(G_OBJECT(btn), "ctx", ctx, g_free);

    g_signal_connect(btn, "pressed",
                     G_CALLBACK(on_mouse_btn_pressed),  ctx);
    g_signal_connect(btn, "released",
                     G_CALLBACK(on_mouse_btn_released), ctx);

    return btn;
}

static GtkWidget *touchpad_create(App *app) {
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(root), 4);

    TouchpadWidget *tw = touchpad_widget_new(app->mouse);
    GtkWidget *area = touchpad_widget_gtk(tw);
    gtk_widget_set_size_request(area, -1, 240);
    gtk_box_pack_start(GTK_BOX(root), area, TRUE, TRUE, 0);

    /* 鼠标左中右按钮 */
    GtkWidget *mouse_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(mouse_row),
                        make_mouse_button(app, "左键", BTN_LEFT),
                        TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(mouse_row),
                        make_mouse_button(app, "中键", BTN_MIDDLE),
                        TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(mouse_row),
                        make_mouse_button(app, "右键", BTN_RIGHT),
                        TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), mouse_row, FALSE, FALSE, 0);

    /* 底部功能键（原有） */
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