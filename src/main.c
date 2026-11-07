#include <gtk/gtk.h>

/* gdkx.h 只在 GTK 构建时启用了 X11 后端才存在。
 * 无条件 include 在纯 Wayland 构建上会直接编译失败。 */
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

#include "app.h"
#include "settings.h"
#include "layout/layout.h"
#include "layout/gamepad_layout.h"
#include "layout/gamepad_editor.h"
#include "widgets/touchpad.h"
#include "backend/uinput_mouse.h"

static App g_app = {0};

/* 前向声明：app_set_layout / 重生成窗口 里会用到 */
static void update_page_button(App *app);
static void apply_borderless(App *app);
static void on_window_destroy(GtkWidget *widget, gpointer user_data);
static void create_main_window(App *app);
static void show_main_window(App *app);

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

/* ===== resize 必须等 unmaximize 完成 =====
 *
 * 切出游戏布局时先调 gtk_window_unmaximize()，那是异步请求：
 * KWin 要经过一轮 configure 往返（通常还带还原动画）才真正还原。
 * 以前这里固定等 30ms 就 gtk_window_resize()，还原没完成时 resize
 * 会和还原在合成器侧竞争，还原几何可能被按错误锚点结算 ——
 * 窗口（顶栏所在的上边）有概率跑到屏幕外，无边框窗口顶栏够不着
 * 就再也没法拖动了。
 *
 * 所以 resize 前确认窗口已退出最大化：仍在最大化状态就继续等。
 * 等待有上限（40ms × 50 ≈ 2s），超时后退回旧行为照常 resize，
 * 避免 WM 异常时窗口永远停留在错误尺寸；真正还原完成时
 * on_window_state 还会再补一次 fit 兜底。 */
#define FIT_WAIT_MS        40
#define FIT_WAIT_MAX_TICKS 50

static guint g_fit_wait_ticks = 0;

/* ===== 重生成后的盯守（app_replace_placement）=====
 * 理论上 unrealize/realize 出来的新窗口不可能是最大化的，
 * 但实测某些环境里它仍被顶回全屏。盯守 1.5 秒：
 * 发现新窗口处于最大化（而当前是普通布局）就立刻纠正。 */
#define REPLACE_WATCH_MS    100
#define REPLACE_WATCH_TICKS 15    /* 100ms × 15 ≈ 1.5s */
#define REPLACE_FIX_MAX     5     /* 最多纠正次数，防止与 WM 死循环 */

static guint    g_replace_watch      = 0;
static gint     g_replace_watch_left = 0;
static gint     g_replace_fixes      = 0;
static gboolean g_replace_fixed      = FALSE;
static guint    g_replace_idle       = 0;

static void center_window_x11(App *app);
static gboolean replace_watch_tick(gpointer data);

static void fit_window_resize(App *app) {
    GtkWidget *content = gtk_bin_get_child(GTK_BIN(app->window));
    if (!content) return;

    GtkRequisition nat;
    gtk_widget_get_preferred_size(content, NULL, &nat);
    if (nat.width  < 1) nat.width  = 1;
    if (nat.height < 1) nat.height = 1;

    /* 尺寸已经一致就不必再请求一次 resize，减少与 WM 的往返 */
    gint cw = 0, ch = 0;
    gtk_window_get_size(GTK_WINDOW(app->window), &cw, &ch);
    if (cw == nat.width && ch == nat.height) return;

    g_print("[OSK] fit：窗口 %dx%d → %dx%d\n",
            cw, ch, nat.width, nat.height);
    gtk_window_resize(GTK_WINDOW(app->window), nat.width, nat.height);
}

static gboolean fit_window_idle(gpointer data) {
    App *app = data;
    g_fit_idle = 0;

    if (!app || !app->window || !app->layout) return G_SOURCE_REMOVE;
    if (app->layout->transparent) return G_SOURCE_REMOVE;

    /* WM 还没确认解除最大化：继续等，绝不能在这个状态下 resize */
    if (gtk_window_is_maximized(GTK_WINDOW(app->window)) &&
        ++g_fit_wait_ticks < FIT_WAIT_MAX_TICKS) {
        g_fit_idle = g_timeout_add(FIT_WAIT_MS, fit_window_idle, app);
        return G_SOURCE_REMOVE;
    }
    g_fit_wait_ticks = 0;

    fit_window_resize(app);
    return G_SOURCE_REMOVE;
}

static void schedule_fit(App *app) {
    if (!app || !app->layout) return;
    if (app->layout->transparent) return;   /* 穿透布局不跟着收缩 */
    if (g_fit_idle) g_source_remove(g_fit_idle);
    g_fit_wait_ticks = 0;
    /* 稍等一拍，让新布局完成分配后再量尺寸 */
    g_fit_idle = g_timeout_add(30, fit_window_idle, app);
}

/* ===== 从铺满布局（游戏）返回时重新放置窗口 =====
 *
 * 为什么不信任 unmaximize 的还原结果：
 *   Wayland 下客户端没有窗口定位权，unmaximize 还原到哪全由
 *   KWin 决定，实测有概率把窗口顶栏放到屏幕外 —— 无边框窗口
 *   顶栏够不着，用户就再也无法拖动了。
 *
 * 做法：干脆地换一个全新的 GtkWindow 对象。
 *
 *   藏起旧窗口 → （空闲时）建全新 GtkWindow → 装入当前布局
 *   → 调好尺寸 → show → X11 精确居中 → 销毁旧窗口
 *
 * 之前试过 hide+show、unrealize/realize，新窗口仍被顶回全屏：
 *   unrealize 能销毁 X11 的 X 窗口/_NET_WM_STATE、Wayland 的
 *   wl_surface/xdg_toplevel 和 GTK 后端缓存，但 **GtkWindow
 *   对象自身的 widget 级标志（maximize_initially 等）活了下来**，
 *   在这套环境里 show 时仍把窗口顶回最大化（连发五次
 *   unmaximize 都被压回去，日志可证）。所以这次连对象一起换：
 *   全新对象的一切标志天然是 FALSE，什么状态都带不过来。
 *
 * app_set_layout 在切出铺满布局时先把旧窗口藏掉并挂一个空闲
 * 回调（replace_idle），菜单信号栈完全退空后才真正重建，
 * 避免「全屏窗口里装着新布局」的中间态。
 *
 * show 之后仍有 1.5 秒盯守（replace_watch_tick）：万一还有
 * 什么东西把新窗口顶回最大化（比如 KWin 窗口规则），立刻纠正；
 * 反复纠正无效时打日志提示用户检查窗口规则。 */

