#include "layout.h"
#include "widgets/keybutton.h"
#include <cairo.h>
#include <string.h>

/* ===== 布局注册表 ===== */

static const LayoutOps *const *get_table(gsize *count) {
    static const LayoutOps *table[8];
    static gsize n = 0;
    if (n == 0) {
        table[n++] = layout_standard_ops();
        table[n++] = layout_minimal_ops();
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

/* ===== 点击穿透洞 =====
 *
 * 挖洞期间这里保存洞的矩形；g_hole_active 为 FALSE 表示没挖。
 *
 * 洞的尺寸取小值：太小可能因为坐标误差漏掉点击，太大会吃掉
 * 附近的触摸。4px 是个折中 —— 触控板的轻点判定阈值是 8px，
 * 所以即使洞略大于 1px 也不会误伤。 */
#define HOLE_SIZE 4

static gboolean g_hole_active = FALSE;
static gint     g_hole_x = 0;
static gint     g_hole_y = 0;

/* ===== 光标位置跟踪 =====
 *
 * Wayland 下拿不到可靠的全局指针坐标：
 *   gdk_device_get_position() 受合成器限制，返回值不可信
 *   gdk_window_get_origin()   同样不可靠
 * 两者相减会得到无意义的坐标 —— 之前的实现就是这样把洞挖到了
 * 屏幕角落，导致穿透失效、光标乱飘。
 *
 * 改用「自己累加」：OSK 注入的每一次 REL_X/REL_Y 都是自己发出的，
 * 把它们累加就是光标位置，不需要向合成器查询。
 *
 * 局限：只在 OSK 运行期间、且光标未被其他程序移动时准确。
 * 对「用触控板移动光标再点击」这个场景足够 —— 因为位置正是
 * 由触控板自己移动出来的。
 */
static gint g_cursor_x = 0;
static gint g_cursor_y = 0;
static gboolean g_cursor_known = FALSE;

void layout_note_cursor_move(App *app, gint dx, gint dy) {
    if (!app || !app->window) return;

    /* 第一次移动时以窗口左上角为起点 —— 虽然不精确，
     * 但比用不可靠的全局查询要好，且只影响第一次点击。 */
    if (!g_cursor_known) {
        g_cursor_x = 0;
        g_cursor_y = 0;
        g_cursor_known = TRUE;
    }

    g_cursor_x += dx;
    g_cursor_y += dy;

    /* 夹到窗口范围内：光标移出窗口后点击本来就不会命中 OSK，
     * 这里保持一个合法值避免后续计算异常。 */
    GdkWindow *gw = gtk_widget_get_window(app->window);
    if (gw) {
        gint w = gdk_window_get_width(gw);
        gint h = gdk_window_get_height(gw);
        if (w > 0 && g_cursor_x > w) g_cursor_x = w;
        if (w > 0 && g_cursor_x < 0) g_cursor_x = 0;
        if (h > 0 && g_cursor_y > h) g_cursor_y = h;
        if (h > 0 && g_cursor_y < 0) g_cursor_y = 0;
    }
}

/* 供外部（窗口 motion 事件）校准：光标真的移到某处时以真实值为准。
 * 这能在用户用物理鼠标、或其他程序移动光标后重新对齐。 */
void layout_set_cursor_pos(App *app, gint x, gint y) {
    (void)app;
    g_cursor_x = x;
    g_cursor_y = y;
    g_cursor_known = TRUE;
}

/* 判断控件是否需要接收点击。
 *
 * 这里不能用「所有挂了事件掩码的控件都算」这种宽松规则：
 * 很多 GtkContainer 内部会给自己挂上指针事件，一旦放进来，
 * 整条顶栏（占满整行宽度）就会被算进输入区，穿透直接失效。
 *
 * 所以维持白名单，但补一条显式通道：控件可以用 "osk-input" 标记
 * 自己报名。摇杆、触控板这类自绘的 GtkDrawingArea 就靠它进来 ——
 * 它们既不是 GtkButton 也没有 osk-keybutton 标记，之前被漏掉，
 * 表现就是「点上去直接穿透，无法拖动」。 */
static gboolean widget_wants_input(GtkWidget *w) {
    if (g_object_get_data(G_OBJECT(w), "osk-input"))
        return TRUE;

    if (GTK_IS_BUTTON(w))
        return TRUE;

    if (GTK_IS_EVENT_BOX(w) &&
        g_object_get_data(G_OBJECT(w), "osk-keybutton"))
        return TRUE;

    return FALSE;
}

/* 递归收集需要接收输入的控件。
 *
 * 注意：绝不能把整条顶栏当成一个矩形收进来。顶栏是纵向 GtkBox 的子控件，
 * 会占满整行宽度，那样整个顶部横条都会拦截点击，透明布局的穿透就废了。
 * 弹出菜单是独立的 popup 窗口，不需要出现在 input region 里。 */
static void collect_input_widgets(GtkWidget *w, GList **list) {
    if (!gtk_widget_get_mapped(w))
        return;

    if (widget_wants_input(w))
        *list = g_list_append(*list, w);

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

    /* 挖洞：从区域里扣掉光标处的一小块，让注入的点击穿过去。
     * 必须在 union 之后做 subtract，否则后面 union 别的控件时
     * 会把洞重新填上（如果洞恰好落在某个控件矩形内）。 */
    if (g_hole_active) {
        cairo_rectangle_int_t hole = { g_hole_x, g_hole_y,
                                        HOLE_SIZE, HOLE_SIZE };
        cairo_region_subtract_rectangle(region, &hole);
    }

    /* 直接作用于 toplevel 的 GdkWindow，坐标与窗口原点一致 */
    gdk_window_input_shape_combine_region(gw, region, 0, 0);

    /* 只在区域真正变化时记一条，避免收敛期每 40ms 刷屏。
     *
     * 挖洞期间不记：每次点击都会改一次区域，会刷出大量无意义日志。 */
    if (!g_hole_active) {
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

/* ===== 点击穿透洞的实现 ===== */

/* 取光标在窗口内的坐标。
 *
 * 只用自己累加的值，不向合成器查询 —— 原因见上面「光标位置跟踪」。
 * 返回 FALSE 表示还不知道光标在哪（从未移动过），此时不挖洞。 */
static gboolean get_pointer_pos(App *app, gint *x, gint *y) {
    if (!app || !app->window) return FALSE;
    if (!g_cursor_known) return FALSE;

    GdkWindow *gw = gtk_widget_get_window(app->window);
    if (!gw) return FALSE;

    gint w = gdk_window_get_width(gw);
    gint h = gdk_window_get_height(gw);
    if (w <= 0 || h <= 0) return FALSE;

    *x = g_cursor_x;
    *y = g_cursor_y;

    /* 落在窗口内才算有效；超出说明光标已经离开 OSK 覆盖范围 */
    return (*x >= 0 && *y >= 0 && *x < w && *y < h);
}

void layout_punch_hole(App *app) {
    if (!app || !app->window) return;
    if (!app->layout || !app->layout->transparent) return;

    gint x = 0, y = 0;
    if (!get_pointer_pos(app, &x, &y)) {
        /* 位置未知：不挖洞，让这次点击照常被 OSK 处理 */
        g_hole_active = FALSE;
        return;
    }

    /* 以光标为中心挖一个小方块。
     *
     * 洞口坐标必须夹在窗口内：subtract_rectangle 对窗口外的矩形
     * 虽然是空操作，但如果坐标算出很大的负数或溢出值，
     * cairo 的区域运算会产生难以预料的结果 —— 之前的版本
     * 就是用了不可靠的全局坐标，把洞挖到屏幕角落，
     * 表现成「穿透失效 + 光标乱飘」。 */
    gint hx = x - HOLE_SIZE / 2;
    gint hy = y - HOLE_SIZE / 2;

    GdkWindow *gw = gtk_widget_get_window(app->window);
    if (gw) {
        gint w = gdk_window_get_width(gw);
        gint h = gdk_window_get_height(gw);
        if (hx < 0) hx = 0;
        if (hy < 0) hy = 0;
        if (w > 0 && hx > w - HOLE_SIZE) hx = w - HOLE_SIZE;
        if (h > 0 && hy > h - HOLE_SIZE) hy = h - HOLE_SIZE;
    }

    g_hole_x = hx;
    g_hole_y = hy;
    g_hole_active = TRUE;

    /* 立即重算并应用，不依赖收敛期定时器 */
    layout_apply_input_region_now(app);
}

void layout_restore_hole(App *app) {
    if (!g_hole_active) return;

    g_hole_active = FALSE;

    if (!app || !app->window) return;
    if (!app->layout || !app->layout->transparent) return;

    layout_apply_input_region_now(app);
}