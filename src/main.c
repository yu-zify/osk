#include <gtk/gtk.h>
#include "app.h"
#include "layout/layout.h"

static App g_app = {0};

/* ===== 布局切换 ===== */

static void app_set_layout(App *app, const LayoutOps *ops) {
    /* --- 清掉旧布局 --- */
    if (app->layout_host) {
        GList *children = gtk_container_get_children(
            GTK_CONTAINER(app->layout_host));
        for (GList *it = children; it; it = it->next)
            gtk_widget_destroy(GTK_WIDGET(it->data));
        g_list_free(children);
    }
    g_ptr_array_set_size(app->key_buttons, 0);
    app->shift_btn_count = 0;
    app->ctrl_btn = app->alt_btn = app->meta_btn = app->caps_btn = NULL;

    /* --- 窗口属性按布局切换 --- */
    gboolean fullscreen = ops && ops->transparent;
    if (fullscreen) {
        gtk_window_set_type_hint(GTK_WINDOW(app->window),
                                  GDK_WINDOW_TYPE_HINT_DOCK);
        gtk_window_set_skip_taskbar_hint(GTK_WINDOW(app->window), TRUE);
        gtk_window_set_keep_above(GTK_WINDOW(app->window), TRUE);
        gtk_window_maximize(GTK_WINDOW(app->window));
    } else {
        gtk_window_set_type_hint(GTK_WINDOW(app->window),
                                  GDK_WINDOW_TYPE_HINT_UTILITY);
        gtk_window_set_skip_taskbar_hint(GTK_WINDOW(app->window), FALSE);
        gtk_window_unmaximize(GTK_WINDOW(app->window));
        gtk_window_resize(GTK_WINDOW(app->window), 760, 360);
    }
    
    GtkStyleContext *ctx = gtk_widget_get_style_context(app->window);
    if (ops && ops->visual_transparent)
        gtk_style_context_add_class(ctx, "osk-transparent");
    else
        gtk_style_context_remove_class(ctx, "osk-transparent");
    
    /* 任何布局都不抢焦点 */
    gtk_window_set_accept_focus(GTK_WINDOW(app->window), FALSE);
    gtk_window_set_focus_on_map(GTK_WINDOW(app->window), FALSE);

    /* --- 创建新布局 --- */
    app->layout = ops;
    if (ops) {
        GtkWidget *root = ops->create(app);
        gtk_box_pack_start(GTK_BOX(app->layout_host), root, TRUE, TRUE, 0);
        gtk_widget_show_all(app->layout_host);
        app_refresh_labels(app);
    }

    /* --- input region --- */
    layout_apply_input_region(app, fullscreen);
}

static gboolean on_window_size_allocate(GtkWidget *w, GdkRectangle *alloc,
                                         gpointer data) {
    (void)w; (void)alloc;
    App *app = data;
    if (app->layout && app->layout->transparent)
        layout_apply_input_region(app, TRUE);
    return FALSE;
}

static void on_layout_button(GtkButton *btn, gpointer user_data) {
    (void)user_data;
    const char *name = g_object_get_data(G_OBJECT(btn), "osk-layout");
    const LayoutOps *ops = layout_lookup(name);
    if (ops) {
        g_print("[OSK] 切换到布局：%s\n", ops->display_name);
        app_set_layout(&g_app, ops);
    }
}

/* ===== 窗口销毁 ===== */

static void on_window_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget; (void)user_data;
    osk_keyboard_free(g_app.kbd);
    g_app.kbd = NULL;
    osk_touchpad_free(g_app.touchpad); 
    g_app.touchpad = NULL;
    if (g_app.key_buttons) {
        g_ptr_array_free(g_app.key_buttons, TRUE);
        g_app.key_buttons = NULL;
    }
    gtk_main_quit();
}

