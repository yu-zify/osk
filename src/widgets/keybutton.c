#include "keybutton.h"

#define REPEAT_DELAY_MS     450
#define REPEAT_INTERVAL_MS   40
#define SWIPE_THRESHOLD      24   /* 上滑判定阈值，像素 */

struct _KeyButton {
    GtkWidget            *button;       /* GtkEventBox */
    GtkWidget            *label_main;   /* 中间：主字符（随 shift/caps 变化） */
    GtkWidget            *label_up;     /* 右上：普通状态上滑字符 */
    GtkWidget            *label_up_s;   /* 右上：Shift 状态上滑字符 */

    const KeyDef         *def;
    KeyButtonActivateFunc on_activate;
    KeyButtonSwipeFunc    on_swipe;
    gpointer              user_data;

    guint    repeat_delay;
    guint    repeat_timer;
    gboolean pressed;
    gboolean fired;           /* 本次按下是否已经发过普通键 */
    gboolean swipe_triggered;

    /* —— HOLD 模式（游戏布局用）——
     * hold_mode 为真时走「按下即按住、松手才释放」的路径，
     * 不触发连发、不响应上滑。 */
    gboolean          hold_mode;
    KeyButtonHoldFunc on_hold_press;
    KeyButtonHoldFunc on_hold_release;
    gboolean          hold_active;   /* 当前是否处于按下状态（防重复释放） */

    /* 按住拖拽（见 key_button_set_hold_drag）。
     * 上一帧的拖拽点在控件里记录，回调拿到的已经是每帧增量。 */
    KeyButtonHoldDragFunc on_hold_drag;
    gdouble               hold_last_x, hold_last_y;
    gboolean              hold_has_last;
};

static KeyButton *g_active = NULL;

/* ===== 内部状态清理 ===== */

static void key_button_reset(KeyButton *kb) {
    if (!kb) return;
    kb->pressed = FALSE;
    if (kb->repeat_delay) { g_source_remove(kb->repeat_delay); kb->repeat_delay = 0; }
    if (kb->repeat_timer) { g_source_remove(kb->repeat_timer); kb->repeat_timer = 0; }
    kb->fired = FALSE;

    /* 按压反馈类：无论走哪条路径结束，都统一在这里清掉。
     * 放在 reset 里而不是各个回调里，是因为结束路径有多条
     * （正常松手、手势取消、切布局），漏一处就会卡在白色。 */
    gtk_style_context_remove_class(
        gtk_widget_get_style_context(kb->button), "key-pressed");

    /* HOLD 模式还在按住时被复位（切布局/窗口失焦）：
     * 必须补一次 release，否则这个键会永远停在按下状态 */
    if (kb->hold_active) {
        kb->hold_active = FALSE;
        gtk_style_context_remove_class(
            gtk_widget_get_style_context(kb->button), "key-held");
        if (kb->on_hold_release)
            kb->on_hold_release(kb->def, kb->user_data);
    }

    if (kb->swipe_triggered) {
        kb->swipe_triggered = FALSE;
        gtk_style_context_remove_class(
            gtk_widget_get_style_context(kb->button), "swipe-active");
    }

    if (g_active == kb)
        g_active = NULL;
}

void key_button_stop_repeat(KeyButton *kb) {
    key_button_reset(kb);
}

void key_button_set_hold_mode(KeyButton *kb,
                               KeyButtonHoldFunc on_press,
                               KeyButtonHoldFunc on_release) {
    if (!kb) return;
    kb->hold_mode      = TRUE;
    kb->on_hold_press  = on_press;
    kb->on_hold_release = on_release;

    /* 游戏键不显示上滑角标：没有上滑功能，显示了反而误导 */
    key_button_set_up_labels(kb, NULL, NULL, FALSE);
}

void key_button_set_hold_drag(KeyButton *kb,
                               KeyButtonHoldDragFunc on_drag) {
    if (!kb) return;
    kb->on_hold_drag = on_drag;
}

