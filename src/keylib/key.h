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
} KeyAction;

typedef struct {
    const char    *id;
    KeyClass       klass;
    KeyAction      action;

    const char    *label;
    const char    *label_shift;   /* NULL = 与 label 相同 */
    const char    *label_up;      /* NULL = 不支持上滑 */

    unsigned short code;
    unsigned short up_code;       /* 0 = 不支持上滑 */
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

#endif