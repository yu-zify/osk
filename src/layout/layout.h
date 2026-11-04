#ifndef OSK_LAYOUT_H
#define OSK_LAYOUT_H

#include <gtk/gtk.h>
#include "app.h"

typedef struct _LayoutOps {
    const char *name;
    const char *display_name;
    gboolean    transparent;          /* 输入穿透 */
    gboolean    visual_transparent;   /* 视觉透明（窗口背景透明） */
    gboolean    hide_drag;            /* 顶栏不显示拖动区（全屏/游戏这类铺满布局） */
    gint        pages;                /* 页数，1 表示单页 */
    GtkWidget *(*create)(App *app);   /* 按 app->page 创建对应页 */
} LayoutOps;

const LayoutOps *layout_game_ops(void);   /* 新增 */

const LayoutOps *layout_lookup(const char *name);

/* 布局切换后保持页号在有效范围内；返回是否改变了 app->page */
gboolean layout_clamp_page(App *app);

/* 根据 transparent 标志设置窗口的 input region（异步，会排一次延时任务） */
void layout_apply_input_region(App *app, gboolean transparent);

/* 立即重算并应用 input region。窗口每次 resize 后都必须同步调用，
 * 否则 GTK 会把 input shape 重置成整个窗口，穿透失效。 */
void layout_apply_input_region_now(App *app);

const LayoutOps *layout_standard_ops(void);
const LayoutOps *layout_minimal_ops(void);
const LayoutOps *layout_fullscreen_ops(void);

#endif