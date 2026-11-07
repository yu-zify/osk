#include "touchpad.h"
#include "app.h"
#include "layout/layout.h"

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
    /* 用于点击穿透：注入点击前在光标处挖洞。
     * 可能为 NULL（独立使用时），此时不挖洞，行为与以前一致。 */
    struct _App *app;

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

    /* 点击穿透的延时恢复状态。
     * 注入是同步的，只有「恢复洞」需要延时。 */
    guint      hole_restore_id;   /* 注入后等待恢复的定时器 */
};

/* 触控板指针速度。
 *
 * 从 app->config.mouse_speed 读取，而不是编译期常量 ——
 * 用户在设置里调完要立刻生效，不能要求重启。
 *
 * 没有 app 引用时（独立使用控件）退回 1.0。 */
static gdouble move_speed(TouchpadWidget *tw) {
    if (tw->app) {
        gdouble s = tw->app->config.mouse_speed;
        if (s >= OSK_MOUSE_SPEED_MIN && s <= OSK_MOUSE_SPEED_MAX)
            return s;
    }
    return OSK_MOUSE_SPEED_DEFAULT;
}

/* ===== 点击穿透 =====
 *
 * 注入的 BTN_LEFT 会走和真实设备一样的命中测试。光标压在 OSK 控件上时，
 * 这次点击会被 OSK 吃掉。解决：注入前在光标处挖一个洞。
 *
 * 时序（Wayland 下两条路径是分开的）：
 *   region 变更 → 合成器 socket  → 可能有延迟
 *   uinput 注入 → libinput → 合成器
 * 无法保证先后，所以挖洞后等一小段再注入，注入后再等一小段恢复。
 *
 * 两个延时都是用户无感的量级（毫秒级），但能显著提高成功率。
 */

#define CLICK_RESTORE_DELAY_MS 80   /* 注入后保持洞，避免恢复早于点击 */

/* 注入后延时恢复 */
static gboolean restore_hole_tick(gpointer data) {
    TouchpadWidget *tw = data;
    tw->hole_restore_id = 0;
    if (tw->app)
        layout_restore_hole(tw->app);
    return G_SOURCE_REMOVE;
}

/* 注入一次点击，必要时先挖洞。
 *
 * 所有点击注入都走这里，替代直接调用 osk_mouse_button。
 * 没有 app 引用时（独立使用触控板控件）退化为立即注入，行为与以前一致。
 *
 * ===== 关于延迟 =====
 *
 * 早先这里在挖洞后等了 25ms 才注入，理由是「等合成器处理 region」。
 * 但 layout_punch_hole() 内部是同步调用
 * gdk_window_input_shape_combine_region 的，region 立刻提交，
 * 那 25ms 完全是白等 —— 每次点击都白白多出四十分之一秒。
 *
 * 现在改为：挖洞后立即注入，只保留注入后的 80ms 恢复延时
 * （那个是必要的：恢复早于点击处理会导致点击被 OSK 吃掉）。 */
static void inject_click(TouchpadWidget *tw, guint button) {
    if (!tw || !tw->mouse) return;

    if (!tw->app) {
        /* 没接 layout：直接注入 */
        osk_mouse_button(tw->mouse, button, TRUE);
        osk_mouse_button(tw->mouse, button, FALSE);
        return;
    }

    /* 挖洞（同步生效），然后立刻注入 */
    layout_punch_hole(tw->app);

    osk_mouse_button(tw->mouse, button, TRUE);
    osk_mouse_button(tw->mouse, button, FALSE);

    /* 保持洞 80ms 再恢复，确保点击已经送达下层应用 */
    if (tw->hole_restore_id)
        g_source_remove(tw->hole_restore_id);
    tw->hole_restore_id = g_timeout_add(CLICK_RESTORE_DELAY_MS,
                                         restore_hole_tick, tw);
}


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
    gdouble speed = move_speed(tw) * SENSITIVITY;

    tw->accum_x += dx * speed;
    tw->accum_y += dy * speed;

    gint ix = (gint)tw->accum_x;
    gint iy = (gint)tw->accum_y;

    if (ix == 0 && iy == 0)
        return;

    tw->accum_x -= ix;
    tw->accum_y -= iy;
    osk_mouse_move_rel(tw->mouse, ix, iy);

    /* 关键：把实际注入的增量告诉 layout，让它累加出光标位置。
     * Wayland 下查不到全局指针坐标，只能靠这个自己算 ——
     * 这也正是点击穿透找洞位置的数据来源。 */
    if (tw->app)
        layout_note_cursor_move(tw->app, ix, iy);
}

