#include "stick.h"

#include <math.h>

/* 摇杆头半径占控件半径的比例 */
#define KNOB_RADIUS_RATIO 0.34
/* 死区：像素比例。小于这个偏移视为居中，避免手指抖动导致视角漂移 */
#define DEAD_ZONE_RATIO   0.08

struct _StickWidget {
    GtkWidget  *area;
    OskGamepad *gp;
    OskKeyboard *kbd;             /* 方向键绑定用；可能为 NULL */
    gboolean    right_side;

    gdouble center_x, center_y;
    gdouble knob_x,   knob_y;      /* 摇杆头当前位置（相对控件） */
    gboolean dragging;

    /* 当前操作本摇杆的那根手指的触摸序列。
     *
     * 多指同时操作的关键：鼠标指针只有一个，用 GdkEventMotion 时
     * 两个摇杆会抢同一个指针，表现就是「只能动一个」。
     * 触摸屏上每根手指有独立的 sequence，用它区分归属即可。
     * 为 NULL 表示当前没有手指按在本摇杆上（鼠标调试走单独路径）。 */
    GdkEventSequence *touch_seq;

    /* 八向模式：开启后斜向会同时触发两个方向的键（比如右上同时按 W+D）。
     * 关闭时只取主轴，斜推只触发一个方向。 */
    gboolean eight_way;

    /* 四方向键位绑定：0 表示该方向不绑 */
    unsigned short dir_code[STICK_DIR_COUNT];
    /* 各方向当前是否已按下，避免重复发送 down/up */
    gboolean       dir_held[STICK_DIR_COUNT];

    /* 背景透明度（0..1），由编辑面板设置 */
    gdouble bg_alpha;
};

static void compute_center(StickWidget *sw) {
    GtkAllocation a;
    gtk_widget_get_allocation(sw->area, &a);
    sw->center_x = a.width  / 2.0;
    sw->center_y = a.height / 2.0;
}

/* 基座半径 = 控件短边一半再留 2px 边距。
 * 绘制、命中换算、摇杆头半径都以它为准，三处不能各算各的，
 * 否则摇杆头会跑到圈外或者拖不到边缘。 */
static gdouble base_radius(StickWidget *sw) {
    GtkAllocation a;
    gtk_widget_get_allocation(sw->area, &a);
    gdouble r = MIN(a.width, a.height) / 2.0 - 2.0;
    return r > 0 ? r : 0;
}

/* 把归一化坐标 (-1..1) 换算成四方向的按下状态并发按键。
 *
 * 两种模式：
 *   四向（默认）：只取主轴。斜推 45° 时取分量大的那个轴，
 *                 所以右上只会触发 W 或 D 中的一个。
 *   八向（eight_way）：每个轴独立判定。斜推会同时触发两个方向，
 *                 右上同时按 W 和 D —— 适合需要斜向移动的游戏。
 *
 * 八向判定额外要求该轴分量超过一个较小的阈值，
 * 否则接近水平的推动会误触发上下键。 */
static void update_dir_keys(StickWidget *sw, gdouble nx, gdouble ny) {
    if (!sw->kbd) return;

    gboolean want[STICK_DIR_COUNT] = { FALSE, FALSE, FALSE, FALSE };

    /* 用与轴相同的死区作为触发阈值，保证「轴动了键才动」 */
    const gdouble th = DEAD_ZONE_RATIO;

    gdouble ax = fabs(nx);
    gdouble ay = fabs(ny);

    if (sw->eight_way) {
        /* 八向：每个轴独立判定，但要求该轴明显强于另一轴的一半，
         * 避免轻微抖动就触发两个方向。 */
        if (ay >= th)
            want[ny < 0 ? STICK_DIR_UP : STICK_DIR_DOWN] = TRUE;
        if (ax >= th)
            want[nx < 0 ? STICK_DIR_LEFT : STICK_DIR_RIGHT] = TRUE;
    } else {
        /* 四向：只取主轴，斜推只触发一个方向 */
        if (ay >= th && ay >= ax) {
            if (ny < 0) want[STICK_DIR_UP]   = TRUE;
            else        want[STICK_DIR_DOWN] = TRUE;
        } else if (ax >= th && ax > ay) {
            if (nx < 0) want[STICK_DIR_LEFT]  = TRUE;
            else        want[STICK_DIR_RIGHT] = TRUE;
        }
    }

    for (int d = 0; d < STICK_DIR_COUNT; ++d) {
        if (want[d] == sw->dir_held[d]) continue;   /* 状态没变，不发事件 */

        unsigned short code = sw->dir_code[d];
        if (code == 0) {
            /* 该方向没绑键，仍然记录状态，避免反复判断 */
            sw->dir_held[d] = want[d];
            continue;
        }

        if (want[d])
            osk_keyboard_key_down(sw->kbd, code);
        else
            osk_keyboard_key_up(sw->kbd, code);

        sw->dir_held[d] = want[d];
    }
}

