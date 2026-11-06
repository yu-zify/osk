#ifndef OSK_KEY_H
#define OSK_KEY_H

#include <glib.h>
#include <linux/input-event-codes.h>

typedef enum {
    KEY_CLASS_CHAR,       /* 字母、数字、符号 */
    KEY_CLASS_MODIFIER,   /* Shift/Ctrl/Alt/Meta/Caps */
    KEY_CLASS_FUNCTION,   /* Enter/Tab/Esc/方向键/F 键 */
    KEY_CLASS_CONTROL,    /* 控件：切页、最小化、关闭 */
} KeyClass;

typedef enum {
    KEY_ACTION_EMIT,      /* 发送 code */
    KEY_ACTION_MODIFIER,  /* 切换修饰状态，不发事件 */
    KEY_ACTION_COMMAND,   /* 触发应用命令 */
    /* 以下用于游戏布局的「真实长按」语义：
     * 按下时发按下事件、松手时发抬起事件，中间保持住。 */
    KEY_ACTION_GAMEPAD_BTN, /* 手柄按键：code 是 BTN_SOUTH 之类 */
    KEY_ACTION_MOUSE_BTN,   /* 鼠标按键：code 是 BTN_LEFT 之类 */
} KeyAction;

typedef struct {
    const char    *id;
    KeyClass       klass;
    KeyAction      action;

    const char    *label;         /* 主标签：普通形态（小写字母、数字、符号） */
    const char    *label_shift;   /* Shift/Caps 激活时的主标签；NULL = 与 label 相同 */

    /* 上滑：像真实键盘的符号键一样有上下两档。
     * 普通状态下滑出 label_up，Shift 激活时滑出 label_up_shift。 */
    const char    *label_up;
    const char    *label_up_shift;

    unsigned short code;

    unsigned short up_code;       /* 普通状态上滑键码；0 = 不支持上滑 */
    gboolean       up_shift;      /* 普通状态上滑是否需要叠加 Shift */
    unsigned short up_code_shift; /* Shift 状态上滑键码；0 = 与 up_code 相同 */
    gboolean       up_shift_shift;/* Shift 状态上滑是否需要叠加 Shift */

    gboolean       is_letter;

    guint          modifier_bit;
    const char    *command;
} KeyDef;

/* 位掩码，供 modifier_bit 使用 */
#define OSK_MOD_SHIFT (1u << 0)
#define OSK_MOD_CTRL  (1u << 1)
#define OSK_MOD_ALT   (1u << 2)
#define OSK_MOD_META  (1u << 3)
#define OSK_MOD_CAPS  (1u << 4)

const KeyDef *keylib_get(const char *id);

/* 遍历内置键表。
 * 编辑面板的「按键映射」下拉菜单需要把所有可绑定的键列出来，
 * count 返回条目数；返回的数组由 keylib 静态持有，不要释放。
 *
 * 注意：返回的表里包含 page 这类控件键（KEY_ACTION_COMMAND），
 * 调用方如果只想列真正能发按键的，需自行过滤 action 字段。 */
const KeyDef *keylib_all(gsize *count);

#endif
