#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include "app.h"
#include "settings.h"
#include "layout/layout.h"

static App g_app = {0};

/* 前向声明：app_set_layout 里会用到 */
static void update_page_button(App *app);
static void apply_borderless(App *app);

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

/* 窗口尺寸跟随键盘大小：
 * 按键是固定尺寸的，所以内容的首选尺寸是确定的；把窗口收缩到刚好容纳内容。
 * 全屏/游戏这类穿透布局交给窗口管理器，不参与。 */
static guint g_fit_idle = 0;

static gboolean fit_window_idle(gpointer data) {
    g_fit_idle = 0;
    App *app = data;
    if (!app || !app->window || !app->layout) return G_SOURCE_REMOVE;
    if (app->layout->transparent) return G_SOURCE_REMOVE;

    GtkWidget *content = gtk_bin_get_child(GTK_BIN(app->window));
    if (!content) return G_SOURCE_REMOVE;

    GtkRequisition nat;
    gtk_widget_get_preferred_size(content, NULL, &nat);
    if (nat.width  < 1) nat.width  = 1;
    if (nat.height < 1) nat.height = 1;

    gtk_window_resize(GTK_WINDOW(app->window), nat.width, nat.height);
    return G_SOURCE_REMOVE;
}

static void schedule_fit(App *app) {
    if (!app || !app->layout) return;
    if (app->layout->transparent) return;   /* 穿透布局不跟着收缩 */
    if (g_fit_idle) g_source_remove(g_fit_idle);
    /* 稍等一拍，让新布局完成分配后再量尺寸 */
    g_fit_idle = g_timeout_add(30, fit_window_idle, app);
}

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

    /* 窗口跟着键盘实际大小变化 */
    schedule_fit(app);
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
    app->page   = 0;          /* 切布局时回到第一页 */

    /* 全屏/游戏这类铺满屏幕的布局不需要拖动区 */
    if (app->drag_area)
        gtk_widget_set_visible(app->drag_area, !ops->hide_drag);

    app_rebuild_layout(app);
    update_page_button(app);

    /* 最大化/还原会改变 WM 侧装饰，切换布局后重新确认无边框 */
    apply_borderless(app);
}

/* 每次窗口尺寸变化都要同步重算输入区。
 * 关键：窗口 resize 后 GTK 会把 input shape 重置为整个窗口，
 * 只靠延时任务会被异步的 maximize 覆盖掉，导致穿透失效。 */
static gboolean on_window_size_allocate(GtkWidget *w, GdkRectangle *alloc,
                                         gpointer data) {
    (void)w; (void)alloc;
    App *app = data;
    if (app->layout && app->layout->transparent)
        layout_apply_input_region_now(app);
    return FALSE;
}

/* 窗口配置变化（最大化/还原/移动）之后也补一次 */
static gboolean on_window_configure(GtkWidget *w, GdkEventConfigure *ev,
                                     gpointer data) {
    (void)w; (void)ev;
    App *app = data;
    if (app->layout && app->layout->transparent)
        layout_apply_input_region_now(app);
    return FALSE;
}

/* 去掉系统标题栏与边框。
 *
 * 光靠 gtk_window_set_decorated(FALSE) 不够，有两层原因：
 *   1. GTK 自己会给 toplevel 绘制 "decoration" 节点的阴影/圆角边框，
 *      即使窗口已被设为无装饰 —— 这是「边框去不掉」的主因。
 *      对应处理在 CSS 里（见 app_css 的 decoration 规则）。
 *   2. 窗口最大化/全屏时，不少窗口管理器会强制补回装饰。
 *      这里在 X11 侧再清一次 WM 提示。
 * 两个时机都要调用：窗口创建后，以及 map 之后（WM 可能此时才响应）。 */