/* 空闲回调：菜单/按钮的信号栈退空后再动窗口 */
static gboolean replace_idle(gpointer data) {
    App *app = data;
    GtkWidget *old;

    g_replace_idle = 0;
    if (!app || !app->window) return G_SOURCE_REMOVE;

    old = app->window;
    g_print("[OSK] 重生成窗口：旧窗口 maximized=%d\n",
            gtk_window_is_maximized(GTK_WINDOW(old)) ? 1 : 0);

    /* 全新 GtkWindow 对象：app->window / 顶栏 / 布局挂载点
     * 全部换成新的（回调里捕获的窗口指针随 build_toolbar 重建） */
    create_main_window(app);

    /* 当前布局装进新窗口 */
    app_rebuild_layout(app);
    show_main_window(app);

    /* 立刻按内容定好尺寸，不等 30ms 定时器 */
    if (g_fit_idle) {
        g_source_remove(g_fit_idle);
        g_fit_idle = 0;
    }
    g_fit_wait_ticks = 0;
    fit_window_resize(app);

    /* 旧窗口退场：摘掉「销毁即退出程序」的处理器再销毁 */
    g_signal_handlers_disconnect_by_func(old,
                                         G_CALLBACK(on_window_destroy),
                                         NULL);
    gtk_widget_destroy(old);

    {
        gint cw = 0, ch = 0;
        gtk_window_get_size(GTK_WINDOW(app->window), &cw, &ch);
        g_print("[OSK] 重生成窗口：新窗口 size=%dx%d maximized=%d\n",
                cw, ch,
                gtk_window_is_maximized(GTK_WINDOW(app->window)) ? 1 : 0);
    }

    /* 启动盯守：1.5 秒内发现被顶回最大化就立刻纠正 */
    g_replace_fixes      = 0;
    g_replace_fixed      = FALSE;
    g_replace_watch_left = REPLACE_WATCH_TICKS;
    if (g_replace_watch)
        g_source_remove(g_replace_watch);
    g_replace_watch = g_timeout_add(REPLACE_WATCH_MS,
                                    replace_watch_tick, app);

    center_window_x11(app);
    return G_SOURCE_REMOVE;
}

/* 对外入口：切出铺满布局时调用。推迟到空闲执行，
 * 让当前的菜单/按钮信号处理完全走完。 */
static void app_replace_placement(App *app) {
    if (!app || !app->window) return;
    if (g_replace_idle)
        g_source_remove(g_replace_idle);
    g_replace_idle = g_idle_add(replace_idle, app);
}
static void center_window_x11(App *app) {
#ifdef GDK_WINDOWING_X11
    GdkWindow *gw;
    GdkDisplay *display;
    GdkMonitor *monitor;
    GdkRectangle wa;
    gint w = 0, h = 0, x, y;

    if (!app || !app->window) return;
    gw = gtk_widget_get_window(app->window);
    if (!gw || !GDK_IS_X11_WINDOW(gw)) return;

    display = gtk_widget_get_display(app->window);
    monitor = gdk_display_get_monitor_at_window(display, gw);
    gdk_monitor_get_workarea(monitor, &wa);

    gtk_window_get_size(GTK_WINDOW(app->window), &w, &h);
    x = wa.x + (wa.width  - w) / 2;
    y = wa.y + (wa.height - h) / 2;
    if (x < wa.x) x = wa.x;
    if (y < wa.y) y = wa.y;
    gtk_window_move(GTK_WINDOW(app->window), x, y);
#else
    (void)app;
#endif
}

