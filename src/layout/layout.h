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
const LayoutOps *layout_game_ops(void);

/* 切出游戏布局时释放所有按住的键与摇杆，避免 sticky key。
 * 其他布局下调用是安全的空操作。 */
void layout_game_release_all(App *app);

/* ===== 点击穿透洞 =====
 *
 * 问题：触控板轻点时会注入一次 BTN_LEFT。如果此时光标正好压在
 * OSK 的某个控件上，这次点击会被 OSK 自己吃掉，而不是落到下层应用。
 *
 * 做法：在注入点击之前，从输入区里临时扣掉光标所在的一小块
 * （「洞」），让这次点击穿过去，注入完成后再恢复。
 *
 * 只影响「注入点击」这一瞬间，不改变静态的控件可用性 ——
 * 手指触摸触控板仍然正常，因为洞只挖在光标处，与手指位置无关。
 *
 * 时序说明（Wayland 特有）：region 变更走 socket，注入走 uinput，
 * 两条路径到达合成器的先后无法保证。所以调用方要拉开时序：
 * 挖洞 → 等一小段 → 注入 → 等一小段 → 恢复。
 */
void layout_punch_hole(App *app);   /* 按当前光标位置挖洞 */
void layout_restore_hole(App *app); /* 恢复原来的输入区 */

/* 记录光标位移。
 *
 * Wayland 下查询不到可靠的全局指针坐标（gdk_device_get_position 与
 * gdk_window_get_origin 都不可信），所以由注入方主动告知：
 * 每次 osk_mouse_move_rel() 之后调用本函数，把同样的增量记下来，
 * 累加结果就当作光标位置。
 *
 * 这依赖「光标是 OSK 自己移动的」这个前提 —— 而触控板场景恰好满足。 */
void layout_note_cursor_move(App *app, gint dx, gint dy);

/* 用真实观测到的坐标校准（例如窗口收到 motion 事件时）。
 * 物理鼠标移动、或其他程序移动光标之后可以用它纠正累计误差。 */
void layout_set_cursor_pos(App *app, gint x, gint y);

#endif