#ifndef OSK_STICK_H
#define OSK_STICK_H

#include <gtk/gtk.h>
#include "backend/uinput_gamepad.h"
#include "backend/uinput_keyboard.h"

typedef struct _StickWidget StickWidget;

/* 摇杆的四个方向。顺序与 GpItem.dir_id 一致 */
typedef enum {
    STICK_DIR_UP,
    STICK_DIR_DOWN,
    STICK_DIR_LEFT,
    STICK_DIR_RIGHT,
    STICK_DIR_COUNT
} StickDir;

/* 创建一个虚拟摇杆。
 * side 为 TRUE 表示右摇杆（映射到 RX/RY），FALSE 表示左摇杆（X/Y）。
 * 拖动摇杆头时持续输出轴值，松手自动回中。
 *
 * kbd 用于方向键绑定：为 NULL 时只输出轴，不触发任何按键。 */
StickWidget *stick_widget_new(OskGamepad *gp, gboolean right_side,
                               OskKeyboard *kbd);

GtkWidget *stick_widget_gtk(StickWidget *tw);

/* 为某个方向绑定一个键盘键码（0 = 解除绑定）。
 * 推杆超过阈值时按下，回到死区时松开。
 * 两套输出（ABS 轴 + 方向键）同时生效，互不干扰。 */
void stick_widget_bind_dir(StickWidget *tw, StickDir dir,
                            unsigned short code);

/* 解除全部方向绑定 */
void stick_widget_clear_binds(StickWidget *tw);

/* 八向支持。
 *
 *   关闭（默认）：斜推只触发主轴方向，右上只按 W 或 D 中的一个。
 *   开启：每个轴独立判定，右上会同时按 W 和 D —— 适合需要斜向移动的游戏。
 *
 * 只影响方向键绑定，不影响 ABS 轴输出。 */
void     stick_widget_set_eight_way(StickWidget *tw, gboolean enable);
gboolean stick_widget_get_eight_way(StickWidget *tw);

/* 底盘背景透明度，0 = 全透明，1 = 不透明。默认 0.55。 */
void    stick_widget_set_bg_alpha(StickWidget *tw, gdouble alpha);
gdouble stick_widget_get_bg_alpha(StickWidget *tw);

/* 强制回中（切布局、失焦时调用，避免角色一直朝某个方向跑） */
void stick_widget_center(StickWidget *tw);

#endif
