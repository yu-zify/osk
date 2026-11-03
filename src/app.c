#include "app.h"

static gboolean shift_active(const App *app) {
    return app->shift_state != SHIFT_OFF;
}

/* Shift 或 Caps 任一激活时，字母键显示/输入大写 */
static gboolean want_upper(const App *app) {
    return shift_active(app) || app->caps;
}

/* ===== 修饰键外观 =====
 * 统一用按钮颜色表示激活状态，不再在按钮上显示圆点。
 *   Shift 单次  → 蓝色 (mod-oneshot)
 *   Shift 锁定 / Caps / Ctrl / Alt / Meta 激活 → 红色 (mod-locked)
 */
static void update_shift_button(KeyButton *kb, ShiftState state) {
    if (!kb) return;
    key_button_set_label(kb, "Shift");
    key_button_set_active_class(kb, "mod-oneshot", state == SHIFT_ONESHOT);
    key_button_set_active_class(kb, "mod-locked",  state == SHIFT_LOCKED);
}

static void update_plain_modifier(KeyButton *kb, const char *name,
                                   gboolean active) {
    if (!kb) return;
    key_button_set_label(kb, name);
    key_button_set_active_class(kb, "mod-locked", active);
}

/* ===== 按键行为 ===== */

/* Shift 三态循环：
 *   点一下 → 蓝色，单次大写（输入一个字符后自动恢复）
 *   点两下 → 红色，锁定大写
 *   点三下 → 恢复普通颜色，回到小写
 */
static void on_shift_clicked(App *app) {
    switch (app->shift_state) {
    case SHIFT_OFF:     app->shift_state = SHIFT_ONESHOT; break;
    case SHIFT_ONESHOT: app->shift_state = SHIFT_LOCKED;  break;
    case SHIFT_LOCKED:
    default:            app->shift_state = SHIFT_OFF;     break;
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

/* 主键是否需要与 Shift 一起发送 */
static gboolean need_shift_mod(const App *app, const KeyDef *def) {
    if (def->klass == KEY_CLASS_CHAR) {
        if (def->is_letter) {
            /* 字母：Shift 或 Caps 任一激活即为大写 */
            return want_upper(app);
        }
        /* 数字/符号：只有 Shift 生效时才需要，Caps 不影响 */
        return shift_active(app);
    }
    return shift_active(app);
}

static gsize collect_modifiers(const App *app, const KeyDef *def,
                                unsigned short *mods) {
    gsize n = 0;

    if (need_shift_mod(app, def)) mods[n++] = KEY_LEFTSHIFT;
    if (app->ctrl)  mods[n++] = KEY_LEFTCTRL;
    if (app->alt)   mods[n++] = KEY_LEFTALT;
    if (app->meta)  mods[n++] = KEY_LEFTMETA;
    return n;
}

/* 单次大写：用完一个字符就回到小写 */
static void consume_oneshot(App *app) {
    if (app->shift_state != SHIFT_ONESHOT)
        return;
    app->shift_state = SHIFT_OFF;
}

/* 字母键主标签随 shift/caps 切换大小写。
 * 右上角两档上滑角标由 keybutton 维护，这里只更新高亮。 */
static void update_char_button(App *app, KeyButton *kb) {
    const KeyDef *def = key_button_def(kb);
    if (!def || def->klass != KEY_CLASS_CHAR) return;

    gboolean upper = want_upper(app);

    if (def->is_letter && def->label_shift) {
        key_button_set_label(kb, upper ? def->label_shift : def->label);
        key_button_set_active_class(kb, "key-upper", upper);
    }

    /* 上滑是否走 Shift 档：字母键跟随 shift/caps，其他跟随 Shift */
    gboolean upper_swipe = def->is_letter ? upper : shift_active(app);
    key_button_set_up_labels(kb, def->label_up, def->label_up_shift,
                             upper_swipe);
}

/* ===== 公共接口 ===== */

void app_apply_config(App *app) {
    if (!app || !app->window) return;
    gtk_widget_set_opacity(app->window, app->config.opacity);
}

void app_on_key_activate(const KeyDef *def, gboolean is_repeat,
                          gpointer user_data) {
    App *app = user_data;
    if (!app || !def) return;

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

    /* 输入一个字符/功能键后，单次大写自动关闭 */
    if (def->klass == KEY_CLASS_CHAR || def->klass == KEY_CLASS_FUNCTION) {
        if (app->shift_state == SHIFT_ONESHOT) {
            consume_oneshot(app);
            app_refresh_labels(app);
        }
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

    /* 字符键：主标签大小写 + 上滑角标高亮 */
    if (app->key_buttons) {
        for (gsize i = 0; i < app->key_buttons->len; ++i)
            update_char_button(app, g_ptr_array_index(app->key_buttons, i));
    }

    for (gsize i = 0; i < app->shift_btn_count; ++i)
        update_shift_button(app->shift_btns[i], app->shift_state);

    update_plain_modifier(app->caps_btn, "Caps", app->caps);
    update_plain_modifier(app->ctrl_btn, "Ctrl", app->ctrl);
    update_plain_modifier(app->alt_btn,  "Alt",  app->alt);
    update_plain_modifier(app->meta_btn, "Meta", app->meta);
}

void app_on_key_swipe(const KeyDef *def, gpointer user_data) {
    App *app = user_data;
    if (!app || !def || !def->up_code) return;

    /* 上滑输出的是明确目标字符，按当前 Shift 档位二选一：
     *   - 普通档：label_up / up_code / up_shift
     *   - Shift 档：label_up_shift / up_code_shift / up_shift_shift
     * 哪一档生效和按钮右上角的高亮一致。 */
    gboolean upper = (def->klass == KEY_CLASS_CHAR && def->is_letter)
                     ? want_upper(app)
                     : shift_active(app);

    unsigned short code    = def->up_code;
    gboolean       need_sh = def->up_shift;
    const char    *label   = def->label_up;

    if (upper && def->up_code_shift) {
        code    = def->up_code_shift;
        need_sh = def->up_shift_shift;
        label   = def->label_up_shift;
    }

    unsigned short mods[4];
    gsize n = 0;
    if (need_sh)    mods[n++] = KEY_LEFTSHIFT;
    if (app->ctrl)  mods[n++] = KEY_LEFTCTRL;
    if (app->alt)   mods[n++] = KEY_LEFTALT;
    if (app->meta)  mods[n++] = KEY_LEFTMETA;

    osk_keyboard_chord(app->kbd, code, mods, n);
    g_print("[OSK] 上滑 %s → '%s'（%zu 个修饰键）\n",
            def->id, label ? label : "?", n);

    /* 上滑同样消耗单次 Shift */
    if (app->shift_state == SHIFT_ONESHOT) {
        consume_oneshot(app);
        app_refresh_labels(app);
    }
}
