#include "app.h"

#define SHIFT_CLICK_WINDOW_MS 400   /* 双击/三击判定窗口 */

static gboolean shift_active(const App *app) {
    return app->shift_state != SHIFT_OFF;
}


static void update_modifier_label(KeyButton *kb, const char *name,
                                   gboolean active) {
    if (!kb) return;
    char buf[32];
    g_snprintf(buf, sizeof(buf), active ? "● %s" : "  %s", name);
    key_button_set_label(kb, buf);
}

static gboolean shift_click_timeout(gpointer data) {
    App *app = data;
    app->shift_click_timer = 0;
    app->shift_click_count = 0;   /* 只重置计数，状态保留 */
    return G_SOURCE_REMOVE;
}

static void on_shift_clicked(App *app) {
    if (app->shift_click_timer) {
        g_source_remove(app->shift_click_timer);
        app->shift_click_timer = 0;
    }

    app->shift_click_count++;

    switch (app->shift_click_count) {
    case 1:
        app->shift_state = SHIFT_ONESHOT;
        break;
    case 2:
        app->shift_state = SHIFT_LOCKED;
        break;
    default:
        app->shift_state = SHIFT_OFF;
        app->shift_click_count = 0;
        break;
    }

    /* 状态不是 OFF 才启动窗口计时器 */
    if (app->shift_state != SHIFT_OFF) {
        app->shift_click_timer = g_timeout_add(SHIFT_CLICK_WINDOW_MS,
                                                shift_click_timeout, app);
    }

    app_refresh_labels(app);
}

static void on_modifier_clicked(App *app, const KeyDef *def) {
    switch (def->modifier_bit) {
    case OSK_MOD_SHIFT:
        on_shift_clicked(app);
        return;   /* on_shift_clicked 内部已经 refresh 了 */
    case OSK_MOD_CTRL:  app->ctrl  = !app->ctrl;  break;
    case OSK_MOD_ALT:   app->alt   = !app->alt;   break;
    case OSK_MOD_META:  app->meta  = !app->meta;  break;
    case OSK_MOD_CAPS:  app->caps  = !app->caps;  break;
    default: return;
    }
    app_refresh_labels(app);
}

static const char *key_label(const KeyDef *def, const App *app) {
    if (!def->label_shift)
        return def->label;
    gboolean use_shift;
    if (def->klass == KEY_CLASS_CHAR && def->is_letter)
        use_shift = shift_active(app) != app->caps;
    else
        use_shift = shift_active(app);
    return use_shift ? def->label_shift : def->label;
}

static gsize collect_modifiers(const App *app, const KeyDef *def,
                                unsigned short *mods) {
    gsize n = 0;
    gboolean need_shift;
    if (def->klass == KEY_CLASS_CHAR && def->is_letter)
        need_shift = shift_active(app) != app->caps;
    else
        need_shift = shift_active(app);

    if (need_shift) mods[n++] = KEY_LEFTSHIFT;
    if (app->ctrl)  mods[n++] = KEY_LEFTCTRL;
    if (app->alt)   mods[n++] = KEY_LEFTALT;
    if (app->meta)  mods[n++] = KEY_LEFTMETA;
    return n;
}
/* ===== 公共接口 ===== */

void app_on_key_activate(const KeyDef *def, gboolean is_repeat,
                          gpointer user_data) {
    App *app = user_data;

    if (def->action == KEY_ACTION_MODIFIER) {
        if (!is_repeat)
            on_modifier_clicked(app, def);
        return;
    }

    unsigned short mods[4];
    gsize n = collect_modifiers(app, def, mods);

    osk_keyboard_chord(app->kbd, def->code, mods, n);

    if (is_repeat)
        g_print("[OSK] 重复 %s\n", def->id);
    else
        g_print("[OSK] 发送 %s（%zu 个修饰键）\n", def->id, n);

    /* ONESHOT 在输入一个字符/功能键后自动关闭 */
if (app->shift_state == SHIFT_ONESHOT &&
    (def->klass == KEY_CLASS_CHAR || def->klass == KEY_CLASS_FUNCTION)) {
    app->shift_state = SHIFT_OFF;
    app->shift_click_count = 0;
    if (app->shift_click_timer) {
        g_source_remove(app->shift_click_timer);
        app->shift_click_timer = 0;
    }
    app_refresh_labels(app);
}
}

void app_register_key_button(App *app, KeyButton *kb) {
    if (!app || !kb) return;

    g_ptr_array_add(app->key_buttons, kb);

    const KeyDef *def = key_button_def(kb);

    switch (def->modifier_bit) {
    case OSK_MOD_SHIFT:
        if (app->shift_btn_count < 2)
            app->shift_btns[app->shift_btn_count++] = kb;
        break;
    case OSK_MOD_CTRL: app->ctrl_btn = kb; break;
    case OSK_MOD_ALT:  app->alt_btn  = kb; break;
    case OSK_MOD_META: app->meta_btn = kb; break;
    case OSK_MOD_CAPS: app->caps_btn = kb; break;
    }
}

static void update_shift_button(KeyButton *kb, ShiftState state) {
    if (!kb) return;
    key_button_set_label(kb, "Shift");

    GtkWidget *w = key_button_widget(kb);
    GtkStyleContext *ctx = gtk_widget_get_style_context(w);
    gtk_style_context_remove_class(ctx, "mod-active");
    gtk_style_context_remove_class(ctx, "mod-locked");

    if (state == SHIFT_ONESHOT)
        gtk_style_context_add_class(ctx, "mod-active");
    else if (state == SHIFT_LOCKED)
        gtk_style_context_add_class(ctx, "mod-locked");
}


    
void app_refresh_labels(App *app) {
    if (!app) return;

    /* 字符键的三个标签在 key_button_new 时就已经设好，不再变化。
     * 只有修饰键按钮需要根据 shift/caps/ctrl/... 状态更新显示。 */
/* 在 app_refresh_labels 里： */
for (gsize i = 0; i < app->shift_btn_count; ++i)
    update_shift_button(app->shift_btns[i], app->shift_state);
    update_modifier_label(app->caps_btn, "Caps", app->caps);
    update_modifier_label(app->ctrl_btn, "Ctrl", app->ctrl);
    update_modifier_label(app->alt_btn,  "Alt",  app->alt);
    update_modifier_label(app->meta_btn, "Meta", app->meta);
}

void app_on_key_swipe(const KeyDef *def, gpointer user_data) {
    App *app = user_data;
    if (!def || !def->up_code) return;

    /* 上滑不叠加 Shift（上滑字符本身已经是一个明确的符号），
     * 但保留 Ctrl/Alt/Meta。 */
    unsigned short mods[4];
    gsize n = 0;
    if (app->ctrl) mods[n++] = KEY_LEFTCTRL;
    if (app->alt)  mods[n++] = KEY_LEFTALT;
    if (app->meta) mods[n++] = KEY_LEFTMETA;

    osk_keyboard_chord(app->kbd, def->up_code, mods, n);
    g_print("[OSK] 上滑 %s → 0x%X\n", def->id, def->up_code);
}