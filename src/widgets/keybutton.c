#include "keybutton.h"

#define REPEAT_DELAY_MS     450
#define REPEAT_INTERVAL_MS   40
#define FIRE_DELAY_MS        40   /* 首次触发的延迟窗口，让上滑有机会取消 */
#define SWIPE_THRESHOLD      24   /* 上滑判定阈值，像素 */

struct _KeyButton {
    GtkWidget            *button;       /* GtkEventBox */
    GtkWidget            *label_main;   /* 中间 */
    GtkWidget            *label_shift;  /* 右上 */
    GtkWidget            *label_up;     /* 左上 */

    const KeyDef         *def;
    KeyButtonActivateFunc on_activate;
    KeyButtonSwipeFunc    on_swipe;
    gpointer              user_data;

    guint    repeat_delay;
    guint    repeat_timer;
    gboolean pressed;
    gboolean swipe_triggered;
};

static KeyButton *g_active = NULL;

/* ===== 内部状态清理 ===== */

void key_button_stop_repeat(KeyButton *kb) {
    if (!kb) return;
    kb->pressed = FALSE;
    if (kb->repeat_delay) { g_source_remove(kb->repeat_delay); kb->repeat_delay = 0; }
    if (kb->repeat_timer) { g_source_remove(kb->repeat_timer); kb->repeat_timer = 0; }

    if (kb->swipe_triggered) {
        kb->swipe_triggered = FALSE;
        gtk_style_context_remove_class(
            gtk_widget_get_style_context(kb->button), "swipe-active");
    }

    if (g_active == kb)
        g_active = NULL;
}

/* 创建一个标签，设置对齐、margin、CSS class 并加到 overlay */
static GtkWidget *make_overlay_label(GtkOverlay *overlay,
                                      const char *text,
                                      GtkAlign halign, GtkAlign valign,
                                      gint margin_start, gint margin_end,
                                      gint margin_top,
                                      const char *css_class) {
    if (!text || !*text)
        return NULL;

    GtkWidget *label = gtk_label_new(text);
    gtk_widget_set_halign(label, halign);
    gtk_widget_set_valign(label, valign);
    if (margin_start) gtk_widget_set_margin_start(label, margin_start);
    if (margin_end)   gtk_widget_set_margin_end(label, margin_end);
    if (margin_top)   gtk_widget_set_margin_top(label, margin_top);
    gtk_style_context_add_class(gtk_widget_get_style_context(label), css_class);

    gtk_overlay_add_overlay(overlay, label);
    return label;
}



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
        /* 第一次视为「首次按下」，之后 repeat_tick 用 TRUE */
        if (kb->on_activate)
            kb->on_activate(kb->def, FALSE, kb->user_data);
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

    if (g_active && g_active != kb)
        key_button_stop_repeat(g_active);

    kb->pressed         = TRUE;
    
    kb->swipe_triggered = FALSE;
    g_active            = kb;

    
    kb->repeat_delay = g_timeout_add(REPEAT_DELAY_MS, repeat_begin, kb);
}

static void on_gesture_end(GtkGesture *g, GdkEventSequence *seq,
                           gpointer data) {
    (void)g; (void)seq;
    KeyButton *kb = data;

    /* 松手时如果还没触发上滑、也还没进入重复，就补发一次普通键 */
    if (!kb->swipe_triggered && !kb->repeat_timer) {
        if (kb->on_activate)
            kb->on_activate(kb->def, FALSE, kb->user_data);
    }
    key_button_stop_repeat(kb);
}

static void on_gesture_cancel(GtkGesture *g, GdkEventSequence *seq,
                              gpointer data) {
    (void)g; (void)seq;
    key_button_stop_repeat(data);
}

static void on_drag_update(GtkGestureDrag *g, gdouble ox, gdouble oy,
                           gpointer data) {
   // g_print("[drag] ox=%.1f oy=%.1f pressed=%d up_code=%u\n",
  //      ox, oy, kb->pressed, kb->def->up_code);
    (void)g;
    KeyButton *kb = data;
g_print("[drag] ox=%.1f oy=%.1f pressed=%d up_code=%u\n",
        ox, oy, kb->pressed, kb->def->up_code);
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
    key_button_stop_repeat(kb);
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
    gtk_widget_set_hexpand(kb->button, TRUE);
    gtk_widget_set_vexpand(kb->button, TRUE);
    gtk_widget_set_can_focus(kb->button, FALSE);
    gtk_widget_set_focus_on_click(kb->button, FALSE);

    /* 加一个 CSS class 用于样式 */
    gtk_style_context_add_class(gtk_widget_get_style_context(kb->button),
                                 "osk-key");

    /* ---- 2. 内嵌 Label ---- */
    /* ---- 2. 用 GtkOverlay 分层放三个标签 ---- */
    GtkWidget *overlay = gtk_overlay_new();
    gtk_container_add(GTK_CONTAINER(kb->button), overlay);

    /* 中间：主值。作为 GtkOverlay 的底 child，会自动填充整个区域 */
    kb->label_main = gtk_label_new(def->label);
    gtk_widget_set_halign(kb->label_main, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(kb->label_main, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(kb->label_main),
                               "osk-label-main");
    gtk_container_add(GTK_CONTAINER(overlay), kb->label_main);

    /* 右上：shift 值 */
    kb->label_shift = make_overlay_label(GTK_OVERLAY(overlay),
                                          def->label_shift,
                                          GTK_ALIGN_END, GTK_ALIGN_START,
                                          0, 3, 1,
                                          "osk-label-shift");

    /* 左上：上滑值 */
    kb->label_up = make_overlay_label(GTK_OVERLAY(overlay),
                                    def->label_up,
                                    GTK_ALIGN_START, GTK_ALIGN_START,
                                      3, 0, 1,
                                      "osk-label-up");
    /* ---- 3. 挂手势：只用 Drag，不需要 MultiPress ---- */
    GtkGesture *drag = gtk_gesture_drag_new(kb->button);
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag),
                                   GDK_BUTTON_PRIMARY);
    /* 不设 propagation phase，默认 BUBBLE 就够；EventBox 里没有别的抢 */
    g_signal_connect(drag, "begin",       G_CALLBACK(on_gesture_begin),  kb);
    g_signal_connect(drag, "end",         G_CALLBACK(on_gesture_end),    kb);
    g_signal_connect(drag, "cancel",      G_CALLBACK(on_gesture_cancel), kb);
    g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update),    kb);
    g_object_set_data_full(G_OBJECT(kb->button), "osk-drag",
                           drag, g_object_unref);

    /* ---- 4. 生命周期管理 ---- */
    g_object_set_data_full(G_OBJECT(kb->button), "osk-keybutton",
                           kb, key_button_free);

    return kb;
}

GtkWidget *key_button_widget(KeyButton *kb) {
    return kb ? kb->button : NULL;
}

void key_button_set_label(KeyButton *kb, const char *text) {
    if (kb && kb->label_main && text)
        gtk_label_set_text(GTK_LABEL(kb->label_main), text);
}

void key_button_set_shift_label(KeyButton *kb, const char *text) {
    if (!kb || !kb->label_shift) return;
    gtk_label_set_text(GTK_LABEL(kb->label_shift), text ? text : "");
}

void key_button_set_up_label(KeyButton *kb, const char *text) {
    if (!kb || !kb->label_up) return;
    gtk_label_set_text(GTK_LABEL(kb->label_up), text ? text : "");
}

const KeyDef *key_button_def(const KeyButton *kb) {
    return kb ? kb->def : NULL;
}