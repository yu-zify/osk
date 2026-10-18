#include "touchpad.h"
#include <gdk/gdk.h>

struct _TouchpadWidget {
    GtkWidget    *area;       /* GtkDrawingArea */
    OskTouchpad  *backend;
};

/* ===== 坐标映射 ===== */

static void map_coords(TouchpadWidget *tw, gdouble lx, gdouble ly,
                        int *ox, int *oy) {
    GtkAllocation a;
    gtk_widget_get_allocation(tw->area, &a);
    if (a.width  <= 0) a.width  = 1;
    if (a.height <= 0) a.height = 1;

    *ox = (int)(lx / a.width  * 4096.0);
    *oy = (int)(ly / a.height * 4096.0);
    if (*ox < 0) *ox = 0;
    if (*oy < 0) *oy = 0;
    if (*ox > 4096) *ox = 4096;
    if (*oy > 4096) *oy = 4096;
}

/* ===== 触摸事件 ===== */

static gboolean on_touch_event(GtkWidget *widget, GdkEvent *event,
                                gpointer data) {
    (void)widget;
    TouchpadWidget *tw = data;
    
    g_print("[tp] touch type=%d seq=%p x=%.1f y=%.1f\n", ((GdkEventTouch *)event)->type, (void*)((GdkEventTouch *)event)->sequence, ((GdkEventTouch *)event)->x, ((GdkEventTouch *)event)->y);
    if (!tw->backend) return FALSE;

    GdkEventTouch *te = (GdkEventTouch *)event;
    GdkEventSequence *seq = te->sequence;
    guint id = (guint)(guintptr)seq;

    int ox, oy;
    map_coords(tw, te->x, te->y, &ox, &oy);

    switch (te->type) {
    case GDK_TOUCH_BEGIN:
        osk_touchpad_down(tw->backend, id, ox, oy);
        break;
    case GDK_TOUCH_UPDATE:
        osk_touchpad_move(tw->backend, id, ox, oy);
        break;
    case GDK_TOUCH_END:
    case GDK_TOUCH_CANCEL:
        osk_touchpad_up(tw->backend, id);
        break;
    default:
        break;
    }
    return TRUE;
}

/* ===== 鼠标事件（用鼠标模拟单指触摸，便于桌面测试） ===== */

#define MOUSE_FAKE_ID 0xFFFFu

static gboolean on_mouse_press(GtkWidget *widget, GdkEventButton *ev,
                                gpointer data) {
    (void)widget;
    TouchpadWidget *tw = data;
    if (!tw->backend) return FALSE;
    if (ev->button != GDK_BUTTON_PRIMARY) return FALSE;

    int ox, oy;
    map_coords(tw, ev->x, ev->y, &ox, &oy);
    osk_touchpad_down(tw->backend, MOUSE_FAKE_ID, ox, oy);
    return TRUE;
}

static gboolean on_mouse_motion(GtkWidget *widget, GdkEventMotion *ev,
                                 gpointer data) {
    (void)widget;
    TouchpadWidget *tw = data;
    if (!tw->backend) return FALSE;
    if (!(ev->state & GDK_BUTTON1_MASK)) return FALSE;

    int ox, oy;
    map_coords(tw, ev->x, ev->y, &ox, &oy);
    osk_touchpad_move(tw->backend, MOUSE_FAKE_ID, ox, oy);
    return TRUE;
}

static gboolean on_mouse_release(GtkWidget *widget, GdkEventButton *ev,
                                  gpointer data) {
    (void)widget;
    TouchpadWidget *tw = data;
    if (!tw->backend) return FALSE;
    if (ev->button != GDK_BUTTON_PRIMARY) return FALSE;

    osk_touchpad_up(tw->backend, MOUSE_FAKE_ID);
    return TRUE;
}

/* ===== 绘制 ===== */

static gboolean on_draw(GtkWidget *w, cairo_t *cr, gpointer data) {
    (void)data;
    GtkAllocation a;
    gtk_widget_get_allocation(w, &a);

    cairo_set_source_rgb(cr, 0.18, 0.18, 0.20);
    cairo_paint(cr);

    cairo_set_source_rgb(cr, 0.30, 0.30, 0.35);
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, 0.5, 0.5, a.width - 1, a.height - 1);
    cairo_stroke(cr);

    cairo_set_source_rgb(cr, 0.25, 0.25, 0.30);
    cairo_move_to(cr, a.width / 2.0, a.height / 4.0);
    cairo_line_to(cr, a.width / 2.0, a.height * 3.0 / 4.0);
    cairo_move_to(cr, a.width / 4.0, a.height / 2.0);
    cairo_line_to(cr, a.width * 3.0 / 4.0, a.height / 2.0);
    cairo_stroke(cr);

    return TRUE;
}

/* ===== 构造 ===== */

TouchpadWidget *touchpad_widget_new(OskTouchpad *backend) {
    TouchpadWidget *tw = g_new0(TouchpadWidget, 1);
    tw->backend = backend;
    tw->area = gtk_drawing_area_new();

    gtk_widget_set_can_focus(tw->area, FALSE);

    /* 加上触摸、鼠标移动、鼠标按钮的事件掩码 */
    gtk_widget_add_events(tw->area,
        GDK_TOUCH_MASK |
        GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
        GDK_POINTER_MOTION_MASK);
        
/* 调试：看 widget 是否真的添加了 touch mask */
g_print("[tp] touch mask = %s\n",
        (gtk_widget_get_events(tw->area) & GDK_TOUCH_MASK) ? "yes" : "no");

    g_signal_connect(tw->area, "draw",         G_CALLBACK(on_draw),          tw);
    g_signal_connect(tw->area, "touch-event",  G_CALLBACK(on_touch_event),   tw);
    g_signal_connect(tw->area, "button-press-event",
                     G_CALLBACK(on_mouse_press),   tw);
    g_signal_connect(tw->area, "motion-notify-event",
                     G_CALLBACK(on_mouse_motion),  tw);
    g_signal_connect(tw->area, "button-release-event",
                     G_CALLBACK(on_mouse_release), tw);

    g_object_set_data_full(G_OBJECT(tw->area), "osk-touchpad",
                           tw, (GDestroyNotify)g_free);
    return tw;
}

GtkWidget *touchpad_widget_gtk(TouchpadWidget *tw) {
    return tw ? tw->area : NULL;
}