/* ===== 轻点超时：已不再用于发左键 =====
 *
 * 原来单指轻点后会等 350ms（DOUBLE_TAP_TIMEOUT_MS）才发左键，
 * 目的是识别「双击」——第二下如果位置接近就转成拖动。
 *
 * 但这让每次单击都有三分之一秒的延迟，手感很差。
 * 现在改成「抬起立即发出左键」，双击的语义改为：
 *   - 第一下：抬起即左键
 *   - 第二下按住并移动：进入拖动（先补发按下）
 * 这更接近真实触控板，也让单击零延迟。
 *
 * 这个定时器只保留用于「双击后的第二次点击判定窗口」，
 * 不再阻塞第一次点击。 */
static gboolean on_tap_timeout(gpointer data) {
    TouchpadWidget *tw = data;
    tw->tap_timer = 0;

    /* 超时后清掉「等待第二下」的状态。
     * 此时左键早已发出，这里只是状态复位。 */
    if (tw->tap_state == TAP_STATE_SINGLE_WAIT)
        tw->tap_state = TAP_STATE_IDLE;

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
                /* 位置太远 → 视为新的一次独立点击。
                 *
                 * 注意这里不能再 inject_click：第一次点击已经在
                 * TOUCH_END 时发过了（改成零延迟后如此）。
                 * 这里只把状态复位，等这次抬起时自然走正常的
                 * 「单击」分支。 */
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
                /* 第二下没移动就松开 → 再补一次单击 */
                inject_click(tw, BTN_LEFT);
                tw->tap_state = TAP_STATE_IDLE;
            } else if (is_tap && tw->max_touches >= 2) {
                /* 双指轻点 → 右键 */
                inject_click(tw, BTN_RIGHT);
                tw->tap_state = TAP_STATE_IDLE;
            } else if (is_tap && tw->max_touches == 1) {
                /* 单指轻点 → 立刻发出左键。
                 *
                 * 这里不再等 DOUBLE_TAP_TIMEOUT_MS。原来的实现要等
                 * 350ms 才能确定「不是双击」，导致每次单击都有明显延迟。
                 * 现在抬起即点击，双击的第二下会走 DRAG_PENDING 分支
                 * 处理成拖动或再补一次点击。 */
                inject_click(tw, BTN_LEFT);

                /* 仍然进入 SINGLE_WAIT，让 350ms 内的第二下能被
                 * 识别为「双击/拖动」而不是又一次单击。 */
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

/* 销毁时撤掉未到期的定时器。
 * 点击穿透引入了两个延时源（注入前、恢复前），
 * 不清理的话到期时会解引用已释放的 tw。 */
static void touchpad_free(gpointer data) {
    TouchpadWidget *tw = data;
    if (!tw) return;

    if (tw->tap_timer)       { g_source_remove(tw->tap_timer);       tw->tap_timer = 0; }
    if (tw->hole_restore_id) {
        g_source_remove(tw->hole_restore_id);
        tw->hole_restore_id = 0;
        /* 洞还没恢复就被销毁：立刻恢复，否则输入区会一直留着缺口 */
        if (tw->app) layout_restore_hole(tw->app);
    }

    g_free(tw);
}

TouchpadWidget *touchpad_widget_new(OskMouse *mouse, App *app) {
    TouchpadWidget *tw = g_new0(TouchpadWidget, 1);
    tw->mouse = mouse;
    tw->app = app;
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
                           tw, touchpad_free);

    /* 告诉 layout 的输入区收集器：这块自绘区域要吃指针/触摸事件。
     * 否则游戏布局（透明 + 穿透）下点上去会直接落到桌面，触控板没法用。 */
    g_object_set_data(G_OBJECT(tw->area), "osk-input", GINT_TO_POINTER(1));

    return tw;
}

GtkWidget *touchpad_widget_gtk(TouchpadWidget *tw) {
    return tw ? tw->area : NULL;
}