/* 创建一个叠加标签 */
static GtkWidget *make_overlay_label(GtkOverlay *overlay,
                                      GtkAlign halign, GtkAlign valign,
                                      gint margin_start, gint margin_end,
                                      gint margin_top,
                                      const char *css_class) {
    GtkWidget *label = gtk_label_new("");
    gtk_widget_set_halign(label, halign);
    gtk_widget_set_valign(label, valign);
    if (margin_start) gtk_widget_set_margin_start(label, margin_start);
    if (margin_end)   gtk_widget_set_margin_end(label, margin_end);
    if (margin_top)   gtk_widget_set_margin_top(label, margin_top);
    gtk_style_context_add_class(gtk_widget_get_style_context(label), css_class);
    gtk_overlay_add_overlay(overlay, label);
    return label;
}

/* ===== 按键重复 ===== */

static gboolean repeat_tick(gpointer data) {
    KeyButton *kb = data;
    if (!kb->pressed) {
        kb->repeat_timer = 0;
        return G_SOURCE_REMOVE;
    }
    if (kb->on_activate)
        kb->on_activate(kb->def, TRUE, kb->user_data);
    return G_SOURCE_CONTINUE;
}

static gboolean repeat_begin(gpointer data) {
    KeyButton *kb = data;
    kb->repeat_delay = 0;
    if (kb->pressed && !kb->swipe_triggered) {
        if (kb->on_activate) {
            kb->on_activate(kb->def, FALSE, kb->user_data);
            kb->fired = TRUE;
        }
        kb->repeat_timer = g_timeout_add(REPEAT_INTERVAL_MS, repeat_tick, kb);
    }
    return G_SOURCE_REMOVE;
}

/* ===== 上滑触发 ===== */

static void trigger_swipe(KeyButton *kb) {
    kb->swipe_triggered = TRUE;

    if (kb->repeat_delay) { g_source_remove(kb->repeat_delay); kb->repeat_delay = 0; }
    if (kb->repeat_timer) { g_source_remove(kb->repeat_timer); kb->repeat_timer = 0; }

    gtk_style_context_add_class(
        gtk_widget_get_style_context(kb->button), "swipe-active");

    if (kb->on_swipe)
        kb->on_swipe(kb->def, kb->user_data);
}

/* ===== 手势回调 ===== */

static void on_gesture_begin(GtkGesture *g, GdkEventSequence *seq,
                             gpointer data) {
    (void)g; (void)seq;
    KeyButton *kb = data;

    /* g_active 是普通键盘「单活动键」模型（连发/上滑同一时刻只认一个键）。
     * 游戏 HOLD 键是真实按键语义，多个键必须能同时按住 ——
     * 按住 Ctrl 再按 C 就是 Ctrl+C，绝不能把别人按住的键放掉。 */
    if (g_active && g_active != kb && !kb->hold_mode)
        key_button_stop_repeat(g_active);

    kb->pressed         = TRUE;
    kb->fired           = FALSE;
    kb->swipe_triggered = FALSE;
    g_active            = kb;

    /* HOLD 模式：按下立刻生效，不排连发定时器。
     * 游戏要的是「按住多久就按住多久」，连发反而会干扰。 */
    if (kb->hold_mode) {
        kb->hold_active = TRUE;
        kb->hold_has_last = FALSE;   /* 拖拽参考点复位 */
        gtk_style_context_add_class(
            gtk_widget_get_style_context(kb->button), "key-held");
        if (kb->on_hold_press)
            kb->on_hold_press(kb->def, kb->user_data);
        return;
    }

    /* 普通按键：加一个按压类做视觉反馈。
     *
     * 不能用 CSS 的 :active 伪类 —— KeyButton 的壳是 GtkEventBox，
     * 它没有 :active 状态，那条规则永远不会命中。
     * 所以由手势自己维护这个类。 */
    gtk_style_context_add_class(
        gtk_widget_get_style_context(kb->button), "key-pressed");

    kb->repeat_delay = g_timeout_add(REPEAT_DELAY_MS, repeat_begin, kb);
}

