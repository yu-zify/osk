#include "touchpad.h"

#define MAX_TOUCHES        10
#define LONG_PRESS_DRAG_MS 500
#define DRAG_CANCEL_PX     8.0
#define SENSITIVITY        1.0
#define SCROLL_SENSITIVITY 8.0

typedef struct {
    guintptr id;
    gdouble  last_x, last_y;
    gdouble  start_x, start_y;
    gboolean active;
} TouchPoint;

struct _TouchpadWidget {
    GtkWidget  *area;
    OskMouse   *mouse;

    TouchPoint touches[MAX_TOUCHES];
    gint       n_active;

    gdouble  last_mid_x, last_mid_y;
    gboolean has_mid;

    gdouble  scroll_accum_x, scroll_accum_y;
    gdouble  accum_x, accum_y;

    /* 长按拖动（保留） */
    gboolean dragging;
    guint    drag_timer;
};

/* ===== 触点管理 ===== */

static TouchPoint *find_touch(TouchpadWidget *tw, guintptr id) {
    for (int i = 0; i < MAX_TOUCHES; ++i)
        if (tw->touches[i].active && tw->touches[i].id == id)
            return &tw->touches[i];
    return NULL;
}

static TouchPoint *alloc_touch(TouchpadWidget *tw, guintptr id) {
    for (int i = 0; i < MAX_TOUCHES; ++i) {
        if (!tw->touches[i].active) {
            tw->touches[i].active = TRUE;
            tw->touches[i].id = id;
            tw->n_active++;
            return &tw->touches[i];
        }
    }
    return NULL;
}

static void release_touch(TouchpadWidget *tw, TouchPoint *tp) {
    tp->active = FALSE;
    tw->n_active--;
    if (tw->n_active == 0)
        tw->has_mid = FALSE;
}

static void compute_mid(TouchpadWidget *tw, gdouble *mx, gdouble *my) {
    gdouble sx = 0, sy = 0;
    int n = 0;
    for (int i = 0; i < MAX_TOUCHES; ++i) {
        if (tw->touches[i].active) {
            sx += tw->touches[i].last_x;
            sy += tw->touches[i].last_y;
            n++;
        }
    }
    if (n == 0) { *mx = *my = 0; return; }
    *mx = sx / n;
    *my = sy / n;
}

static void emit_move(TouchpadWidget *tw, gdouble dx, gdouble dy) {
    tw->accum_x += dx * SENSITIVITY;
    tw->accum_y += dy * SENSITIVITY;

    gint ix = (gint)tw->accum_x;
    gint iy = (gint)tw->accum_y;

    if (ix == 0 && iy == 0)
        return;

    tw->accum_x -= ix;
    tw->accum_y -= iy;
    osk_mouse_move_rel(tw->mouse, ix, iy);
}

/* ===== 长按拖动 ===== */

static gboolean on_drag_enter(gpointer data) {
    TouchpadWidget *tw = data;
    tw->drag_timer = 0;

    if (tw->n_active != 1 || !tw->mouse)
        return G_SOURCE_REMOVE;

    osk_mouse_button(tw->mouse, BTN_LEFT, TRUE);
    tw->dragging = TRUE;
    return G_SOURCE_REMOVE;
}

static void cancel_drag_timer(TouchpadWidget *tw) {
    if (tw->drag_timer) {
        g_source_remove(tw->drag_timer);
        tw->drag_timer = 0;
    }
}

/* ===== 触摸事件 ===== */