static void emit_axis(StickWidget *sw) {
    gdouble radius = base_radius(sw);
    if (radius <= 0) return;

    gdouble dx = sw->knob_x - sw->center_x;
    gdouble dy = sw->knob_y - sw->center_y;

    /* 限制在圆内，斜向拖动不会超出量程 */
    gdouble dist = sqrt(dx * dx + dy * dy);
    if (dist > radius) {
        dx = dx / dist * radius;
        dy = dy / dist * radius;
    }

    gdouble nx = dx / radius;
    gdouble ny = dy / radius;

    if (sqrt(nx * nx + ny * ny) < DEAD_ZONE_RATIO) {
        nx = 0;
        ny = 0;
    }

    /* 两套输出并存：先发模拟轴，再处理方向键 */
    gint ax = (gint)(nx * 32767);
    gint ay = (gint)(ny * 32767);

    if (sw->right_side)
        osk_gamepad_set_stick_right(sw->gp, ax, ay);
    else
        osk_gamepad_set_stick_left(sw->gp, ax, ay);

    update_dir_keys(sw, nx, ny);
}

/* ===== 绘制 ===== */

static gboolean on_draw(GtkWidget *w, cairo_t *cr, gpointer data) {
    (void)w;
    StickWidget *sw = data;

    gdouble radius = base_radius(sw);
    if (radius <= 0) return TRUE;

    /* 底盘：透明度由 bg_alpha 控制，默认 0.55 */
    cairo_set_source_rgba(cr, 0.16, 0.17, 0.20, sw->bg_alpha);
    cairo_arc(cr, sw->center_x, sw->center_y, radius, 0, 2 * G_PI);
    cairo_fill(cr);

    /* 描边透明度跟着底盘一起变，太实会显得割裂 */
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.28 * sw->bg_alpha + 0.10);
    cairo_set_line_width(cr, 1.5);
    cairo_arc(cr, sw->center_x, sw->center_y, radius, 0, 2 * G_PI);
    cairo_stroke(cr);

    /* 十字参考线 */
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12 * sw->bg_alpha + 0.05);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, sw->center_x - radius * 0.7, sw->center_y);
    cairo_line_to(cr, sw->center_x + radius * 0.7, sw->center_y);
    cairo_move_to(cr, sw->center_x, sw->center_y - radius * 0.7);
    cairo_line_to(cr, sw->center_x, sw->center_y + radius * 0.7);
    cairo_stroke(cr);

    /* 摇杆头。位置也要按圆限制，否则 emit_axis 已经钳制过了、
     * 绘制却还画在外面，视觉与输出不一致。 */
    gdouble dx = sw->knob_x - sw->center_x;
    gdouble dy = sw->knob_y - sw->center_y;
    gdouble dist = sqrt(dx * dx + dy * dy);
    if (dist > radius) {
        dx = dx / dist * radius;
        dy = dy / dist * radius;
    }
    gdouble kx = sw->center_x + dx;
    gdouble ky = sw->center_y + dy;

    gdouble kr = radius * KNOB_RADIUS_RATIO;
    cairo_set_source_rgba(cr, 0.42, 0.46, 0.54, sw->dragging ? 0.95 : 0.75);
    cairo_arc(cr, kx, ky, kr, 0, 2 * G_PI);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.5);
    cairo_set_line_width(cr, 1.5);
    cairo_arc(cr, kx, ky, kr, 0, 2 * G_PI);
    cairo_stroke(cr);

    return TRUE;
}

/* ===== 交互 =====
 *
 * 多指同时操作是这里最关键的约束。
 *
 * 鼠标指针只有一个：用 GdkEventMotion 时，两根手指会被合成为
 * 同一个指针，两个摇杆互相抢，表现就是「只能动一个」。
 *
 * 触摸事件则每根手指有独立的 GdkEventSequence。所以这里
 * 优先走 touch-event，用 sequence 判断「这根手指是不是本摇杆的」；
 * 没有触摸设备时（桌面调试）再退回鼠标事件。
 *
 * 两套路径共用 start/move/end 三个内部函数。
 */