static void on_gesture_end(GtkGesture *g, GdkEventSequence *seq,
                           gpointer data) {
    (void)g; (void)seq;
    KeyButton *kb = data;

    if (kb->hold_mode) {
        if (kb->hold_active) {
            kb->hold_active = FALSE;
            kb->hold_has_last = FALSE;
            gtk_style_context_remove_class(
                gtk_widget_get_style_context(kb->button), "key-held");
            if (kb->on_hold_release)
                kb->on_hold_release(kb->def, kb->user_data);
        }
        key_button_stop_repeat(kb);
        return;
    }

    /* 松手时如果这次按下还没发过普通键（既没有走重复，也没有上滑），
     * 才补发一次普通键，避免长按重复后松手再补一次 */
    if (!kb->swipe_triggered && !kb->fired) {
        if (kb->on_activate)
            kb->on_activate(kb->def, FALSE, kb->user_data);
        kb->fired = TRUE;
    }
    key_button_stop_repeat(kb);
}

static void on_gesture_cancel(GtkGesture *g, GdkEventSequence *seq,
                              gpointer data) {
    (void)g; (void)seq;
    KeyButton *kb = data;

    /* 手势被取消（比如窗口失焦）时也必须补一次 release，
     * 否则那一瞬间按住的键会永远停在按下状态。 */
    if (kb->hold_mode && kb->hold_active) {
        kb->hold_active = FALSE;
        kb->hold_has_last = FALSE;
        gtk_style_context_remove_class(
            gtk_widget_get_style_context(kb->button), "key-held");
        if (kb->on_hold_release)
            kb->on_hold_release(kb->def, kb->user_data);
    }
    key_button_stop_repeat(kb);
}

static void on_drag_update(GtkGestureDrag *g, gdouble ox, gdouble oy,
                           gpointer data) {
    (void)g;
    KeyButton *kb = data;

    /* HOLD 模式：拖动不是上滑，而是「按住拖拽」——
     * 把手指每帧位移交给回调（游戏布局鼠标键拖指针用）。
     * 用手势而不是 motion-notify-event 的原因见 keybutton.h。 */
    if (kb->hold_mode) {
        if (kb->hold_active && kb->on_hold_drag) {
            gdouble dx = 0, dy = 0;

            if (kb->hold_has_last) {
                dx = ox - kb->hold_last_x;
                dy = oy - kb->hold_last_y;
            }
            kb->hold_last_x = ox;
            kb->hold_last_y = oy;
            kb->hold_has_last = TRUE;

            if (dx != 0.0 || dy != 0.0)
                kb->on_hold_drag(kb->def, dx, dy, kb->user_data);
        }
        return;
    }

    if (!kb->pressed || kb->swipe_triggered) return;
    if (!kb->def->up_code) return;
    if (kb->repeat_timer) return;

    /* offset 是相对 drag-begin 位置的偏移。向上滑时 oy < 0。 */
    if (oy > -(gdouble)SWIPE_THRESHOLD) return;
    if (ABS(oy) <= ABS(ox))             return;

    trigger_swipe(kb);
}

static void key_button_free(gpointer data) {
    KeyButton *kb = data;
    if (!kb) return;
    /* 销毁过程中控件可能已在析构，只清定时器，不再操作样式 */
    if (kb->repeat_delay) { g_source_remove(kb->repeat_delay); kb->repeat_delay = 0; }
    if (kb->repeat_timer) { g_source_remove(kb->repeat_timer); kb->repeat_timer = 0; }
    if (g_active == kb)
        g_active = NULL;
    g_free(kb);
}

/* ===== 构造 ===== */

