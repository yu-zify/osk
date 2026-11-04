#include "touchpad.h"

#define MAX_TOUCHES             10
#define TAP_TIMEOUT_MS          200   /* 单次轻点最长持续时间 */
#define TAP_DIST_PX             8.0   /* 单次轻点最大位移 */
#define DOUBLE_TAP_TIMEOUT_MS   350   /* 两次轻点之间的最大间隔 */
#define DOUBLE_TAP_DIST_PX      25.0  /* 两次轻点允许的位置偏差 */
#define DRAG_START_THRESHOLD    6.0   /* 第二下按住后移动多少进入拖动 */
#define SENSITIVITY             1.0
#define SCROLL_SENSITIVITY      8.0

typedef enum {
    TAP_STATE_IDLE,
    TAP_STATE_SINGLE_WAIT,    /* 单指轻点完成，等待第二下或超时 */
    TAP_STATE_DRAG_PENDING,   /* 第二下已按下，等待移动判定 */
    TAP_STATE_DRAGGING,       /* 拖动中 */
} TapState;

typedef struct {
    guintptr id;
    gdouble  last_x, last_y;
    gdouble  start_x, start_y;
    guint64  down_time_us;
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

    /* 手势状态机 */
    TapState tap_state;
    guint    tap_timer;
    gdouble  tap_x, tap_y;
    gdouble  drag_start_x, drag_start_y;
    gint     max_touches;
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

/* ===== 轻点超时：发左键单击 ===== */

static gboolean on_tap_timeout(gpointer data) {
    TouchpadWidget *tw = data;
    tw->tap_timer = 0;
    if (tw->tap_state == TAP_STATE_SINGLE_WAIT) {
        osk_mouse_button(tw->mouse, BTN_LEFT, TRUE);
        osk_mouse_button(tw->mouse, BTN_LEFT, FALSE);
        tw->tap_state = TAP_STATE_IDLE;
    }
    return G_SOURCE_REMOVE;
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
        TouchPoint *tp = alloc_touch(tw, id);
        if (!tp) return FALSE;
        tp->last_x = tp->start_x = te->x;
        tp->last_y = tp->start_y = te->y;
        tp->down_time_us = g_get_monotonic_time();

        if (tw->max_touches < tw->n_active)
            tw->max_touches = tw->n_active;

        /* 第二次点击判定 */
        if (tw->n_active == 1 && tw->tap_state == TAP_STATE_SINGLE_WAIT) {
            if (tw->tap_timer) {
                g_source_remove(tw->tap_timer);
                tw->tap_timer = 0;
            }
            gdouble dx = te->x - tw->tap_x;
            gdouble dy = te->y - tw->tap_y;
            if (dx * dx + dy * dy < DOUBLE_TAP_DIST_PX * DOUBLE_TAP_DIST_PX) {
                /* 位置接近 → 进入拖动等待 */
                tw->tap_state = TAP_STATE_DRAG_PENDING;
                tw->drag_start_x = te->x;
                tw->drag_start_y = te->y;
            } else {
                /* 位置太远 → 第一次的单击立刻发出 */
                osk_mouse_button(tw->mouse, BTN_LEFT, TRUE);
                osk_mouse_button(tw->mouse, BTN_LEFT, FALSE);
                tw->tap_state = TAP_STATE_IDLE;
            }
        } else if (tw->n_active == 2) {
            /* 第二指加入 → 取消拖动意图，走双指 tap/滚动逻辑 */
            if (tw->tap_timer) {
                g_source_remove(tw->tap_timer);
                tw->tap_timer = 0;
            }
            if (tw->tap_state == TAP_STATE_DRAG_PENDING) {
                tw->tap_state = TAP_STATE_IDLE;
            }
        }

        tw->has_mid = FALSE;
        break;
    }

    case GDK_TOUCH_UPDATE: {
        TouchPoint *tp = find_touch(tw, id);
        if (!tp) return FALSE;

        tp->last_x = te->x;
        tp->last_y = te->y;

        /* DRAG_PENDING → 移动超阈值则进入拖动 */
        if (tw->tap_state == TAP_STATE_DRAG_PENDING) {
            gdouble ddx = te->x - tw->drag_start_x;
            gdouble ddy = te->y - tw->drag_start_y;
            if (ddx * ddx + ddy * ddy > DRAG_START_THRESHOLD * DRAG_START_THRESHOLD) {
                osk_mouse_button(tw->mouse, BTN_LEFT, TRUE);
                tw->tap_state = TAP_STATE_DRAGGING;
                tw->accum_x = 0;
                tw->accum_y = 0;
            }
        }

        gdouble mx, my;
        if (tw->tap_state == TAP_STATE_DRAGGING) {
            mx = te->x;
            my = te->y;
        } else {
            compute_mid(tw, &mx, &my);
        }

        if (tw->has_mid) {
            gdouble dx = mx - tw->last_mid_x;
            gdouble dy = my - tw->last_mid_y;

            if (tw->n_active == 1 || tw->tap_state == TAP_STATE_DRAGGING) {
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

        guint64 dt_us = g_get_monotonic_time() - tp->down_time_us;
        gdouble ddx = te->x - tp->start_x;
        gdouble ddy = te->y - tp->start_y;
        gboolean is_tap = (dt_us < (guint64)TAP_TIMEOUT_MS * 1000 &&
                           ddx * ddx + ddy * ddy < TAP_DIST_PX * TAP_DIST_PX);
        gboolean was_last = (tw->n_active == 1);

        release_touch(tw, tp);

        if (was_last) {
            if (tw->tap_state == TAP_STATE_DRAGGING) {
                /* 拖动结束 → 释放左键 */
                osk_mouse_button(tw->mouse, BTN_LEFT, FALSE);
                tw->tap_state = TAP_STATE_IDLE;
            } else if (tw->tap_state == TAP_STATE_DRAG_PENDING) {
                /* 第二下没移动就松开 → 视为左键单击 */
                osk_mouse_button(tw->mouse, BTN_LEFT, TRUE);
                osk_mouse_button(tw->mouse, BTN_LEFT, FALSE);
                tw->tap_state = TAP_STATE_IDLE;
            } else if (is_tap && tw->max_touches >= 2) {
                /* 双指轻点 → 右键 */
                osk_mouse_button(tw->mouse, BTN_RIGHT, TRUE);
                osk_mouse_button(tw->mouse, BTN_RIGHT, FALSE);
                tw->tap_state = TAP_STATE_IDLE;
            } else if (is_tap && tw->max_touches == 1) {
                /* 单指轻点 → 进入等待，350ms 内没第二下则发左键 */
                tw->tap_state = TAP_STATE_SINGLE_WAIT;
                tw->tap_x = te->x;
                tw->tap_y = te->y;
                tw->tap_timer = g_timeout_add(DOUBLE_TAP_TIMEOUT_MS,
                                               on_tap_timeout, tw);
            } else {
                tw->tap_state = TAP_STATE_IDLE;
            }

            tw->accum_x = 0;
            tw->accum_y = 0;
            tw->scroll_accum_x = 0;
            tw->scroll_accum_y = 0;
            tw->max_touches = 0;
        }
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
    tw->tap_state = TAP_STATE_IDLE;

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