/* ===== main ===== */

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    /* CSS */
    static const char *app_css =
    ".osk-key {"
    "  border: 1px solid #555;"
    "  border-radius: 4px;"
    "  background-color: #3c3c3c;"
    "  color: #ffffff;"
    "}"
    ".osk-key.swipe-active {"
    "  background-color: #d94a90;"
    "}"
    ".osk-label-main  { font-size: 13px; }"
    ".osk-label-shift { font-size: 9px; color: #aaa; }"
    "window.osk-transparent { background-color: rgba(0,0,0,0); }"
    ".osk-key.osk-transparent-key {"
    "  background-color: transparent;"
    "  border: 1px solid rgba(255, 255, 255, 0.45);"
    "  border-radius: 6px;"
    "}"
    ".osk-key.osk-transparent-key .osk-label-main {"
    "  color: #ffffff;"
    "  text-shadow: 1px 1px 3px rgba(0, 0, 0, 0.9);"
    "}"
    ".osk-key.osk-transparent-key .osk-label-shift,"
    ".osk-key.osk-transparent-key .osk-label-up {"
    "  color: #dddddd;"
    "  text-shadow: 1px 1px 3px rgba(0, 0, 0, 0.9);"
    "}"
    ".osk-label-up    { font-size: 9px; color: #aaa; }";
    
    GtkCssProvider *css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css_provider, app_css, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css_provider);


    GError *error = NULL;
    g_app.kbd = osk_keyboard_new("/dev/uinput", &error);
    if (!g_app.kbd) {
        g_printerr("[OSK] uinput 初始化失败：%s\n", error->message);
        g_clear_error(&error);
        return 1;
    }
    g_app.touchpad = osk_touchpad_new("/dev/uinput", 4096, 4096, &error);
    if (!g_app.touchpad) {
       g_printerr("[OSK] 触控板初始化失败：%s（键盘不受影响）\n",
                   error->message);
       g_clear_error(&error);
      /* 触控板失败不算致命，继续 */
    }

    g_app.key_buttons = g_ptr_array_new();

    g_app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(g_app.window), "OSK 教学版");
    gtk_window_set_default_size(GTK_WINDOW(g_app.window), 760, 320);
    gtk_window_set_keep_above(GTK_WINDOW(g_app.window), TRUE);
    g_signal_connect(g_app.window, "destroy",
                     G_CALLBACK(on_window_destroy), NULL);
                     
    GdkScreen *screen = gtk_widget_get_screen(g_app.window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual && gdk_screen_is_composited(screen))
        gtk_widget_set_visual(g_app.window, visual);
    gtk_widget_set_app_paintable(g_app.window, TRUE);

    /* 外层容器 + 布局挂载点 */
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(g_app.window), outer);

    /* === 布局切换工具栏 === */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_start(toolbar, 4);
    gtk_widget_set_margin_end(toolbar, 4);
    gtk_widget_set_margin_top(toolbar, 4);
    gtk_box_pack_start(GTK_BOX(outer), toolbar, FALSE, FALSE, 0);

    static const struct {
        const char *label;
        const char *layout;
    } layout_buttons[] = {
        { "标准", "standard"   },
        { "最简", "minimal"    },
        { "全屏", "fullscreen" },
        { "触控板", "touchpad" },
        { "游戏", "game" },
    };
    for (gsize i = 0; i < G_N_ELEMENTS(layout_buttons); ++i) {
        GtkWidget *btn = gtk_button_new_with_label(layout_buttons[i].label);
        g_object_set_data(G_OBJECT(btn), "osk-layout",
                          (gpointer)layout_buttons[i].layout);
        gtk_widget_set_can_focus(btn, FALSE);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_layout_button), NULL);
        gtk_box_pack_start(GTK_BOX(toolbar), btn, FALSE, FALSE, 0);
    }

    /* 布局挂载点 */
    g_app.layout_host = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(outer), g_app.layout_host, TRUE, TRUE, 0);

    /* 加载默认布局 */
    app_set_layout(&g_app, layout_lookup("standard"));
    

    gtk_widget_show_all(g_app.window);
    g_signal_connect(g_app.window, "size-allocate",
                  G_CALLBACK(on_window_size_allocate), &g_app);
    gtk_main();
    return 0;
}