KeyButton *key_button_new(const KeyDef *def,
                           KeyButtonActivateFunc on_activate,
                           KeyButtonSwipeFunc on_swipe,
                           gpointer user_data) {
    g_return_val_if_fail(def != NULL, NULL);

    KeyButton *kb = g_new0(KeyButton, 1);
    kb->def         = def;
    kb->on_activate = on_activate;
    kb->on_swipe    = on_swipe;
    kb->user_data   = user_data;

    /* ---- 1. 用 EventBox 作为「按钮壳」 ---- */
    kb->button = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(kb->button), TRUE);
    gtk_widget_set_hexpand(kb->button, FALSE);
    gtk_widget_set_vexpand(kb->button, FALSE);
    gtk_widget_set_can_focus(kb->button, FALSE);
    gtk_widget_set_focus_on_click(kb->button, FALSE);

    gtk_style_context_add_class(gtk_widget_get_style_context(kb->button),
                                 "osk-key");

    /* ---- 2. Overlay 分层：中间主字符 + 右上两个上滑角标 ---- */
    GtkWidget *overlay = gtk_overlay_new();
    gtk_container_add(GTK_CONTAINER(kb->button), overlay);

    kb->label_main = gtk_label_new(def->label);
    gtk_widget_set_halign(kb->label_main, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(kb->label_main, GTK_ALIGN_CENTER);
    /* 按键尺寸固定，长标签（"Shift"/"Home"/"backspace"）在大字号下必然放不下。
     * 这里按字符数挂 txt1..txt5 类，由 app_apply_font 给出逐级缩小的字号，
     * 让文字自动适配键宽而不是被截断。ellipsize + max_width_chars 只作为
     * 极端情况的兜底，正常字号下不会触发。 */
    gtk_label_set_ellipsize(GTK_LABEL(kb->label_main), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(kb->label_main), 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(kb->label_main),
                               "osk-label-main");
    gtk_container_add(GTK_CONTAINER(overlay), kb->label_main);
    key_button_update_txt_class(kb);

    /* 右上角：两个上滑字符并排。普通档在左，Shift 档在右。 */
    GtkWidget *corner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_halign(corner, GTK_ALIGN_END);
    gtk_widget_set_valign(corner, GTK_ALIGN_START);
    gtk_widget_set_margin_end(corner, 4);
    gtk_widget_set_margin_top(corner, 1);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), corner);

    kb->label_up = gtk_label_new("");
    gtk_style_context_add_class(gtk_widget_get_style_context(kb->label_up),
                               "osk-label-up");
    gtk_box_pack_start(GTK_BOX(corner), kb->label_up, FALSE, FALSE, 0);

    kb->label_up_s = gtk_label_new("");
    gtk_style_context_add_class(gtk_widget_get_style_context(kb->label_up_s),
                               "osk-label-up-shift");
    gtk_box_pack_start(GTK_BOX(corner), kb->label_up_s, FALSE, FALSE, 0);

    /* 左上角：符号键的 Shift 形态（! @ # 之类） */
    if (def->klass == KEY_CLASS_CHAR && !def->is_letter &&
        def->label_shift && *def->label_shift) {
        GtkWidget *hint = make_overlay_label(GTK_OVERLAY(overlay),
                                             GTK_ALIGN_START, GTK_ALIGN_START,
                                             4, 0, 1,
                                             "osk-label-hint");
        gtk_label_set_text(GTK_LABEL(hint), def->label_shift);
    }

    /* ---- 3. 挂手势 ---- */
    GtkGesture *drag = gtk_gesture_drag_new(kb->button);
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag),
                                   GDK_BUTTON_PRIMARY);
    g_signal_connect(drag, "begin",       G_CALLBACK(on_gesture_begin),  kb);
    g_signal_connect(drag, "end",         G_CALLBACK(on_gesture_end),    kb);
    g_signal_connect(drag, "cancel",      G_CALLBACK(on_gesture_cancel), kb);
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update),    kb);
    g_object_set_data_full(G_OBJECT(kb->button), "osk-drag",
                           drag, g_object_unref);

    /* ---- 4. 生命周期管理 ---- */
    g_object_set_data_full(G_OBJECT(kb->button), "osk-keybutton",
                           kb, key_button_free);

    /* 初始化角标（构造时就知道两档符号） */
    key_button_set_up_labels(kb, def->label_up, def->label_up_shift, FALSE);

    return kb;
}

