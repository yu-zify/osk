#include "util.h"

static void apply_key_size(App *app, KeyButton *kb, int span) {
    if (!kb) return;
    gint w = app->config.key_width;
    gint h = app->config.key_height;
    if (w > 0) w = w * span + app->config.key_spacing * (span - 1);
    key_button_set_fixed_size(kb, w, h);
}

KeyButton *layout_make_key(App *app, const char *id) {
    const KeyDef *def = keylib_get(id);
    if (!def) {
        g_printerr("[OSK] 未定义的键：%s\n", id);
        return NULL;
    }
    KeyButton *kb = key_button_new(def,
                                    app_on_key_activate,
                                    app_on_key_swipe,
                                    app);
    app_register_key_button(app, kb);
    apply_key_size(app, kb, 1);
    return kb;
}

/* 键 id 支持 "space*4" 这种倍数写法，用于空格之类的宽键 */
static KeyButton *layout_make_key_sized(App *app, const char *token) {
    char buf[32];
    int span = 1;

    const char *star = strchr(token, '*');
    if (star) {
        gsize n = (gsize)(star - token);
        if (n >= sizeof(buf)) n = sizeof(buf) - 1;
        memcpy(buf, token, n);
        buf[n] = '\0';
        span = atoi(star + 1);
        if (span < 1) span = 1;
        token = buf;
    }

    KeyButton *kb = layout_make_key(app, token);
    apply_key_size(app, kb, span);
    return kb;
}

GtkWidget *layout_build_row(App *app, const char *const *ids) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,
                                 app->config.key_spacing);
    for (int i = 0; ids[i]; ++i) {
        KeyButton *kb = layout_make_key_sized(app, ids[i]);
        if (kb)
            gtk_box_pack_start(GTK_BOX(row), key_button_widget(kb),
                               FALSE, FALSE, 0);
    }
    return row;
}