static void stick_begin(StickWidget *sw, gdouble x, gdouble y) {
    compute_center(sw);

    /* 手指点哪，摇杆头就摆哪 —— 比「从中心算偏移」更符合直觉 */
    sw->knob_x = x;
    sw->knob_y = y;
    sw->dragging = TRUE;

    emit_axis(sw);
    gtk_widget_queue_draw(sw->area);
}

static void stick_move(StickWidget *sw, gdouble x, gdouble y) {
    if (!sw->dragging) return;

    /* 用绝对坐标，不做累加。累加会因事件丢帧而越拖越偏。 */
    sw->knob_x = x;
    sw->knob_y = y;

    emit_axis(sw);
    gtk_widget_queue_draw(sw->area);
}

/* ---- 触摸事件（多点） ---- */

static gboolean on_touch(GtkWidget *w, GdkEvent *ev, gpointer data) {
    (void)w;
    StickWidget *sw = data;
    GdkEventTouch *te = (GdkEventTouch *)ev;

    switch (te->type) {
    case GDK_TOUCH_BEGIN:
        /* 已经有手指在操作本摇杆，忽略后来者 */
        if (sw->touch_seq) return TRUE;
        sw->touch_seq = te->sequence;
        stick_begin(sw, te->x, te->y);
        return TRUE;

    case GDK_TOUCH_UPDATE:
        if (te->sequence != sw->touch_seq) return TRUE;
        stick_move(sw, te->x, te->y);
        return TRUE;

    case GDK_TOUCH_END:
    case GDK_TOUCH_CANCEL:
        if (te->sequence != sw->touch_seq) return TRUE;
        sw->touch_seq = NULL;
        stick_widget_center(sw);
        return TRUE;

    default:
        return FALSE;
    }
}

/* ---- 鼠标事件（桌面调试 / 无触摸设备） ---- */

static gboolean on_button_press(GtkWidget *w, GdkEventButton *ev,
                                 gpointer data) {
    (void)w;
    StickWidget *sw = data;
    if (ev->button != GDK_BUTTON_PRIMARY) return FALSE;

    /* 触摸正在进行时不响应鼠标，避免同一次操作被处理两遍 */
    if (sw->touch_seq) return TRUE;

    stick_begin(sw, ev->x, ev->y);
    return TRUE;
}

static gboolean on_motion(GtkWidget *w, GdkEventMotion *ev, gpointer data) {
    (void)w;
    StickWidget *sw = data;

    if (sw->touch_seq) return TRUE;   /* 交给触摸路径 */
    stick_move(sw, ev->x, ev->y);
    return TRUE;
}

static gboolean on_button_release(GtkWidget *w, GdkEventButton *ev,
                                   gpointer data) {
    (void)w;
    StickWidget *sw = data;
    if (ev->button != GDK_BUTTON_PRIMARY) return FALSE;

    if (sw->touch_seq) return TRUE;   /* 交给触摸路径 */

    stick_widget_center(sw);
    return TRUE;
}

void stick_widget_center(StickWidget *sw) {
    if (!sw) return;
    sw->dragging = FALSE;
    sw->touch_seq = NULL;
    compute_center(sw);
    sw->knob_x = sw->center_x;
    sw->knob_y = sw->center_y;
    /* emit_axis 内部会走 update_dir_keys，把所有已按下的方向键松开 */
    emit_axis(sw);
    gtk_widget_queue_draw(sw->area);
}

void stick_widget_set_eight_way(StickWidget *sw, gboolean enable) {
    if (!sw) return;
    sw->eight_way = enable;

    /* 切换模式后立即按当前杆位重算一次，否则要等下次移动才生效 */
    emit_axis(sw);
}

gboolean stick_widget_get_eight_way(StickWidget *sw) {
    return sw ? sw->eight_way : FALSE;
}

void stick_widget_set_bg_alpha(StickWidget *sw, gdouble alpha) {
    if (!sw) return;
    sw->bg_alpha = CLAMP(alpha, 0.0, 1.0);
    gtk_widget_queue_draw(sw->area);
}

gdouble stick_widget_get_bg_alpha(StickWidget *sw) {
    return sw ? sw->bg_alpha : 0.55;
}