/* 盯守：发现新窗口仍（或又被）最大化就立刻纠正 */
static gboolean replace_watch_tick(gpointer data) {
    App *app = data;

    if (!app || !app->window || !app->layout || app->layout->transparent) {
        g_replace_watch = 0;
        return G_SOURCE_REMOVE;
    }

    if (gtk_window_is_maximized(GTK_WINDOW(app->window))) {
        if (g_replace_fixes < REPLACE_FIX_MAX) {
            g_replace_fixes++;
            g_replace_fixed = TRUE;
            g_print("[OSK] 重生成后窗口仍是最大化（第 %d 次纠正）\n",
                    g_replace_fixes);
            gtk_window_unmaximize(GTK_WINDOW(app->window));
        } else {
            g_print("[OSK] 窗口反复回到最大化，放弃纠正 ——"
                    "请检查 KWin 窗口规则是否对该窗口强制最大化\n");
            g_replace_watch = 0;
            return G_SOURCE_REMOVE;
        }
    } else if (g_replace_fixed) {
        /* 纠正生效：unmaximize 会还原到 KWin 自己的恢复位置，
         * X11 下重新居中一次（Wayland 下是空操作） */
        g_replace_fixed = FALSE;
        center_window_x11(app);
        g_print("[OSK] 窗口已纠正回普通尺寸\n");
    }

    if (--g_replace_watch_left <= 0) {
        g_replace_watch = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
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

    /* --- 窗口属性按布局切换 ---
     *
     * 注意：Wayland 下没有 DOCK / UTILITY 这些 X11 概念，
     * set_type_hint 会被 GTK 映射成近似的 xdg_toplevel 角色，
     * 而某些角色会让 KWin 强制加服务端装饰。
     *
     * 所以这里只在需要时调 maximize / keep_above，
     * 不再改 type_hint —— 窗口的「无装饰」属性在建窗时就定好了，
     * 运行中再改角色反而可能把装饰带回来。 */
    gboolean passthrough = ops->transparent;
    /* 从铺满（最大化）布局切回普通布局：WM 的还原几何不可信，
     * 整个窗口换成全新的 GtkWindow 对象（见 app_replace_placement）。 */
    gboolean leaving_fullscreen = (app->layout && app->layout->transparent &&
                                   !passthrough);
    if (leaving_fullscreen) {
        /* 全屏旧窗口马上就要整个换掉：先藏起来，别让用户看到
         * 「全屏窗口里装着新布局」的中间态。新窗口在空闲回调
         * 里以全新 GtkWindow 对象重建。 */
        gtk_widget_hide(app->window);
        app_replace_placement(app);
    }
    if (passthrough) {
        /* 切入铺满布局：挂起的重生成与盯守不再需要 */
        if (g_replace_idle) {
            g_source_remove(g_replace_idle);
            g_replace_idle = 0;
        }
        if (g_replace_watch) {
            g_source_remove(g_replace_watch);
            g_replace_watch = 0;
        }
        gtk_window_set_skip_taskbar_hint(GTK_WINDOW(app->window), TRUE);
        gtk_window_set_keep_above(GTK_WINDOW(app->window), TRUE);
        gtk_window_maximize(GTK_WINDOW(app->window));
    } else {
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

    /* 切布局前先把游戏布局里按住的东西放掉。
     * 否则从游戏布局切到标准布局时，那些键在系统侧仍处于按下状态，
     * 表现为「切回来之后一直有个键卡着」。切出时是无害的空操作。 */
    layout_game_release_all(app);

    app->layout = ops;
    app->page   = 0;          /* 切布局时回到第一页 */

    /* 全屏/游戏这类铺满屏幕的布局不需要拖动区 */
    if (app->drag_area)
        gtk_widget_set_visible(app->drag_area, !ops->hide_drag);

    /* 游戏布局编辑按钮只在游戏布局下出现 */
    if (app->game_edit_btn)
        gtk_widget_set_visible(app->game_edit_btn,
                               ops == layout_game_ops());

    /* 顶栏排列方式。
     *
     * 普通布局：「左按钮 + 弹性拖动区 + 右侧按钮组」，拖动区占满中间，
     *           所以右侧按钮自然贴右 —— 这时要拖窗口，不能没有它。
     * 游戏布局：铺满屏幕不需要拖动窗口，隐藏拖动区，
     *           让「布局切换 + 按钮组」整体居中。
     *
     * 注意：布局切换按钮在任何布局下都必须可见 ——
     * 否则切进游戏布局就出不来了。 */
    gboolean game_mode = (ops == layout_game_ops());

    if (app->toolbar) {
        gtk_widget_set_halign(app->toolbar,
                               game_mode ? GTK_ALIGN_CENTER
                                         : GTK_ALIGN_FILL);
        gtk_widget_set_hexpand(app->toolbar, !game_mode);
    }

    /* 拖动区虽然被 hide_drag 隐藏，但它仍占着 hexpand。
     * 缩到 0 宽，这样剩下的按钮才能真的居中。 */
    if (app->drag_area) {
        gtk_widget_set_hexpand(app->drag_area, !game_mode);
        gtk_widget_set_size_request(app->drag_area,
                                     game_mode ? 0 : -1, -1);
    }

    /* home/end 在游戏布局下没意义，隐藏以腾出空间 */
    if (app->home_end_box)
        gtk_widget_set_visible(app->home_end_box, !game_mode);

    /* 鼠标按钮同理：游戏布局下不需要触控板 */
    if (app->mouse_btn)
        gtk_widget_set_visible(app->mouse_btn, !game_mode);

    /* 切到游戏布局时把触控板弹窗关掉 —— 它置顶且不抢焦点，
     * 留在屏幕上会挡住游戏画面。 */
    if (game_mode && app->mouse_panel)
        gtk_widget_destroy(app->mouse_panel);

    app_rebuild_layout(app);
    update_page_button(app);

    /* 最大化/还原会改变 WM 侧装饰，切换布局后重新确认无边框 */
    apply_borderless(app);
}

/* 对外入口：切换布局。编辑面板与设置面板都通过它切，
 * 避免各自复制一份窗口属性设置逻辑。 */
void app_switch_layout(App *app, const LayoutOps *ops) {
    if (!app || !ops || ops == app->layout) return;
    g_print("[OSK] 切换到布局：%s\n", ops->display_name);
    app_set_layout(app, ops);
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

/* 窗口状态变化：退出最大化被 WM 确认后再补一次 fit。
 *
 * 正常流程下重生成（app_replace_placement）已经把窗口收拾好了，
 * 这条是兜底：万一窗口被 KWin 规则等外因最大化/还原，
 * 真正退出最大化时把窗口重新收缩到键盘尺寸。 */
static gboolean on_window_state(GtkWidget *w, GdkEventWindowState *ev,
                                gpointer data) {
    (void)w;
    App *app = data;
    if (!app) return FALSE;
    if (!(ev->changed_mask & GDK_WINDOW_STATE_MAXIMIZED)) return FALSE;

    g_print("[OSK] 窗口状态：MAXIMIZED %s\n",
            (ev->new_window_state & GDK_WINDOW_STATE_MAXIMIZED)
                ? "置位" : "清除");

    if (ev->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) return FALSE;
    if (app->layout && !app->layout->transparent)
        schedule_fit(app);
    return FALSE;
}

/* 确保窗口保持无边框。
 *
 * ===== KWin 的特殊行为 =====
 *
 * KWin 对 GTK 应用优先强制服务端装饰（SSD），并忽略应用发出的
 * 「不要装饰」请求（KDE Bug 484800）。所以单靠
 * gtk_window_set_decorated(FALSE) 在 Wayland + KWin 下无效。
 *
 * 真正起作用的是 app_force_csd()：给窗口挂一个空 titlebar，
 * 逼 GTK 走 CSD 路径，KWin 才会放手。
 *
 * 本函数用于 map 之后、切换布局之后再次确认状态 ——
 * 因为 maximize/unmaximize 会让某些合成器重新评估装饰。
 * 注意这里也要走 app_force_csd，否则会把 CSD 状态改回 SSD。 */
static void apply_borderless(App *app) {
    if (!app || !app->window) return;

    /* 重复调用是安全的：titlebar 已存在时 set_titlebar 会替换它，
     * 仍然保持 CSD 模式。 */
    app_force_csd(GTK_WINDOW(app->window));

    GdkWindow *gw = gtk_widget_get_window(app->window);
    if (!gw) return;

    /* X11 侧再清一次 WM 提示。Wayland 下这几个调用是空操作，
     * 保留是为了兼容 XWayland/X11 会话（程序可能在那里运行）。
     *
     * 用 GDK_WINDOWING_X11 做编译期保护：如果 GTK 构建时没启用
     * X11 后端，gdkx.h 和 GDK_IS_X11_WINDOW 都不存在，
     * 无条件使用会直接编译失败。 */
#ifdef GDK_WINDOWING_X11
    if (GDK_IS_X11_WINDOW(gw)) {
        gdk_window_set_decorations(gw, 0);
        gdk_window_set_functions(gw, GDK_FUNC_MOVE | GDK_FUNC_MINIMIZE);
    }
#else
    (void)gw;
#endif
}

/* map 之后再来一次：窗口管理器往往在 map 时才真正套用装饰 */
static gboolean on_window_map(GtkWidget *w, GdkEvent *ev, gpointer data) {
    (void)w; (void)ev; (void)data;
    apply_borderless(&g_app);
    return FALSE;
}

/* ===== 顶栏按钮 ===== */

static void on_settings_clicked(GtkButton *btn, gpointer data) {
    (void)btn;
    settings_open((App *)data);
}

/* ===== 触控板弹窗 =====
 *
 * 以前触控板是一个独立布局，但切布局会打断键盘输入 ——
 * 而实际使用中「边打字边点鼠标」是常态。改成弹窗后两者可以并存。
 *
 * 窗口特性：
 *   - 无系统装饰 + 自绘白边（和主窗口一致）
 *   - 置顶，不抢焦点
 *   - 关闭时清掉 app 上的句柄，再次点击按钮会重新创建
 */

/* 鼠标按键的上下文：哪个设备、哪个键。
 * 原来定义在 layout_touchpad.c 里，那个文件删掉后搬到这里。 */
typedef struct {
    OskMouse *mouse;
    guint     button;
} MouseBtnCtx;

static void on_mouse_btn_pressed(GtkButton *btn, gpointer data) {
    (void)btn;
    MouseBtnCtx *ctx = data;
    if (ctx && ctx->mouse)
        osk_mouse_button(ctx->mouse, ctx->button, TRUE);
}

static void on_mouse_btn_released(GtkButton *btn, gpointer data) {
    (void)btn;
    MouseBtnCtx *ctx = data;
    if (ctx && ctx->mouse)
        osk_mouse_button(ctx->mouse, ctx->button, FALSE);
}

static void on_mouse_panel_destroy(GtkWidget *w, gpointer data) {
    App *app = data;
    if (app) app->mouse_panel = NULL;
    (void)w;
}

static void on_mouse_panel_clicked(GtkButton *btn, gpointer data) {
    (void)btn;
    App *app = data;
    if (!app) return;

    /* 已打开就提到前面，不重复创建 */
    if (app->mouse_panel) {
        gtk_window_present(GTK_WINDOW(app->mouse_panel));
        return;
    }

    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    app->mouse_panel = win;

    gtk_window_set_title(GTK_WINDOW(win), "触控板");
    gtk_window_set_default_size(GTK_WINDOW(win), 420, 300);
    gtk_window_set_keep_above(GTK_WINDOW(win), TRUE);

    /* 不抢焦点：点触控板时不能把当前应用的输入焦点弄丢 */
    gtk_window_set_accept_focus(GTK_WINDOW(win), FALSE);
    gtk_window_set_focus_on_map(GTK_WINDOW(win), FALSE);

    app_setup_subwindow(win);
    g_signal_connect(win, "destroy",
                     G_CALLBACK(on_mouse_panel_destroy), app);

    /* 统一外壳：标题栏（可拖动）+ 关闭按钮 */
    GtkWidget *content = app_subwindow_shell(win, "触控板");

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 6);
    gtk_box_pack_start(GTK_BOX(content), box, TRUE, TRUE, 0);

    /* 触控板区域 */
    TouchpadWidget *tw = touchpad_widget_new(app->mouse, app);
    GtkWidget *area = touchpad_widget_gtk(tw);
    gtk_widget_set_size_request(area, -1, 200);
    gtk_box_pack_start(GTK_BOX(box), area, TRUE, TRUE, 0);

    /* 左中右三键：按住持续生效，所以用 pressed/released 而不是 clicked */
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    struct { const char *label; guint code; } btns[] = {
        { "左键", BTN_LEFT   },
        { "中键", BTN_MIDDLE },
        { "右键", BTN_RIGHT  },
    };

    for (gsize i = 0; i < G_N_ELEMENTS(btns); ++i) {
        GtkWidget *b = gtk_button_new_with_label(btns[i].label);
        gtk_widget_set_can_focus(b, FALSE);
        gtk_widget_set_focus_on_click(b, FALSE);
        gtk_widget_set_size_request(b, -1, 42);
        gtk_style_context_add_class(gtk_widget_get_style_context(b), "osk-tool");

        MouseBtnCtx *ctx = g_new0(MouseBtnCtx, 1);
        ctx->mouse  = app->mouse;
        ctx->button = btns[i].code;
        g_object_set_data_full(G_OBJECT(b), "osk-mbtnctx", ctx, g_free);

        g_signal_connect(b, "pressed",
                         G_CALLBACK(on_mouse_btn_pressed),  ctx);
        g_signal_connect(b, "released",
                         G_CALLBACK(on_mouse_btn_released), ctx);

        gtk_box_pack_start(GTK_BOX(row), b, TRUE, TRUE, 0);
    }
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);

    /* 内容就位后再挂白边 */
    app_finish_subwindow(win);

    /* 应用用户配置的触控板透明度 */
    app_apply_pad_config(app);

    gtk_widget_show_all(win);
}

/* 游戏布局编辑按钮：直接切换编辑模式，不弹面板。
 *
 *   点一下 → 进入编辑模式（按钮变成锁定外观）
 *   再点一下 → 退出编辑模式并自动保存
 *
 * 编辑模式下的增删改都在画布上完成（见 layout_game.c 的捕获层），
 * 所以这里只负责开关状态和视觉反馈。 */
static void on_gamepad_edit_clicked(GtkButton *btn, gpointer data) {
    App *app = data;
    GpState *st = app ? app->gp : NULL;
    if (!app || !st) return;

    gboolean entering = !st->editing;

    /* 锁定外观：进入时高亮，退出时恢复 */
    GtkStyleContext *ctx = gtk_widget_get_style_context(GTK_WIDGET(btn));
    if (entering)
        gtk_style_context_add_class(ctx, "osk-locked");
    else
        gtk_style_context_remove_class(ctx, "osk-locked");

    if (!entering) {
        /* 退出编辑：先把所有按住的键放掉再存盘 */
        layout_game_release_all(app);
        gamepad_save();
        g_print("[OSK] 退出编辑模式，布局已保存\n");
    } else {
        g_print("[OSK] 进入编辑模式\n");
    }

    gamepad_set_editing(app, entering);
}

/* 最小化按钮：
 *   单击 → 最小化
 *   长按 → 关闭键盘（退出程序）
 * 注意 GTK3 的 GtkGestureLongPress 只有两个信号：pressed 和 cancelled，
 * 其中 pressed 是「长按被识别」时发出（不是初次按下），没有 long-pressed。 */
static gboolean g_min_long_fired = FALSE;

/* 长按被识别 */
static void on_minimize_long_pressed(GtkGestureLongPress *g, gdouble x,
                                      gdouble y, gpointer data) {
    (void)g; (void)x; (void)y;
    g_min_long_fired = TRUE;
    g_print("[OSK] 长按最小化键 → 关闭键盘\n");
    gtk_window_close(GTK_WINDOW(data));
}

/* 真正按下时复位标志，保证下一次单击仍然是「最小化」。
 * 返回 FALSE 不拦截事件，GtkButton 自己还要用它。 */
static gboolean on_minimize_button_press(GtkWidget *w, GdkEventButton *ev,
                                          gpointer data) {
    (void)w; (void)ev; (void)data;
    g_min_long_fired = FALSE;
    return FALSE;
}

static void on_minimize_clicked(GtkButton *btn, gpointer data) {
    (void)btn;
    /* 长按已经触发了关窗，这次 clicked 是长按产生的，忽略 */
    if (g_min_long_fired) {
        g_min_long_fired = FALSE;
        return;
    }
    gtk_window_iconify(GTK_WINDOW(data));
}

static void attach_long_press(GtkWidget *widget, gpointer window) {
    GtkGesture *lp = gtk_gesture_long_press_new(widget);
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(lp), GDK_BUTTON_PRIMARY);
    g_signal_connect(lp, "pressed",
                     G_CALLBACK(on_minimize_long_pressed), window);
    g_object_set_data_full(G_OBJECT(widget), "osk-longpress",
                           lp, g_object_unref);

    /* 复位标志：按钮按下时就把上一次的长按状态清掉 */
    gtk_widget_add_events(widget, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(widget, "button-press-event",
                     G_CALLBACK(on_minimize_button_press), NULL);
}

/* ===== 窗口销毁 ===== */

static void on_window_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget; (void)user_data;

    /* 先把还按着的键和摇杆放掉，再销毁设备 */
    layout_game_release_all(&g_app);
    gamepad_state_free(&g_app);

    osk_gamepad_free(g_app.gamepad);
    g_app.gamepad = NULL;
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
        "standard", "minimal", "game"
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
     * 弹出菜单是独立 popup 窗口，不受 input region 限制。
     * 用圆形循环箭头表示「切换布局」，比文字省空间。 */
    GtkWidget *layout_btn = gtk_menu_button_new();
    gtk_button_set_label(GTK_BUTTON(layout_btn), "⟳");
    gtk_widget_set_can_focus(layout_btn, FALSE);
    gtk_widget_set_focus_on_click(layout_btn, FALSE);
    gtk_widget_set_tooltip_text(layout_btn, "切换布局");
    gtk_style_context_add_class(gtk_widget_get_style_context(layout_btn),
                                "osk-tool");
    gtk_style_context_add_class(gtk_widget_get_style_context(layout_btn),
                                "osk-cycle");
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(layout_btn), menu);
    gtk_box_pack_start(GTK_BOX(bar), layout_btn, FALSE, FALSE, 0);

    /* --- 鼠标按钮：弹出触控板子窗口 ---
     *
     * 以前「触控板」是一个独立布局，但它和键盘并不冲突 ——
     * 用户往往想一边打字一边点鼠标，切布局会打断这个流程。
     * 改成弹窗后两件事可以同时做。 */
    GtkWidget *mouse_btn = gtk_button_new_with_label("🖱");
    gtk_widget_set_can_focus(mouse_btn, FALSE);
    gtk_widget_set_focus_on_click(mouse_btn, FALSE);
    gtk_widget_set_tooltip_text(mouse_btn, "触控板");
    gtk_style_context_add_class(gtk_widget_get_style_context(mouse_btn),
                                "osk-tool");
    gtk_style_context_add_class(gtk_widget_get_style_context(mouse_btn),
                                "osk-cycle");
    g_signal_connect(mouse_btn, "clicked",
                     G_CALLBACK(on_mouse_panel_clicked), app);
    gtk_box_pack_start(GTK_BOX(bar), mouse_btn, FALSE, FALSE, 0);
    app->mouse_btn = mouse_btn;

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

    /* home / end：直接用键盘键定义，走正常发键流程。
     *
     * 用一个子容器包起来，方便整体控制可见性 ——
     * 游戏布局下这两个键没用（游戏不认 Home/End），
     * 隐藏它们能给编辑按钮腾出空间。 */
    GtkWidget *home_end_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
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
        gtk_box_pack_start(GTK_BOX(home_end_box), key_button_widget(kb),
                           FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(right), home_end_box, FALSE, FALSE, 0);
    app->home_end_box = home_end_box;

    GtkWidget *settings = gtk_button_new_with_label("⚙");
    gtk_widget_set_can_focus(settings, FALSE);
    gtk_widget_set_focus_on_click(settings, FALSE);
    gtk_widget_set_tooltip_text(settings, "设置");
    gtk_style_context_add_class(gtk_widget_get_style_context(settings),
                                "osk-tool");
    g_signal_connect(settings, "clicked",
                     G_CALLBACK(on_settings_clicked), app);
    gtk_box_pack_start(GTK_BOX(right), settings, FALSE, FALSE, 0);

    /* 游戏布局编辑：只在游戏布局下显示，避免顶栏常驻一个无关按钮 */
    GtkWidget *game_edit = gtk_button_new_with_label("✎");
    gtk_widget_set_can_focus(game_edit, FALSE);
    gtk_widget_set_focus_on_click(game_edit, FALSE);
    gtk_widget_set_tooltip_text(game_edit, "编辑游戏布局");
    gtk_style_context_add_class(gtk_widget_get_style_context(game_edit),
                                "osk-tool");
    g_signal_connect(game_edit, "clicked",
                     G_CALLBACK(on_gamepad_edit_clicked), app);
    gtk_box_pack_start(GTK_BOX(right), game_edit, FALSE, FALSE, 0);
    app->game_edit_btn = game_edit;

    GtkWidget *minimize = gtk_button_new_with_label("—");
    gtk_widget_set_can_focus(minimize, FALSE);
    gtk_widget_set_focus_on_click(minimize, FALSE);
    gtk_widget_set_tooltip_text(minimize, "单击最小化，长按关闭键盘");
    gtk_style_context_add_class(gtk_widget_get_style_context(minimize),
                                "osk-tool");
    g_signal_connect(minimize, "clicked",
                     G_CALLBACK(on_minimize_clicked), app->window);
    /* 长按 0.6 秒关闭键盘 */
    attach_long_press(minimize, app->window);
    gtk_box_pack_start(GTK_BOX(right), minimize, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(bar), right, FALSE, FALSE, 0);

    /* 保存引用，供布局切换时调整居中方式 */
    app->toolbar = bar;
    app->toolbar_left = layout_btn;
    app->toolbar_right = right;

    return bar;
}

