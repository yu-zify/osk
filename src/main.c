#include <gtk/gtk.h>
#include "app.h"
#include "settings.h"
#include "layout/layout.h"

static App g_app = {0};

/* ===== 拖动窗口用的辅助 ===== */

/* 顶栏拖动区：按住左键拖动整窗 */
static gboolean on_drag_begin(GtkWidget *w, GdkEventButton *ev, gpointer data) {
    (void)w;
    GtkWidget *window = data;
    if (ev->button != 1) return FALSE;

    gtk_window_begin_move_drag(GTK_WINDOW(window), 1,
                               (gint)ev->x_root, (gint)ev->y_root,
                               ev->time);
    return TRUE;
}

/* ===== 布局切换 ===== */

void app_rebuild_layout(App *app) {
    if (!app || !app->layout) return;

    /* 清掉旧布局 */
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

    /* 重建 */
    GtkWidget *root = app->layout->create(app);
    gtk_box_pack_start(GTK_BOX(app->layout_host), root, TRUE, TRUE, 0);
    gtk_widget_show_all(app->layout_host);

    app_refresh_labels(app);
    layout_apply_input_region(app, app->layout->transparent);

    /* 键宽高/间距变了以后，非穿透布局让窗口收缩到刚好容纳键盘 */
    if (!app->layout->transparent)
        gtk_window_resize(GTK_WINDOW(app->window), 1, 1);
}

static void app_set_layout(App *app, const LayoutOps *ops) {
    if (!app || !ops) return;

    /* --- 窗口属性按布局切换 --- */
    gboolean fullscreen = ops->transparent;
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
    }

    GtkStyleContext *ctx = gtk_widget_get_style_context(app->window);
    if (ops->visual_transparent)
        gtk_style_context_add_class(ctx, "osk-transparent");
    else
        gtk_style_context_remove_class(ctx, "osk-transparent");

    /* 任何布局都不抢焦点 */
    gtk_window_set_accept_focus(GTK_WINDOW(app->window), FALSE);
    gtk_window_set_focus_on_map(GTK_WINDOW(app->window), FALSE);

    app->layout = ops;
    app_rebuild_layout(app);

    /* 从穿透布局切回普通布局时，恢复到能容纳键盘的尺寸 */
    if (!ops->transparent)
        gtk_window_resize(GTK_WINDOW(app->window), 1, 1);
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

/* ===== 顶栏按钮 ===== */

static void on_settings_clicked(GtkButton *btn, gpointer data) {
    (void)btn;
    settings_open((App *)data);
}

static void on_minimize_clicked(GtkButton *btn, gpointer data) {
    (void)btn;
    gtk_window_iconify(GTK_WINDOW(data));
}

/* ===== 窗口销毁 ===== */

static void on_window_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget; (void)user_data;
    osk_keyboard_free(g_app.kbd);
    g_app.kbd = NULL;
    osk_mouse_free(g_app.mouse);
    g_app.mouse = NULL;
    if (g_app.key_buttons) {
        g_ptr_array_free(g_app.key_buttons, TRUE);
        g_app.key_buttons = NULL;
    }
    gtk_main_quit();
}

/* ===== 顶栏 ===== */