static gboolean on_touch_event(GtkWidget *w, GdkEvent *ev, gpointer data) {
    (void)w;
    TouchpadWidget *tw = data;
    if (!tw->mouse) return FALSE;

    GdkEventTouch *te = (GdkEventTouch *)ev;
    guintptr id = (guintptr)te->sequence;

    switch (te->type) {
    case GDK_TOUCH_BEGIN: {
        osk_mouse_move_rel(tw->mouse, 1, 0); 
        osk_mouse_move_rel(tw->mouse, -1, 0);
        TouchPoint *tp = alloc_touch(tw, id);
        if (!tp) return FALSE;
        tp->last_x = tp->start_x = te->x;
        tp->last_y = tp->start_y = te->y;

        tw->has_mid = FALSE;

        if (tw->n_active == 1) {
            cancel_drag_timer(tw);
            tw->drag_timer = g_timeout_add(LONG_PRESS_DRAG_MS,
                                            on_drag_enter, tw);
        } else {
            cancel_drag_timer(tw);
        }
        break;
    }
    case GDK_TOUCH_UPDATE: {
        TouchPoint *tp = find_touch(tw, id);
        if (!tp) return FALSE;

        tp->last_x = te->x;
        tp->last_y = te->y;

        if (tw->drag_timer) {
            gdouble ddx = tp->last_x - tp->start_x;
            gdouble ddy = tp->last_y - tp->start_y;
            if (ddx * ddx + ddy * ddy > DRAG_CANCEL_PX * DRAG_CANCEL_PX)
                cancel_drag_timer(tw);
        }

        gdouble mx, my;
        compute_mid(tw, &mx, &my);

        if (tw->has_mid) {
            gdouble dx = mx - tw->last_mid_x;
            gdouble dy = my - tw->last_mid_y;

            if (tw->dragging) {
                if (tw->n_active == 1)
                    emit_move(tw, dx, dy);
            } else if (tw->n_active == 1) {
                emit_move(tw, dx, dy);
            } else if (tw->n_active == 2) {
                tw->scroll_accum_x += dx / SCROLL_SENSITIVITY;
                tw->scroll_accum_y += dy / SCROLL_SENSITIVITY;

                gint sx = (gint)tw->scroll_accum_x;
                gint sy = (gint)tw->scroll_accum_y;
                if (sx != 0 || sy != 0) {
                    tw->scroll_accum_x -= sx;
                    tw->scroll_accum_y -= sy;
                    osk_mouse_scroll(tw->mouse, sx, sy);
                }
            }
        }
        tw->last_mid_x = mx;
        tw->last_mid_y = my;
        tw->has_mid = TRUE;
        break;
    }
    case GDK_TOUCH_END:
    case GDK_TOUCH_CANCEL: {
        TouchPoint *tp = find_touch(tw, id);
        if (!tp) return FALSE;

        cancel_drag_timer(tw);

        gboolean was_last = (tw->n_active == 1);

        if (was_last) {
            /* 拖动结束：释放左键 */
            if (tw->dragging) {
                osk_mouse_button(tw->mouse, BTN_LEFT, FALSE);
                tw->dragging = FALSE;
            }
            /* 重置累积器 */
            tw->accum_x = 0;
            tw->accum_y = 0;
            tw->scroll_accum_x = 0;
            tw->scroll_accum_y = 0;
        }
        release_touch(tw, tp);
        tw->has_mid = FALSE;
        break;
    }
    default:
        break;
    }
    return TRUE;
}

/* ===== 鼠标事件（桌面调试用） ===== */

static gdouble  g_mouse_last_x = 0, g_mouse_last_y = 0;
static gboolean g_mouse_pressed = FALSE;

static gboolean on_mouse_press(GtkWidget *w, GdkEventButton *ev, gpointer data) {
    (void)w;
    TouchpadWidget *tw = data;
    if (!tw->mouse || ev->button != GDK_BUTTON_PRIMARY) return FALSE;

    g_mouse_last_x = ev->x;
    g_mouse_last_y = ev->y;
    g_mouse_pressed = TRUE;
    return TRUE;
}

static gboolean on_mouse_motion(GtkWidget *w, GdkEventMotion *ev, gpointer data) {
    (void)w;
    TouchpadWidget *tw = data;
    if (!tw->mouse || !g_mouse_pressed) return FALSE;

    gdouble dx = ev->x - g_mouse_last_x;
    gdouble dy = ev->y - g_mouse_last_y;
    g_mouse_last_x = ev->x;
    g_mouse_last_y = ev->y;

    emit_move(tw, dx, dy);
    return TRUE;
}

static gboolean on_mouse_release(GtkWidget *w, GdkEventButton *ev, gpointer data) {
    (void)w;
    TouchpadWidget *tw = data;
    if (!tw->mouse || ev->button != GDK_BUTTON_PRIMARY) return FALSE;

    tw->accum_x = 0;
    tw->accum_y = 0;
    g_mouse_pressed = FALSE;
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

TouchpadWidget *touchpad_widget_new(OskMouse *mouse) {
    TouchpadWidget *tw = g_new0(TouchpadWidget, 1);
    tw->mouse = mouse;
    tw->area = gtk_drawing_area_new();

    gtk_widget_set_can_focus(tw->area, FALSE);
    gtk_widget_add_events(tw->area,
        GDK_TOUCH_MASK |
        GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
        GDK_POINTER_MOTION_MASK);

    g_signal_connect(tw->area, "draw",         G_CALLBACK(on_draw),          tw);
    g_signal_connect(tw->area, "touch-event",  G_CALLBACK(on_touch_event),   tw);
    g_signal_connect(tw->area, "button-press-event",
                     G_CALLBACK(on_mouse_press),   tw);
    g_signal_connect(tw->area, "motion-notify-event",
                     G_CALLBACK(on_mouse_motion),  tw);
    g_signal_connect(tw->area, "button-release-event",
                     G_CALLBACK(on_mouse_release), tw);

    if (mouse) {
        osk_mouse_move_rel(mouse, 1, 0);
        osk_mouse_move_rel(mouse, -1, 0);
    }

    g_object_set_data_full(G_OBJECT(tw->area), "osk-touchpad",
                           tw, (GDestroyNotify)g_free);
    return tw;
}

GtkWidget *touchpad_widget_gtk(TouchpadWidget *tw) {
    return tw ? tw->area : NULL;
}