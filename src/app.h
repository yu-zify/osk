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
    SHIFT_OFF,       /* 小写，按钮默认颜色 */
    SHIFT_ONESHOT,   /* 下一个字符大写，然后自动关闭；按钮红色 */
    SHIFT_LOCKED,    /* 持续大写，直到手动关闭；按钮蓝色 */
} ShiftState;

/* 可在设置里调整的外观参数 */
typedef struct {
    gint    key_width;      /* 按键固定宽度（像素） */
    gint    key_height;     /* 按键固定高度（像素） */
    gint    key_spacing;    /* 按键间距 */
    gdouble opacity;        /* 键盘整体不透明度 0.2 ~ 1.0 */
    gint    font_size;      /* 按键主标签字号（像素） */
} OskConfig;

typedef struct {
    OskKeyboard *kbd;
    OskMouse    *mouse;

    ShiftState shift_state;        /* 三态循环：OFF → ONESHOT → LOCKED → OFF */

    gboolean ctrl, alt, meta, caps;

    gint     page;                 /* 当前页，0 = 第一页；只有多页布局用得到 */

    OskConfig config;

    GPtrArray *key_buttons;
    KeyButton *shift_btns[2];
    gsize      shift_btn_count;
    KeyButton *ctrl_btn, *alt_btn, *meta_btn, *caps_btn;
    KeyButton *page_btn;           /* 顶栏换页按钮，不随布局重建 */

    GtkWidget       *window;
    GtkWidget       *layout_host;
    GtkWidget       *drag_area;    /* 顶栏拖动区，全屏/游戏布局下隐藏 */
    const LayoutOps *layout;
} App;

/* —— 供 layout / widgets 调用的公共接口 —— */

/* 按键被激活时调用（匹配 KeyButtonActivateFunc） */
void app_on_key_activate(const KeyDef *def, gboolean is_repeat,
                          gpointer user_data);

/* 注册一个新建的 KeyButton，记录到 key_buttons 并登记修饰键按钮 */
void app_register_key_button(App *app, KeyButton *kb);

/* 根据 shift/caps 状态刷新所有键的标签与颜色 */
void app_refresh_labels(App *app);

/* 上滑输入（匹配 KeyButtonSwipeFunc） */
void app_on_key_swipe(const KeyDef *def, gpointer user_data);

/* 应用外观配置（透明度等） */
void app_apply_config(App *app);

/* 应用字号（重载动态 CSS provider，不需要重建布局） */
void app_apply_font(App *app);

/* 按当前 config 重建布局（设置里改了键宽高/间距后调用） */
void app_rebuild_layout(App *app);

#endif