/* ===== 主窗口构建 =====
 *
 * 从 main() 拆出来：切出游戏布局「重生成窗口」时也用它建一个
 * 全新的 GtkWindow 对象 —— 对象自身的 widget 级标志
 * （maximize_initially 等）是之前反复复活成最大化的元凶，
 * 全新对象天然全 FALSE，任何历史状态都带不过来。 */
static void create_main_window(App *app) {
    GtkWidget *win;
    GdkScreen *screen;
    GdkVisual *visual;
    GtkWidget *outer;
    GtkWidget *toolbar;

    win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "OSK 屏幕键盘");
    gtk_window_set_default_size(GTK_WINDOW(win), 800, 380);
    gtk_window_set_keep_above(GTK_WINDOW(win), TRUE);

    /* 隐藏标题栏。
     *
     * 关键：必须用 app_force_csd() 而不是直接 set_decorated(FALSE)。
     *
     * KWin 对 GTK 应用会强制服务端装饰（SSD），无视「不要装饰」的
     * 请求（KDE Bug 484800）。给窗口设一个空 titlebar 后，GTK 会走
     * CSD 路径，合成器放手，此时 set_decorated(FALSE) 才真正生效。
     *
     * 另外不设 type_hint：Wayland 下 DOCK/UTILITY 这些 X11 概念
     * 没有直接对应，GTK 会挑一个近似的角色，反而可能让合成器
     * 加上服务端装饰。保持默认的普通 toplevel 最稳。 */
    app_force_csd(GTK_WINDOW(win));
    gtk_window_set_resizable(GTK_WINDOW(win), TRUE);

    g_signal_connect(win, "destroy",
                     G_CALLBACK(on_window_destroy), NULL);
    /* map 时再摘一次装饰，兼容会在显示时补边框的窗口管理器 */
    g_signal_connect(win, "map-event",
                     G_CALLBACK(on_window_map), NULL);
    g_signal_connect(win, "size-allocate",
                     G_CALLBACK(on_window_size_allocate), app);
    g_signal_connect(win, "configure-event",
                     G_CALLBACK(on_window_configure), app);
    g_signal_connect(win, "window-state-event",
                     G_CALLBACK(on_window_state), app);

    screen = gtk_widget_get_screen(win);
    visual = gdk_screen_get_rgba_visual(screen);
    if (visual && gdk_screen_is_composited(screen))
        gtk_widget_set_visual(win, visual);
    gtk_widget_set_app_paintable(win, TRUE);

    gtk_style_context_add_class(gtk_widget_get_style_context(win),
                                "osk-window");

    /* 必须先换成新窗口指针再建顶栏：build_toolbar 会把
     * app->window 捕获进拖动区/最小化按钮的回调数据 */
    app->window = win;

    /* 外层容器 + 布局挂载点 */
    outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(win), outer);

    /* 白边用 cairo 自绘（CSS 方案会影响弹出菜单，见 app_attach_border）。
     * 必须在外层容器建好之后调用 —— 它挂到子控件上，
     * 子控件不存在时会退回到窗口本身，那样在 CSD 模式下画不出来。 */
    app_attach_border(win);

    toolbar = build_toolbar(app);
    gtk_box_pack_start(GTK_BOX(outer), toolbar, FALSE, FALSE, 0);
    /* 顶栏与白边错开 1px，和下面的按键区保持一致 */
    gtk_widget_set_margin_start(toolbar, 1);
    gtk_widget_set_margin_end(toolbar, 1);
    gtk_widget_set_margin_top(toolbar, 1);

    app->layout_host = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    /* 按键与白边之间留 1px：白边是画在 outer 外沿的描边，
     * 内容整体内缩 1px 就不会压到线上。 */
    gtk_widget_set_margin_start(app->layout_host, 1);
    gtk_widget_set_margin_end(app->layout_host, 1);
    gtk_widget_set_margin_top(app->layout_host, 0);
    gtk_widget_set_margin_bottom(app->layout_host, 1);
    gtk_widget_set_hexpand(app->layout_host, TRUE);
    gtk_widget_set_vexpand(app->layout_host, TRUE);
    gtk_box_pack_start(GTK_BOX(outer), app->layout_host, TRUE, TRUE, 0);
}