static void apply_borderless(App *app) {
    if (!app || !app->window) return;

    gtk_window_set_decorated(GTK_WINDOW(app->window), FALSE);

    GdkWindow *gw = gtk_widget_get_window(app->window);
    if (!gw) return;

    /* WM 侧：清掉所有装饰位，功能只保留移动与最小化 */
    gdk_window_set_decorations(gw, 0);
    gdk_window_set_functions(gw, GDK_FUNC_MOVE | GDK_FUNC_MINIMIZE);
}

/* map 之后再来一次：窗口管理器往往在 map 时才真正套用装饰 */
static gboolean on_window_map(GtkWidget *w, GdkEvent *ev, gpointer data) {
    (void)ev; (void)data;
    apply_borderless(&g_app);
    return FALSE;
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

/* 布局菜单项被点击：切换到对应布局 */
static void on_layout_menu_item(GtkMenuItem *item, gpointer data) {
    (void)data;
    const char *name = g_object_get_data(G_OBJECT(item), "osk-layout");
    if (!name) return;

    const LayoutOps *ops = layout_lookup(name);
    if (ops && ops != g_app.layout) {
        g_print("[OSK] 切换到布局：%s\n", ops->display_name);
        app_set_layout(&g_app, ops);
    }
}

/* 换页按钮：只在多页布局（标准、最简）下显示，第二页时变红 */
static void update_page_button(App *app) {
    if (!app || !app->page_btn) return;

    gint pages = (app->layout && app->layout->pages > 0)
                 ? app->layout->pages : 1;

    GtkWidget *w = key_button_widget(app->page_btn);

    /* 单页布局直接隐藏，不占顶栏空间 */
    gtk_widget_set_visible(w, pages >= 2);
    if (pages < 2)
        return;

    key_button_set_active_class(app->page_btn, "mod-locked", app->page == 1);
    key_button_set_label(app->page_btn, app->page == 1 ? "2/2" : "1/2");
}

static void on_page_clicked(const KeyDef *def, gboolean is_repeat,
                             gpointer user_data) {
    (void)def;
    if (is_repeat) return;

    App *app = user_data;
    gint pages = (app->layout && app->layout->pages > 0)
                 ? app->layout->pages : 1;
    if (pages < 2) return;

    app->page = (app->page + 1) % pages;
    app_rebuild_layout(app);
    update_page_button(app);
}

static GtkWidget *build_toolbar(App *app) {
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkStyleContext *bctx = gtk_widget_get_style_context(bar);
    gtk_style_context_add_class(bctx, "osk-toolbar");

    /* --- 左侧：布局选择（弹窗菜单） --- */
    static const char *const layout_names[] = {
        "standard", "minimal", "fullscreen", "touchpad", "game"
    };

    GtkWidget *menu = gtk_menu_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(menu),
                                "osk-menu");
    for (gsize i = 0; i < G_N_ELEMENTS(layout_names); ++i) {
        const LayoutOps *ops = layout_lookup(layout_names[i]);
        if (!ops) continue;

        GtkWidget *item = gtk_menu_item_new_with_label(ops->display_name);
        g_object_set_data(G_OBJECT(item), "osk-layout",
                          (gpointer)ops->name);
        g_signal_connect(item, "activate",
                         G_CALLBACK(on_layout_menu_item), NULL);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }
    gtk_widget_show_all(menu);

    /* GtkMenuButton 本身是 GtkButton 子类，会被 input region 正常收集；
     * 弹出菜单是独立 popup 窗口，不受 input region 限制。 */
    GtkWidget *layout_btn = gtk_menu_button_new();
    gtk_button_set_label(GTK_BUTTON(layout_btn), "布局 ▾");
    gtk_widget_set_can_focus(layout_btn, FALSE);
    gtk_widget_set_focus_on_click(layout_btn, FALSE);
    gtk_widget_set_tooltip_text(layout_btn, "选择布局");
    gtk_style_context_add_class(gtk_widget_get_style_context(layout_btn),
                                "osk-tool");
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(layout_btn), menu);
    gtk_box_pack_start(GTK_BOX(bar), layout_btn, FALSE, FALSE, 0);

    /* --- 中间：拖动区（只放一个移动符号，尽量省空间） --- */
    GtkWidget *drag_area = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(drag_area), FALSE);
    gtk_widget_set_hexpand(drag_area, TRUE);
    gtk_widget_add_events(drag_area, GDK_BUTTON_PRESS_MASK);
    gtk_style_context_add_class(gtk_widget_get_style_context(drag_area),
                                "osk-drag-area");
    gtk_widget_set_tooltip_text(drag_area, "按住拖动窗口");

    GtkWidget *grip = gtk_label_new("✥");
    gtk_widget_set_halign(grip, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(grip, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(grip),
                                "osk-grip");
    gtk_container_add(GTK_CONTAINER(drag_area), grip);
    g_signal_connect(drag_area, "button-press-event",
                     G_CALLBACK(on_drag_begin), app->window);
    gtk_box_pack_start(GTK_BOX(bar), drag_area, TRUE, TRUE, 0);
    app->drag_area = drag_area;

    /* --- 右侧：换页、home/end、设置、最小化 --- */
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);

    const KeyDef *page_def = keylib_get("page");
    if (page_def) {
        KeyButton *pb = key_button_new(page_def, on_page_clicked, NULL, app);
        key_button_set_fixed_size(pb, 34, 20);
        gtk_style_context_add_class(gtk_widget_get_style_context(
                                        key_button_widget(pb)), "osk-tool");
        gtk_widget_set_tooltip_text(key_button_widget(pb), "切换第二页");
        gtk_box_pack_start(GTK_BOX(right), key_button_widget(pb),
                           FALSE, FALSE, 0);
        app->page_btn = pb;
    }

    /* home / end：直接用键盘键定义，走正常发键流程 */
    static const char *const extra_keys[] = { "home", "end" };
    for (gsize i = 0; i < G_N_ELEMENTS(extra_keys); ++i) {
        const KeyDef *kd = keylib_get(extra_keys[i]);
        if (!kd) continue;
        KeyButton *kb = key_button_new(kd, app_on_key_activate,
                                       app_on_key_swipe, app);
        key_button_set_fixed_size(kb, 34, 20);
        gtk_style_context_add_class(gtk_widget_get_style_context(
                                        key_button_widget(kb)), "osk-tool");
        gtk_widget_set_tooltip_text(key_button_widget(kb), kd->label);
        gtk_box_pack_start(GTK_BOX(right), key_button_widget(kb),
                           FALSE, FALSE, 0);
    }

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
    /* 窗口背景透明：空白处直接透出桌面，只有按键和按钮有底色。
     * 需要 RGBA visual + app_paintable，main() 里已经设置。 */
    "window.osk-window {"
    "  background-color: transparent;"
    "  background-image: none;"
    "  padding: 0;"
    "  border: none;"
    "}"
    "window.osk-window > box {"
    "  padding: 0;"
    "  background-color: transparent;"
    "}"
    /* 关键：GTK3 给 toplevel 画了一层 decoration 节点（窗口阴影 + 圆角边框），
     * 即使 gtk_window_set_decorated(FALSE) 也照样画。这里把它整个抹掉，
     * 否则窗口四周会一直有一圈阴影/描边，看起来就像「边框去不掉」。 */
    ".decoration {"
    "  border: none;"
    "  border-radius: 0;"
    "  box-shadow: none;"
    "  margin: 0;"
    "  padding: 0;"
    "  background: none;"
    "}"
    ".decoration:backdrop {"
    "  border: none;"
    "  box-shadow: none;"
    "}"
    ".csd {"
    "  border: none;"
    "  border-radius: 0;"
    "  box-shadow: none;"
    "  margin: 0;"
    "}"
    /* 顶栏：背景透明，让空白处露出底色，按钮像浮在上面 */
    ".osk-toolbar {"
    "  background-color: transparent;"
    "  background-image: none;"
    "  padding: 3px 6px;"
    "  border: none;"
    "}"
    ".osk-title {"
    "  color: #8b929c;"
    "  font-size: 11px;"
    "}"
    /* 拖动区里的十字移动符号：低调，只在悬停时提亮 */
    ".osk-grip {"
    "  color: #5c636e;"
    "  font-size: 13px;"
    "}"
    ".osk-drag-area:hover .osk-grip {"
    "  color: #9aa2ae;"
    "}"
    /* 布局弹出菜单：和顶栏按钮同一套观感 */
    ".osk-menu {"
    "  background-color: #2b2f36;"
    "  border: 1px solid #454b54;"
    "  padding: 4px;"
    "}"
    ".osk-menu menuitem {"
    "  padding: 5px 14px;"
    "  border-radius: 5px;"
    "  color: #d6dae0;"
    "  font-size: 12px;"
    "}"
    ".osk-menu menuitem:hover {"
    "  background-color: #3f4652;"
    "}"
    /* 换页按钮在当前页不可用（非多页布局）时置灰 */
    ".osk-key.mod-disabled {"
    "  background-color: #2e333b;"
    "  border-color: #3a3f47;"
    "  color: #6b7280;"
    "}"
    ".osk-tool {"
    "  min-height: 20px;"
    "  min-width: 26px;"
    "  padding: 0 6px;"
    "  border: 1px solid #454b54;"
    "  border-radius: 5px;"
    "  background-image: none;"
    "  background-color: #343943;"
    "  color: #d6dae0;"
    "  font-size: 11px;"
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
    /* 顶栏里用 KeyButton 实现的控件（换页、home/end）也跟着缩小 */
    ".osk-tool.osk-key {"
    "  border-radius: 5px;"
    "}"
    ".osk-tool .osk-label-main {"
    "  font-size: 11px;"
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
    g_app.config.key_width   = 50;
    g_app.config.key_height  = 60;
    g_app.config.key_spacing = 1;
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

    /* 隐藏标题栏：无边框窗口，由自绘顶栏承担拖动与按钮 */
    gtk_window_set_decorated(GTK_WINDOW(g_app.window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(g_app.window), TRUE);

    g_signal_connect(g_app.window, "destroy",
                     G_CALLBACK(on_window_destroy), NULL);
    /* map 时再摘一次装饰，兼容会在显示时补边框的窗口管理器 */
    g_signal_connect(g_app.window, "map-event",
                     G_CALLBACK(on_window_map), NULL);
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
    /* 不要四周留白：键盘紧贴窗口边缘 */
    gtk_widget_set_margin_start(g_app.layout_host, 0);
    gtk_widget_set_margin_end(g_app.layout_host, 0);
    gtk_widget_set_margin_top(g_app.layout_host, 0);
    gtk_widget_set_margin_bottom(g_app.layout_host, 0);
    gtk_widget_set_hexpand(g_app.layout_host, TRUE);
    gtk_widget_set_vexpand(g_app.layout_host, TRUE);
    gtk_box_pack_start(GTK_BOX(outer), g_app.layout_host, TRUE, TRUE, 0);

    /* 加载默认布局 */
    app_set_layout(&g_app, layout_lookup("standard"));
    app_apply_config(&g_app);

    gtk_widget_show_all(g_app.window);
    /* show_all 之后换页按钮才真正显示，这时再同步一次它的状态 */
    update_page_button(&g_app);
    /* 显示后再确认一次无边框（此时 GDK 窗口已创建） */
    apply_borderless(&g_app);

    g_signal_connect(g_app.window, "size-allocate",
                  G_CALLBACK(on_window_size_allocate), &g_app);
    g_signal_connect(g_app.window, "configure-event",
                  G_CALLBACK(on_window_configure), &g_app);
    gtk_main();
    return 0;
}