void stick_widget_bind_dir(StickWidget *tw, StickDir dir,
                            unsigned short code) {
    if (!tw || dir < 0 || dir >= STICK_DIR_COUNT) return;

    /* 换绑前先把旧键松开，否则旧键会一直卡在按下状态 */
    if (tw->dir_held[dir] && tw->dir_code[dir] != 0 && tw->kbd)
        osk_keyboard_key_up(tw->kbd, tw->dir_code[dir]);

    tw->dir_code[dir] = code;
    tw->dir_held[dir] = FALSE;
}

void stick_widget_clear_binds(StickWidget *tw) {
    if (!tw) return;

    /* 先松开所有仍按着的方向键，再清空绑定 */
    for (int d = 0; d < STICK_DIR_COUNT; ++d) {
        if (tw->dir_held[d] && tw->dir_code[d] != 0 && tw->kbd)
            osk_keyboard_key_up(tw->kbd, tw->dir_code[d]);
        tw->dir_code[d] = 0;
        tw->dir_held[d] = FALSE;
    }
}

static void stick_widget_free(gpointer data) {
    StickWidget *sw = data;
    if (!sw) return;
    /* 销毁时确保没有键停在按下状态 */
    if (sw->kbd) {
        for (int d = 0; d < STICK_DIR_COUNT; ++d)
            if (sw->dir_held[d] && sw->dir_code[d] != 0)
                osk_keyboard_key_up(sw->kbd, sw->dir_code[d]);
    }
    g_free(sw);
}

static gboolean on_size_allocate(GtkWidget *w, GtkAllocation *a, gpointer data) {
    (void)w; (void)a;
    StickWidget *sw = data;

    gboolean was_centered = !sw->dragging;
    compute_center(sw);

    /* 没在拖动时让摇杆头跟着中心走，窗口缩放后视觉上不会偏 */
    if (was_centered) {
        sw->knob_x = sw->center_x;
        sw->knob_y = sw->center_y;
    }
    return FALSE;
}

StickWidget *stick_widget_new(OskGamepad *gp, gboolean right_side,
                               OskKeyboard *kbd) {
    StickWidget *sw = g_new0(StickWidget, 1);
    sw->gp = gp;
    sw->kbd = kbd;
    sw->right_side = right_side;
    sw->bg_alpha = 0.55;          /* 与绘制里的默认值保持一致 */

    sw->area = gtk_drawing_area_new();
    gtk_widget_set_hexpand(sw->area, TRUE);
    gtk_widget_set_vexpand(sw->area, TRUE);
    gtk_widget_set_can_focus(sw->area, FALSE);

    /* 触摸事件必须显式开启：这是多点同时操作的唯一途径。
     * 鼠标事件保留作为桌面调试路径。 */
    gtk_widget_add_events(sw->area,
                          GDK_TOUCH_MASK          |
                          GDK_BUTTON_PRESS_MASK   |
                          GDK_BUTTON_RELEASE_MASK |
                          GDK_POINTER_MOTION_MASK);

    g_signal_connect(sw->area, "touch-event",
                     G_CALLBACK(on_touch), sw);
    g_signal_connect(sw->area, "button-press-event",
                     G_CALLBACK(on_button_press), sw);
    g_signal_connect(sw->area, "motion-notify-event",
                     G_CALLBACK(on_motion), sw);
    g_signal_connect(sw->area, "button-release-event",
                     G_CALLBACK(on_button_release), sw);

    /* 控件尺寸变化时重算中心，否则窗口拉伸后摇杆中心还是旧值 */
    g_signal_connect(sw->area, "size-allocate",
                     G_CALLBACK(on_size_allocate), sw);

    g_signal_connect(sw->area, "draw", G_CALLBACK(on_draw), sw);

    g_object_set_data_full(G_OBJECT(sw->area), "osk-stick",
                           sw, stick_widget_free);

    /* 告诉 layout 的输入区收集器：这块自绘区域要吃指针事件。
     * 没有这个标记的话，透明布局下点击会直接穿透到桌面，
     * 表现就是「摇杆拖不动」。 */
    g_object_set_data(G_OBJECT(sw->area), "osk-input", GINT_TO_POINTER(1));

    return sw;
}

GtkWidget *stick_widget_gtk(StickWidget *sw) {
    return sw ? sw->area : NULL;
}
