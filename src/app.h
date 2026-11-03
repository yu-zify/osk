#ifndef OSK_APP_H
#define OSK_APP_H

#include <gtk/gtk.h>
#include "backend/uinput_keyboard.h"
#include "keylib/key.h"
#include "widgets/keybutton.h"
#include "backend/uinput_mouse.h"

/* 前向声明，避免循环包含 */
typedef struct _LayoutOps LayoutOps;

typedef enum {
    SHIFT_OFF,       /* 小写 */
    SHIFT_ONESHOT,   /* 下一个字符大写，然后自动关闭 */
    SHIFT_LOCKED,    /* 持续大写，直到手动关闭 */
} ShiftState;

typedef struct {
    OskKeyboard *kbd;
    OskMouse    *mouse;

    ShiftState shift_state;        /* 替代原来的 gboolean shift */
    guint      shift_click_timer;  /* 双击/三击检测定时器 */
    gint       shift_click_count;  /* 当前连续点击计数 */

    gboolean ctrl, alt, meta, caps;

    GPtrArray *key_buttons;
    KeyButton *shift_btns[2];
    gsize      shift_btn_count;
    KeyButton *ctrl_btn, *alt_btn, *meta_btn, *caps_btn;

    GtkWidget       *window;
    GtkWidget       *layout_host;
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