GtkWidget *key_button_widget(KeyButton *kb) {
    return kb ? kb->button : NULL;
}

static void set_small_label(GtkWidget *label, const char *text) {
    if (!label) return;
    gboolean show = text && *text;
    gtk_label_set_text(GTK_LABEL(label), show ? text : "");
    /* 空标签不进 show_all，避免占位；有内容时显式显示 */
    gtk_widget_set_no_show_all(label, !show);
    if (show)
        gtk_widget_show(label);
    else
        gtk_widget_hide(label);
}

/* 按主标签的字符数切换 txt1..txt5 类，供 app_apply_font 给出逐级字号。
 * 需要按字节数估算宽度：ASCII 与全角符号混排，这里用 g_utf8_strlen 取字符数。 */
void key_button_update_txt_class(KeyButton *kb) {
    if (!kb || !kb->label_main) return;

    GtkStyleContext *ctx = gtk_widget_get_style_context(kb->label_main);
    for (int i = 1; i <= 5; ++i) {
        char cls[8];
        g_snprintf(cls, sizeof(cls), "txt%d", i);
        gtk_style_context_remove_class(ctx, cls);
    }

    const char *text = gtk_label_get_text(GTK_LABEL(kb->label_main));
    glong n = text ? g_utf8_strlen(text, -1) : 1;

    int idx = (int)CLAMP(n, 1, 5);
    char cls[8];
    g_snprintf(cls, sizeof(cls), "txt%d", idx);
    gtk_style_context_add_class(ctx, cls);
}

void key_button_set_label(KeyButton *kb, const char *text) {
    if (!kb || !kb->label_main || !text) return;
    gtk_label_set_text(GTK_LABEL(kb->label_main), text);
    key_button_update_txt_class(kb);
}

void key_button_set_up_labels(KeyButton *kb,
                              const char *up,
                              const char *up_shift,
                              gboolean highlight_shift) {
    if (!kb) return;

    set_small_label(kb->label_up, up);
    set_small_label(kb->label_up_s, up_shift);

    /* 两档符号同时可见，用亮度标明当前上滑会输出哪一个 */
    GtkStyleContext *c1 = gtk_widget_get_style_context(kb->label_up);
    GtkStyleContext *c2 = gtk_widget_get_style_context(kb->label_up_s);

    gtk_style_context_remove_class(c1, "up-current");
    gtk_style_context_remove_class(c2, "up-current");

    if (highlight_shift && up_shift && *up_shift)
        gtk_style_context_add_class(c2, "up-current");
    else if (up && *up)
        gtk_style_context_add_class(c1, "up-current");
}

void key_button_set_fixed_size(KeyButton *kb, gint width, gint height) {
    if (!kb) return;
    gtk_widget_set_size_request(kb->button, width, height);
    if (width > 0)  gtk_widget_set_hexpand(kb->button, FALSE);
    if (height > 0) gtk_widget_set_vexpand(kb->button, FALSE);
}

void key_button_set_active_class(KeyButton *kb,
                                 const char *class_name,
                                 gboolean active) {
    if (!kb || !class_name) return;
    GtkStyleContext *ctx = gtk_widget_get_style_context(kb->button);
    if (active)
        gtk_style_context_add_class(ctx, class_name);
    else
        gtk_style_context_remove_class(ctx, class_name);
}

const KeyDef *key_button_def(const KeyButton *kb) {
    return kb ? kb->def : NULL;
}
