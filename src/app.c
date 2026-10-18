#include "app.h"


static void update_modifier_label(KeyButton *kb, const char *name,
                                   gboolean active) {
    if (!kb) return;
    char buf[32];
    g_snprintf(buf, sizeof(buf), active ? "● %s" : "  %s", name);
    key_button_set_label(kb, buf);
}

static void on_modifier_clicked(App *app, const KeyDef *def) {
    switch (def->modifier_bit) {
    case OSK_MOD_SHIFT: app->shift = !app->shift; break;
    case OSK_MOD_CTRL:  app->ctrl  = !app->ctrl;  break;
    case OSK_MOD_ALT:   app->alt   = !app->alt;   break;
    case OSK_MOD_META:  app->meta  = !app->meta;  break;
    case OSK_MOD_CAPS:  app->caps  = !app->caps;  break;
    default: return;
    }
    app_refresh_labels(app);
}

static gsize collect_modifiers(const App *app, const KeyDef *def,
                                unsigned short *mods) {
    gsize n = 0;
    gboolean need_shift;
    if (def->klass == KEY_CLASS_CHAR && def->is_letter)
        need_shift = app->shift != app->caps;
    else
        need_shift = app->shift;

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

    if (def->klass == KEY_CLASS_CHAR && app->shift) {
        app->shift = FALSE;
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

void app_refresh_labels(App *app) {
    if (!app) return;

    /* 字符键的三个标签在 key_button_new 时就已经设好，不再变化。
     * 只有修饰键按钮需要根据 shift/caps/ctrl/... 状态更新显示。 */
    for (gsize i = 0; i < app->shift_btn_count; ++i)
        update_modifier_label(app->shift_btns[i], "Shift", app->shift);
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