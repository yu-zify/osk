#include "util.h"

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
    return kb;
}

GtkWidget *layout_build_row(App *app, const char *const *ids) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    for (int i = 0; ids[i]; ++i) {
        KeyButton *kb = layout_make_key(app, ids[i]);
        if (kb)
            gtk_box_pack_start(GTK_BOX(row), key_button_widget(kb),
                               TRUE, TRUE, 0);
    }
    return row;
}