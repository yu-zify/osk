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

/* ===== input region ===== */

static guint g_input_region_idle = 0;

/* 递归收集所有 mapped 的 GtkEventBox（KeyButton）和 GtkButton（工具栏按钮）。
 * 其他容器（GtkBox、GtkOverlay、GtkLabel）不加入 region。 */
static void collect_input_widgets(GtkWidget *w, GList **list) {
    if (!gtk_widget_get_mapped(w))
        return;

    if (GTK_IS_EVENT_BOX(w) || GTK_IS_BUTTON(w))
        *list = g_list_append(*list, w);

    if (GTK_IS_CONTAINER(w)) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(w));
        for (GList *it = children; it; it = it->next)
            collect_input_widgets(GTK_WIDGET(it->data), list);
        g_list_free(children);
    }
}

static gboolean apply_input_region_idle(gpointer data) {
    g_input_region_idle = 0;
    App *app = data;
    GtkWidget *window = app->window;

    if (!window || !gtk_widget_get_mapped(window))
        return G_SOURCE_REMOVE;

    GList *widgets = NULL;
    collect_input_widgets(window, &widgets);

    cairo_region_t *region = cairo_region_create();
    for (GList *it = widgets; it; it = it->next) {
        GtkWidget *w = GTK_WIDGET(it->data);
        GtkAllocation a;
        gtk_widget_get_allocation(w, &a);
        if (a.width <= 0 || a.height <= 0)
            continue;

        gint wx, wy;
        gtk_widget_translate_coordinates(w, window, 0, 0, &wx, &wy);

        cairo_rectangle_int_t r = { wx, wy, a.width, a.height };
        cairo_region_union_rectangle(region, &r);
    }
    g_list_free(widgets);

    gtk_widget_input_shape_combine_region(window, region);
    cairo_region_destroy(region);
    return G_SOURCE_REMOVE;
}

void layout_apply_input_region(App *app, gboolean transparent) {
    if (!app || !app->window)
        return;

    if (!transparent) {
        gtk_widget_input_shape_combine_region(app->window, NULL);
        if (g_input_region_idle) {
            g_source_remove(g_input_region_idle);
            g_input_region_idle = 0;
        }
        return;
    }

    /* 关键改动 1：总是取消旧定时器，重新调度 */
    if (g_input_region_idle)
        g_source_remove(g_input_region_idle);

    /* 关键改动 2：用 50ms 延迟代替 idle，
       容忍 maximize / size-allocate 之类的异步窗口变化 */
    g_input_region_idle = g_timeout_add(50, apply_input_region_idle, app);
}