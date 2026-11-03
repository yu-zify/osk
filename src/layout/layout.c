#include "layout.h"
#include "widgets/keybutton.h"
#include <cairo.h>
#include <string.h>


const LayoutOps *layout_touchpad_ops(void);
/* ===== 布局注册表 ===== */

static const LayoutOps *const *get_table(gsize *count) {
    static const LayoutOps *table[8];
    static gsize n = 0;
    if (n == 0) {
        table[n++] = layout_standard_ops();
        table[n++] = layout_minimal_ops();
        table[n++] = layout_fullscreen_ops();
        table[n++] = layout_touchpad_ops();
        table[n++] = layout_game_ops();
    }
    *count = n;
    return table;
}

const LayoutOps *layout_lookup(const char *name) {
    if (!name) return NULL;
    gsize n;
    const LayoutOps *const *t = get_table(&n);
    for (gsize i = 0; i < n; ++i)
        if (strcmp(t[i]->name, name) == 0)
            return t[i];
    return NULL;
}

gboolean layout_clamp_page(App *app) {
    if (!app || !app->layout) return FALSE;

    gint pages = app->layout->pages > 0 ? app->layout->pages : 1;
    gint old   = app->page;
    if (app->page < 0)      app->page = 0;
    if (app->page >= pages) app->page = 0;
    return app->page != old;
}

/* ===== input region ===== */

static guint g_input_region_idle = 0;

/* 上一次应用过的区域，用来避免重复打印日志 */
static cairo_region_t *g_last_region = NULL;

/* 递归收集需要接收输入的控件。
 * 判定规则：
 *   - 按键（KeyButton 的 GtkEventBox，带 "osk-keybutton" 标记）
 *   - GtkButton 及其子类（设置/最小化/home/end/GtkMenuButton 等）
 *
 * 注意：绝不能把整条顶栏当成一个矩形收进来。顶栏是纵向 GtkBox 的子控件，
 * 会占满整行宽度，那样整个顶部横条都会拦截点击，透明布局的穿透就废了。
 * 弹出菜单是独立的 popup 窗口，不需要出现在 input region 里。 */
static void collect_input_widgets(GtkWidget *w, GList **list) {
    if (!gtk_widget_get_mapped(w))
        return;

    if (GTK_IS_BUTTON(w)) {
        *list = g_list_append(*list, w);
    } else if (GTK_IS_EVENT_BOX(w) &&
               g_object_get_data(G_OBJECT(w), "osk-keybutton")) {
        *list = g_list_append(*list, w);
    }

    if (GTK_IS_CONTAINER(w)) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(w));
        for (GList *it = children; it; it = it->next)
            collect_input_widgets(GTK_WIDGET(it->data), list);
        g_list_free(children);
    }
}

/* 计算并立即应用输入区。
 *
 * 为什么必须"立即"：窗口每次 resize（尤其是异步的 maximize）之后，
 * GTK 都会把 input shape 重置回整个窗口。如果只是延时应用一次，
 * 最大化晚于定时器完成时，区域就被清掉了，穿透随之失效。
 * 所以 size-allocate 里要同步调用本函数。 */
void layout_apply_input_region_now(App *app) {
    if (!app || !app->window) return;
    if (!app->layout || !app->layout->transparent) return;

    GtkWidget *window = app->window;
    if (!gtk_widget_get_mapped(window)) return;

    GdkWindow *gw = gtk_widget_get_window(window);
    if (!gw) return;

    GList *widgets = NULL;
    collect_input_widgets(window, &widgets);

    cairo_region_t *region = cairo_region_create();
    for (GList *it = widgets; it; it = it->next) {
        GtkWidget *w = GTK_WIDGET(it->data);
        GtkAllocation a;
        gtk_widget_get_allocation(w, &a);
        if (a.width <= 0 || a.height <= 0)
            continue;

        /* translate 失败时必须跳过，否则会用未初始化的坐标污染区域 */
        gint wx = 0, wy = 0;
        if (!gtk_widget_translate_coordinates(w, window, 0, 0, &wx, &wy))
            continue;

        cairo_rectangle_int_t r = { wx, wy, a.width, a.height };
        cairo_region_union_rectangle(region, &r);
    }
    g_list_free(widgets);

    /* 直接作用于 toplevel 的 GdkWindow，坐标与窗口原点一致 */
    gdk_window_input_shape_combine_region(gw, region, 0, 0);

    /* 只在区域真正变化时记一条，避免收敛期每 40ms 刷屏 */
    {
        gboolean changed = !g_last_region ||
                           !cairo_region_equal(g_last_region, region);
        if (changed) {
            cairo_rectangle_int_t ext = { 0, 0, 0, 0 };
            cairo_region_get_extents(region, &ext);
            g_print("[OSK] 输入区：%d 个矩形，范围 %dx%d+%d+%d（窗口 %dx%d）\n",
                    cairo_region_num_rectangles(region),
                    ext.width, ext.height, ext.x, ext.y,
                    gdk_window_get_width(gw), gdk_window_get_height(gw));

            if (g_last_region) cairo_region_destroy(g_last_region);
            g_last_region = cairo_region_copy(region);
        }
    }

    cairo_region_destroy(region);
}

/* 切换布局后的一小段时间内持续重算输入区。
 *
 * 为什么不能只算一次：切到全屏/游戏时会调 gtk_window_maximize()，
 * 那是异步的。紧接着算出的 widget allocation 还是旧值（小窗口），
 * 等到 WM 真正完成最大化，布局才变成最终尺寸，但此时不会再触发
 * size-allocate，区域就永久停在错误坐标上 —— 表现就是「首次切换穿透失效，
 * 再点一次布局（菜单开合引发 configure）才好」。
 *
 * 这里在切换后的 1 秒内反复重算，覆盖 WM 完成最大化的各种时机。 */
#define REGION_SETTLE_MS      40    /* 重算间隔 */
#define REGION_SETTLE_ROUNDS  25    /* 40ms × 25 ≈ 1s */

static gint g_settle_left = 0;

static gboolean region_settle_tick(gpointer data) {
    App *app = data;

    if (!app->layout || !app->layout->transparent) {
        g_input_region_idle = 0;
        g_settle_left = 0;
        return G_SOURCE_REMOVE;
    }

    layout_apply_input_region_now(app);

    if (--g_settle_left <= 0) {
        g_input_region_idle = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

void layout_apply_input_region(App *app, gboolean transparent) {
    if (!app || !app->window)
        return;

    if (!transparent) {
        /* 退出穿透布局：恢复整窗可点，并丢掉缓存的区域 */
        GdkWindow *gw = gtk_widget_get_window(app->window);
        if (gw)
            gdk_window_input_shape_combine_region(gw, NULL, 0, 0);
        if (g_last_region) {
            cairo_region_destroy(g_last_region);
            g_last_region = NULL;
        }
        if (g_input_region_idle) {
            g_source_remove(g_input_region_idle);
            g_input_region_idle = 0;
        }
        g_settle_left = 0;
        return;
    }

    /* 先同步应用一次，让第一帧就有大致正确的区域 */
    layout_apply_input_region_now(app);

    /* 再启动一段收敛期，等 WM 完成最大化后自动纠正 */
    if (g_input_region_idle)
        g_source_remove(g_input_region_idle);
    g_settle_left = REGION_SETTLE_ROUNDS;
    g_input_region_idle = g_timeout_add(REGION_SETTLE_MS,
                                        region_settle_tick, app);
}