/* 显示主窗口并同步一次窗口相关状态（布局切换重建后同样走这里） */
static void show_main_window(App *app) {
    if (!app || !app->window) return;

    gtk_widget_show_all(app->window);
    /* show_all 之后换页按钮才真正显示，这时再同步一次它的状态 */
    update_page_button(app);
    /* 显示后再确认一次无边框（此时 GDK 窗口已创建） */
    apply_borderless(app);
    /* 透明度等外观配置是 widget 级的，新窗口要重新应用一次 */
    app_apply_config(app);
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
    /* ===== 子窗口 =====
     * 所有子窗口（设置、触控板、编辑面板、对话框）共用一套外壳，
     * 保证观感与主窗口一致：无系统装饰 + 自绘白边 + 标题栏。 */
    "window.osk-subwindow {"
    "  background-color: transparent;"
    "}"
    /* 外壳：白边由 cairo 画在它的外沿，这里只给半透明底色，
     * 让内容和桌面区分开。 */
    ".osk-subwin-shell {"
    "  background-color: rgba(30, 33, 39, 0.96);"
    "}"
    /* 标题栏：和主窗口顶栏同一套观感 */
    ".osk-subwin-bar {"
    "  background-color: rgba(52, 57, 67, 0.55);"
    "  padding: 4px 6px;"
    "  border-bottom: 1px solid rgba(255,255,255,0.08);"
    "}"
    ".osk-subwin-title {"
    "  color: #d6dae0;"
    "  font-size: 12px;"
    "}"
    ".osk-subwin-content {"
    "  background-color: transparent;"
    "}"
    /* 关闭按钮：窄一点，和标题栏高度协调 */
    ".osk-tool.osk-subwin-close {"
    "  min-width: 26px;"
    "  min-height: 24px;"
    "  padding: 0;"
    "  font-size: 13px;"
    "}"
    /* 把 GTK 的 decoration 节点整个抹平：不要阴影、不要圆角、不要边框。
     *
     * 注意这里只能设成 none —— 之前试过在这里画白边，结果
     * GTK 的弹出菜单（GtkMenu 也是带 decoration 的 toplevel）
     * 跟着丢了背景，变成透明。
     *
     * 白边改为在代码里用 cairo 自绘（见 attach_border_draw），
     * 只画在真正需要的窗口上，不影响菜单。 */
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
    "  padding: 4px 7px;"
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
    "  min-height: 30px;"
    "  min-width: 38px;"
    "  padding: 0 10px;"
    "  border: 1px solid rgba(255,255,255,0.14);"
    "  border-radius: 7px;"
    "  background-image: linear-gradient(to bottom,"
    "    rgba(255,255,255,0.07) 0%,"
    "    rgba(0,0,0,0.04) 100%);"
    "  background-color: rgba(52, 57, 67, 0.62);"
    "  color: #e8ecf2;"
    "  font-size: 15px;"
    "  text-shadow: none;"
    "  box-shadow: inset 0 1px 0 rgba(255,255,255,0.10),"
    "              0 1px 2px rgba(0,0,0,0.30);"
    "  transition: background-color 80ms ease, border-color 80ms ease;"
    "}"
    ".osk-tool:hover {"
    "  background-color: rgba(78, 87, 101, 0.80);"
    "  border-color: rgba(255,255,255,0.30);"
    "}"
    ".osk-tool:active {"
    "  background-image: none;"
    "  background-color: rgba(36, 41, 50, 0.90);"
    "  box-shadow: inset 0 2px 3px rgba(0,0,0,0.40);"
    "}"
    /* 顶栏里用 KeyButton 实现的控件（换页、home/end）也跟着缩小 */
    ".osk-tool.osk-key {"
    "  border-radius: 5px;"
    "}"
    ".osk-tool .osk-label-main {"
    "  font-size: 11px;"
    "}"
    /* 布局切换按钮：圆形循环箭头。
     * 布局切换（⟳）和鼠标/触控板（🖱）两个按钮共用这个类，
     * 尺寸与其他顶栏按钮（min-height 30px）对齐。 */
    ".osk-tool.osk-cycle {"
    "  min-width: 30px;"
    "  min-height: 30px;"
    "  padding: 0;"
    "  border-radius: 15px;"
    "  font-size: 16px;"
    "}"
    /* 按键：固定尺寸、圆角、立体感。
     *
     * 视觉层次靠三层叠加：
     *   1. 渐变背景 —— 顶部略亮，模拟受光面
     *   2. 1px 高光边 —— 顶部一条浅色内阴影，做出「凸起」错觉
     *   3. 底部投影 —— 让按键像浮在键盘面上
     */
    ".osk-key {"
    "  border: 1px solid rgba(255,255,255,0.10);"
    "  border-radius: 8px;"
    "  background-image: linear-gradient(to bottom,"
    "    rgba(255,255,255,0.08) 0%,"
    "    rgba(255,255,255,0.02) 45%,"
    "    rgba(0,0,0,0.06) 100%);"
    "  background-color: #3d444f;"
    "  color: #eef1f5;"
    "  box-shadow: inset 0 1px 0 rgba(255,255,255,0.13),"
    "              0 2px 3px rgba(0,0,0,0.38);"
    "  transition: background-color 80ms ease,"
    "              box-shadow 80ms ease,"
    "              border-color 80ms ease;"
    "}"
    /* 悬停：轻微提亮，给指针反馈 */
    ".osk-key:hover {"
    "  background-color: #464e5b;"
    "  border-color: rgba(255,255,255,0.18);"
    "}"
    /* 按下：整体下沉 —— 投影收掉、内阴影加深，模拟被按进去 */
    ".osk-key:active {"
    "  background-color: #333944;"
    "  background-image: linear-gradient(to bottom,"
    "    rgba(0,0,0,0.12) 0%,"
    "    rgba(0,0,0,0.03) 100%);"
    "  box-shadow: inset 0 2px 4px rgba(0,0,0,0.45);"
    "}"
    /* 实际生效的按压反馈。
     *
     * 上面的 :active 对 KeyButton 无效 —— 它的壳是 GtkEventBox，
     * 没有 :active 伪类。按压状态由 keybutton.c 通过添加/移除
     * key-pressed 类来维护。
     *
     * 白色高亮：让用户明确看到「这一下被接收了」。 */
    ".osk-key.key-pressed {"
    "  background-color: #f2f5f9;"
    "  background-image: none;"
    "  border-color: #ffffff;"
    "  color: #1a1d22;"
    "  box-shadow: inset 0 2px 5px rgba(0,0,0,0.28),"
    "              0 0 6px rgba(255,255,255,0.35);"
    "}"
    /* 白色按下时，内部的标签也要跟着变深色，否则白底白字看不见 */
    ".osk-key.key-pressed .osk-label-main,"
    ".osk-key.key-pressed .osk-label-up,"
    ".osk-key.key-pressed .osk-label-up-shift,"
    ".osk-key.key-pressed .osk-label-hint {"
    "  color: #1a1d22;"
    "}"
    ".osk-key.swipe-active {"
    "  background-color: #c5548f;"
    "  border-color: #e580b4;"
    "}"
    /* 游戏布局里按住不放的键：绿色高亮 + 发光感 */
    ".osk-key.key-held {"
    "  background-color: #2f8f4e;"
    "  border-color: #57d07e;"
    "  color: #ffffff;"
    "  box-shadow: inset 0 1px 0 rgba(255,255,255,0.25),"
    "              0 0 8px rgba(87,208,126,0.55);"
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
    /* 游戏布局：按键没有上滑功能，右上角的两个角标字符纯属干扰。
     * 隐藏它们（连同左上角的 Shift 提示一起），让按键表面干净。 */
    ".osk-no-swipe-hint .osk-label-up,"
    ".osk-no-swipe-hint .osk-label-up-shift,"
    ".osk-no-swipe-hint .osk-label-hint {"
    "  opacity: 0;"
    "  font-size: 0;"
    "}"
    /* 位置与尺寸对话框里的单位提示 */
    ".osk-dim {"
    "  color: #8b929c;"
    "  font-size: 11px;"
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
    /* 游戏布局：画布本身完全透明，只有控件可见 */
    ".osk-gamepad-canvas {"
    "  background-color: transparent;"
    "  background-image: none;"
    "}"
    /* 游戏按键被按住时的视觉反馈：见上面的 .osk-key.key-held
     * （已合并到主按键样式附近，避免重复定义） */
    /* 编辑模式下被拖动的控件 */
    ".osk-transparent-key.gp-selected {"
    "  border-color: #ffc46b;"
    "  border-width: 2px;"
    "}"
    /* 顶栏的编辑按钮处于锁定（编辑模式开启）状态 */
    ".osk-tool.osk-locked {"
    "  background-color: #c9873c;"
    "  border-color: #ffc46b;"
    "  color: #ffffff;"
    "}"
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
    "}"
    /* 滚轮条：竖条形区域，手指沿条上下滑 = 鼠标滚轮滚动 */
    ".osk-wheel-strip {"
    "  background-color: rgba(28,31,36,0.55);"
    "  border: 1px solid rgba(255, 255, 255, 0.30);"
    "  border-radius: 12px;"
    "}"
    ".osk-wheel-strip label {"
    "  color: #ffffff;"
    "  font-size: 14px;"
    "  text-shadow: 1px 1px 3px rgba(0, 0, 0, 0.9);"
    "}"
    /* —— 按键状态反馈的层叠保障（必须放在表尾） ——
     *
     * GTK 的 CSS 等特异性时后写的规则赢，所以状态色要在表尾重申：
     *   1. 修饰键处于 oneshot（蓝）时再按下去，白色按压反馈会被
     *      更靠后的 mod-oneshot 规则盖掉 —— 表现为「Ctrl 按下没有
     *      反馈色」；
     *   2. 透明键（游戏布局）的 osk-transparent-key 背景规则在表内
     *      最靠后，会把 key-held 的绿色反馈整个盖掉 —— 表现为
     *      「游戏布局按键没有按住反馈」。 */
    ".osk-key.key-pressed {"
    "  background-color: #f2f5f9;"
    "  background-image: none;"
    "  border-color: #ffffff;"
    "  color: #1a1d22;"
    "  box-shadow: inset 0 2px 5px rgba(0,0,0,0.28),"
    "              0 0 6px rgba(255,255,255,0.35);"
    "}"
    ".osk-key.key-pressed .osk-label-main,"
    ".osk-key.key-pressed .osk-label-up,"
    ".osk-key.key-pressed .osk-label-up-shift,"
    ".osk-key.key-pressed .osk-label-hint {"
    "  color: #1a1d22;"
    "}"
    /* 游戏布局透明键：按住绿色高亮（3 类特异性压过 2 类的底色规则） */
    ".osk-key.osk-transparent-key.key-held {"
    "  background-color: #2f8f4e;"
    "  border-color: #57d07e;"
    "  color: #ffffff;"
    "  box-shadow: inset 0 1px 0 rgba(255,255,255,0.25),"
    "              0 0 8px rgba(87,208,126,0.55);"
    "}"
    /* 透明键按压瞬间的白色反馈（HOLD 键整个按住期间都算） */
    ".osk-key.osk-transparent-key.key-pressed {"
    "  background-color: #f2f5f9;"
    "  background-image: none;"
    "  border-color: #ffffff;"
    "  color: #1a1d22;"
    "}";

/* ===== 动态字号 =====
 * 静态 CSS 里写死字号无法运行时调整，这里单独用一个高优先级的 provider，
 * 变更时重载内容即可，不需要重建整个布局。 */
static GtkCssProvider *g_font_provider = NULL;

void app_apply_font(App *app) {
    if (!app || !g_font_provider) return;

    gint fs = app->config.font_size;
    if (fs < OSK_FONT_SIZE_MIN) fs = OSK_FONT_SIZE_MIN;
    if (fs > OSK_FONT_SIZE_MAX) fs = OSK_FONT_SIZE_MAX;

    /* 角标字号由设置单独控制（影响字母键右上角的上滑字符） */
    gint corner = app->config.corner_font_size;
    if (corner < OSK_CORNER_FONT_SIZE_MIN) corner = OSK_CORNER_FONT_SIZE_MIN;
    if (corner > OSK_CORNER_FONT_SIZE_MAX) corner = OSK_CORNER_FONT_SIZE_MAX;

    /* 主标签按字符数分级缩小：键宽固定，长标签靠降字号来放下，
     * 而不是被省略号截断。txt1 是单字符（保持设定字号），
     * 每多一个字符降一档，5 字符及以上用最小档。 */
    gint fs2 = MAX(8, fs * 80 / 100);
    gint fs3 = MAX(8, fs * 64 / 100);
    gint fs4 = MAX(7, fs * 52 / 100);
    gint fs5 = MAX(7, fs * 44 / 100);

    char *css = g_strdup_printf(
        ".osk-label-main { font-size: %dpx; }"
        ".osk-label-main.txt2 { font-size: %dpx; }"
        ".osk-label-main.txt3 { font-size: %dpx; }"
        ".osk-label-main.txt4 { font-size: %dpx; }"
        ".osk-label-main.txt5 { font-size: %dpx; }"
        ".osk-label-up, .osk-label-up-shift, .osk-label-hint {"
        "  font-size: %dpx;"
        "}"
        /* 顶栏里的小按键（换页/home/end）尺寸只有 34x20，
         * 必须把小字号钉死，否则会被上面的规则放大后挤不下。
         * 放在最后且选择器更具体，确保覆盖生效。 */
        ".osk-tool .osk-label-main,"
        ".osk-tool .osk-label-main.txt2,"
        ".osk-tool .osk-label-main.txt3,"
        ".osk-tool .osk-label-main.txt4,"
        ".osk-tool .osk-label-main.txt5 {"
        "  font-size: 11px;"
        "}",
        fs, fs2, fs3, fs4, fs5, corner);

    gtk_css_provider_load_from_data(g_font_provider, css, -1, NULL);
    g_free(css);
}

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

    /* 字号 provider 优先级更高，覆盖基础样式里的字号 */
    g_font_provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(g_font_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);

    /* 外观：先取默认值，再用配置文件覆盖。
     * 以前这里是硬编码的六个数字，改完设置重启就丢，
     * 「恢复默认」按钮等于每次启动的自动行为。 */
    config_load(&g_app.config);
    app_apply_font(&g_app);

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
    g_app.gamepad = osk_gamepad_new("/dev/uinput", &error);
    if (!g_app.gamepad) {
        g_printerr("[OSK] 手柄初始化失败：%s（摇杆与手柄键不可用）\n",
                    error->message ? error->message : "未知错误");
        g_clear_error(&error);
    }

    g_app.key_buttons = g_ptr_array_new();

    /* 主窗口构建（拆成函数：切出游戏布局时用它整体重建窗口） */
    create_main_window(&g_app);

    /* 游戏布局：从配置载入自定义控件表（没有就用默认布局）。
     * 必须在切到游戏布局之前完成，否则画布构建时拿不到模型。 */
    {
        GpState *st = gamepad_state();
        st->model = config_load_gamepad();
        g_app.gp = st;
    }

    /* 加载默认布局：最简（启动快、窗口小，需要完整键盘再切换） */
    app_set_layout(&g_app, layout_lookup("minimal"));

    /* 显示主窗口（窗口状态信号已在 create_main_window 里连接） */
    show_main_window(&g_app);

    gtk_main();
    return 0;
}
