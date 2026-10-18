#ifndef OSK_LAYOUT_H
#define OSK_LAYOUT_H

#include <gtk/gtk.h>
#include "app.h"

typedef struct _LayoutOps {
    const char *name;
    const char *display_name;
    gboolean    transparent;          /* 输入穿透 */
    gboolean    visual_transparent;   /* 视觉透明（窗口背景透明） */
    GtkWidget *(*create)(App *app);
} LayoutOps;

const LayoutOps *layout_game_ops(void);   /* 新增 */

const LayoutOps *layout_lookup(const char *name);

/* 根据 transparent 标志设置窗口的 input region */
void layout_apply_input_region(App *app, gboolean transparent);

const LayoutOps *layout_standard_ops(void);
const LayoutOps *layout_minimal_ops(void);
const LayoutOps *layout_fullscreen_ops(void);

#endif