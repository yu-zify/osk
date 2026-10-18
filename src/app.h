#ifndef OSK_APP_H
#define OSK_APP_H

#include <gtk/gtk.h>
#include "backend/uinput_keyboard.h"
#include "keylib/key.h"
#include "widgets/keybutton.h"
#include "backend/uinput_touchpad.h"

/* 前向声明，避免循环包含 */
typedef struct _LayoutOps LayoutOps;

typedef struct {
    /* 输入后端 */
    OskKeyboard *kbd;
    OskTouchpad *touchpad;
    /* 修饰键状态 */
    gboolean shift, ctrl, alt, meta, caps;

    /* 所有按键控件，用于批量刷新标签 */
    GPtrArray *key_buttons;

    /* 修饰键按钮引用（Shift 可能有多个） */
    KeyButton *shift_btns[2];
    gsize      shift_btn_count;
    KeyButton *ctrl_btn, *alt_btn, *meta_btn, *caps_btn;

    /* 窗口 + 布局挂载点 + 当前布局 */
    GtkWidget       *window;
    GtkWidget       *layout_host;   /* GtkBox，所有布局 widget 挂这里 */
    const LayoutOps *layout;
} App;

/* —— 供 layout / widgets 调用的公共接口 —— */

/* 按键被激活时调用（匹配 KeyButtonActivateFunc） */
void app_on_key_activate(const KeyDef *def, gboolean is_repeat,
                          gpointer user_data);

/* 注册一个新建的 KeyButton，记录到 key_buttons 并登记修饰键按钮 */
void app_register_key_button(App *app, KeyButton *kb);

/* 根据 shift/caps 状态刷新所有标签 */
void app_refresh_labels(App *app);

void app_on_key_swipe(const KeyDef *def, gpointer user_data);   

#endif