static GtkWidget *build_toolbar(App *app) {
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkStyleContext *bctx = gtk_widget_get_style_context(bar);
    gtk_style_context_add_class(bctx, "osk-toolbar");

    /* --- 左侧：布局按钮 --- */
    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    static const struct {
        const char *label;
        const char *layout;
    } layout_buttons[] = {
        { "标准",   "standard"   },
        { "最简",   "minimal"    },
        { "全屏",   "fullscreen" },
        { "触控板", "touchpad"   },
        { "游戏",   "game"       },
    };
    for (gsize i = 0; i < G_N_ELEMENTS(layout_buttons); ++i) {
        GtkWidget *btn = gtk_button_new_with_label(layout_buttons[i].label);
        g_object_set_data(G_OBJECT(btn), "osk-layout",
                          (gpointer)layout_buttons[i].layout);
        gtk_widget_set_can_focus(btn, FALSE);
        gtk_widget_set_focus_on_click(btn, FALSE);
        gtk_style_context_add_class(gtk_widget_get_style_context(btn),
                                    "osk-tool");
        g_signal_connect(btn, "clicked", G_CALLBACK(on_layout_button), NULL);
        gtk_box_pack_start(GTK_BOX(left), btn, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(bar), left, FALSE, FALSE, 0);

    /* --- 中间：拖动区（标题） --- */
    GtkWidget *drag_area = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(drag_area), FALSE);
    gtk_widget_set_hexpand(drag_area, TRUE);
    gtk_widget_add_events(drag_area, GDK_BUTTON_PRESS_MASK);
    gtk_style_context_add_class(gtk_widget_get_style_context(drag_area),
                                "osk-drag-area");

    GtkWidget *title = gtk_label_new("OSK 屏幕键盘  ·  按住此处拖动");
    gtk_widget_set_halign(title, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(title, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(title),
                                "osk-title");
    gtk_container_add(GTK_CONTAINER(drag_area), title);
    g_signal_connect(drag_area, "button-press-event",
                     G_CALLBACK(on_drag_begin), app->window);
    gtk_box_pack_start(GTK_BOX(bar), drag_area, TRUE, TRUE, 0);

    /* --- 右侧：设置、最小化 --- */
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

    GtkWidget *settings = gtk_button_new_with_label("⚙");
    gtk_widget_set_can_focus(settings, FALSE);
    gtk_widget_set_focus_on_click(settings, FALSE);
    gtk_widget_set_tooltip_text(settings, "设置");
    gtk_style_context_add_class(gtk_widget_get_style_context(settings),
                                "osk-tool");
    g_signal_connect(settings, "clicked",
                     G_CALLBACK(on_settings_clicked), app);
    gtk_box_pack_start(GTK_BOX(right), settings, FALSE, FALSE, 0);

    GtkWidget *minimize = gtk_button_new_with_label("—");
    gtk_widget_set_can_focus(minimize, FALSE);
    gtk_widget_set_focus_on_click(minimize, FALSE);
    gtk_widget_set_tooltip_text(minimize, "最小化");
    gtk_style_context_add_class(gtk_widget_get_style_context(minimize),
                                "osk-tool");
    g_signal_connect(minimize, "clicked",
                     G_CALLBACK(on_minimize_clicked), app->window);
    gtk_box_pack_start(GTK_BOX(right), minimize, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(bar), right, FALSE, FALSE, 0);

    return bar;
}

/* ===== CSS ===== */

static const char *app_css =
    /* 键盘背景 */
    "window.osk-window {"
    "  background-color: #23262b;"
    "}"
    /* 顶栏 */
    ".osk-toolbar {"
    "  background-color: #2b2f36;"
    "  padding: 5px 8px;"
    "  border-bottom: 1px solid #3a3f47;"
    "}"
    ".osk-title {"
    "  color: #8b929c;"
    "  font-size: 11px;"
    "}"
    ".osk-tool {"
    "  min-height: 24px;"
    "  min-width: 30px;"
    "  padding: 2px 9px;"
    "  border: 1px solid #454b54;"
    "  border-radius: 6px;"
    "  background-image: none;"
    "  background-color: #343943;"
    "  color: #d6dae0;"
    "  font-size: 12px;"
    "  text-shadow: none;"
    "  box-shadow: none;"
    "}"
    ".osk-tool:hover {"
    "  background-color: #3f4652;"
    "  border-color: #5a6270;"
    "}"
    ".osk-tool:active {"
    "  background-color: #2a2f38;"
    "}"
    /* 按键：固定尺寸、圆角、柔和阴影 */
    ".osk-key {"
    "  border: 1px solid #4a515c;"
    "  border-radius: 8px;"
    "  background-image: none;"
    "  background-color: #3a404a;"
    "  color: #eef1f5;"
    "  box-shadow: 0 1px 0 rgba(0,0,0,0.35);"
    "  transition: background-color 90ms ease;"
    "}"
    ".osk-key.swipe-active {"
    "  background-color: #c5548f;"
    "  border-color: #e580b4;"
    "}"
    ".osk-label-main {"
    "  font-size: 14px;"
    "}"
    /* 大写状态：主标签加粗 */
    ".osk-key.key-upper .osk-label-main {"
    "  font-weight: bold;"
    "  color: #ffffff;"
    "}"
    /* 右上角两个上滑角标 */
    ".osk-label-up, .osk-label-up-shift {"
    "  font-size: 9px;"
    "  color: #6f7783;"
    "}"
    /* 当前上滑会输出的那一档加亮 */
    ".osk-label-up.up-current {"
    "  color: #7fd3ff;"
    "}"
    ".osk-label-up-shift.up-current {"
    "  color: #ffc46b;"
    "}"
    /* 左上角：符号键的 Shift 形态 */
    ".osk-label-hint {"
    "  font-size: 9px;"
    "  color: #8f97a3;"
    "}"
    /* 修饰键：Shift 单次 = 蓝色 */
    ".osk-key.mod-oneshot {"
    "  background-color: #2f6fd0;"
    "  border-color: #5b9bf0;"
    "  color: #ffffff;"
    "}"
    /* 修饰键：锁定 = 红色 */
    ".osk-key.mod-locked {"
    "  background-color: #c9433c;"
    "  border-color: #f0706a;"
    "  color: #ffffff;"
    "}"
    /* 透明布局 */
    "window.osk-transparent { background-color: rgba(0,0,0,0); }"
    ".osk-key.osk-transparent-key {"
    "  background-color: rgba(28,31,36,0.55);"
    "  border: 1px solid rgba(255, 255, 255, 0.30);"
    "  border-radius: 8px;"
    "}"
    ".osk-key.osk-transparent-key .osk-label-main {"
    "  color: #ffffff;"
    "  text-shadow: 1px 1px 3px rgba(0, 0, 0, 0.9);"
    "}"
    ".osk-key.osk-transparent-key .osk-label-up,"
    ".osk-key.osk-transparent-key .osk-label-up-shift {"
    "  text-shadow: 1px 1px 3px rgba(0, 0, 0, 0.9);"
    "}"
    ".osk-key.osk-transparent-key .osk-label-hint {"
    "  color: #c8ccd4;"
    "  text-shadow: 1px 1px 3px rgba(0, 0, 0, 0.9);"
    "}";

/* ===== main ===== */

int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    GtkCssProvider *css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css_provider, app_css, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css_provider);

    /* 默认外观 */
    g_app.config.key_width   = 54;
    g_app.config.key_height  = 46;
    g_app.config.key_spacing = 4;
    g_app.config.opacity     = 1.0;

    GError *error = NULL;
    g_app.kbd = osk_keyboard_new("/dev/uinput", &error);
    if (!g_app.kbd) {
        g_printerr("[OSK] uinput 初始化失败：%s\n", error->message);
        g_clear_error(&error);
        return 1;
    }
    g_app.mouse = osk_mouse_new("/dev/uinput", &error);
    if (!g_app.mouse) {
        g_printerr("[OSK] 鼠标初始化失败：%s（键盘不受影响）\n",
                    error->message);
        g_clear_error(&error);
    }

    g_app.key_buttons = g_ptr_array_new();

    g_app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(g_app.window), "OSK 屏幕键盘");
    gtk_window_set_default_size(GTK_WINDOW(g_app.window), 800, 380);
    gtk_window_set_keep_above(GTK_WINDOW(g_app.window), TRUE);

    /* 隐藏标题栏：无边框 + 透明窗口，由自绘顶栏承担拖动与按钮 */
    gtk_window_set_decorated(GTK_WINDOW(g_app.window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(g_app.window), TRUE);

    g_signal_connect(g_app.window, "destroy",
                     G_CALLBACK(on_window_destroy), NULL);

    GdkScreen *screen = gtk_widget_get_screen(g_app.window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual && gdk_screen_is_composited(screen))
        gtk_widget_set_visual(g_app.window, visual);
    gtk_widget_set_app_paintable(g_app.window, TRUE);

    GtkStyleContext *wctx = gtk_widget_get_style_context(g_app.window);
    gtk_style_context_add_class(wctx, "osk-window");

    /* 外层容器 + 布局挂载点 */
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(g_app.window), outer);

    GtkWidget *toolbar = build_toolbar(&g_app);
    gtk_box_pack_start(GTK_BOX(outer), toolbar, FALSE, FALSE, 0);

    g_app.layout_host = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(g_app.layout_host, 10);
    gtk_widget_set_margin_end(g_app.layout_host, 10);
    gtk_widget_set_margin_top(g_app.layout_host, 10);
    gtk_widget_set_margin_bottom(g_app.layout_host, 10);
    gtk_box_pack_start(GTK_BOX(outer), g_app.layout_host, TRUE, TRUE, 0);

    /* 加载默认布局 */
    app_set_layout(&g_app, layout_lookup("standard"));
    app_apply_config(&g_app);

    gtk_widget_show_all(g_app.window);
    g_signal_connect(g_app.window, "size-allocate",
                  G_CALLBACK(on_window_size_allocate), &g_app);
    gtk_main();
    return 0;
}
