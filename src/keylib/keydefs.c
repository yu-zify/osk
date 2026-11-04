#include "key.h"
#include <string.h>

/* 字母键：中间小写，Shift/Caps 激活时中间变大写 */
#define K_CHAR(id, n, s, c) \
    { id, KEY_CLASS_CHAR, KEY_ACTION_EMIT, n, s, NULL, NULL, c, \
      0, FALSE, 0, FALSE, TRUE,  0, NULL }

/* 字母键 + 双上滑：up 为普通上滑符号，up_s 为 Shift 上滑符号。
 * uc / uc_s 是两个符号各自的键码；need / need_s 表示要不要叠加 Shift。 */
#define K_CHAR_UP(id, n, s, up, up_s, c, uc, need, uc_s, need_s) \
    { id, KEY_CLASS_CHAR, KEY_ACTION_EMIT, n, s, up, up_s, c, \
      uc, need, uc_s, need_s, TRUE,  0, NULL }

/* 符号/数字键：中间主字符，Shift 形态显示在左上角 */
#define K_SYM(id, n, s, c) \
    { id, KEY_CLASS_CHAR, KEY_ACTION_EMIT, n, s, NULL, NULL, c, \
      0, FALSE, 0, FALSE, FALSE, 0, NULL }

#define K_FN(id, label, c) \
    { id, KEY_CLASS_FUNCTION, KEY_ACTION_EMIT, label, NULL, NULL, NULL, c, \
      0, FALSE, 0, FALSE, FALSE, 0, NULL }
#define K_MOD(id, label, c, bit) \
    { id, KEY_CLASS_MODIFIER, KEY_ACTION_MODIFIER, label, NULL, NULL, NULL, c, \
      0, FALSE, 0, FALSE, FALSE, bit, NULL }
#define K_HOLD(id, label, c) \
    { id, KEY_CLASS_FUNCTION, KEY_ACTION_EMIT, label, NULL, NULL, NULL, c, \
      0, FALSE, 0, FALSE, FALSE, 0, NULL }

static const KeyDef builtin_keys[] = {
    /* ===== 数字行：不支持上滑，Shift 形态显示在左上角 ===== */
    K_SYM("`",  "`", "~", KEY_GRAVE),
    K_SYM("1",  "1", "!", KEY_1),
    K_SYM("2",  "2", "@", KEY_2),
    K_SYM("3",  "3", "#", KEY_3),
    K_SYM("4",  "4", "$", KEY_4),
    K_SYM("5",  "5", "%", KEY_5),
    K_SYM("6",  "6", "^", KEY_6),
    K_SYM("7",  "7", "&", KEY_7),
    K_SYM("8",  "8", "*", KEY_8),
    K_SYM("9",  "9", "(", KEY_9),
    K_SYM("0",  "0", ")", KEY_0),
    K_SYM("-",  "-", "_", KEY_MINUS),
    K_SYM("=",  "=", "+", KEY_EQUAL),
    K_SYM("[",  "[", "{", KEY_LEFTBRACE),
    K_SYM("]",  "]", "}", KEY_RIGHTBRACE),
    K_SYM("\\", "\\", "|", KEY_BACKSLASH),

    /* ===== 符号键 ===== */
    K_SYM(";", ";", ":", KEY_SEMICOLON),
    K_SYM("'", "'", "\"", KEY_APOSTROPHE),
    K_SYM(",", ",", "<", KEY_COMMA),
    K_SYM(".", ".", ">", KEY_DOT),
    K_SYM("/", "/", "?", KEY_SLASH),

    /* ===== 功能键 ===== */
    K_FN("space",     "␣", KEY_SPACE),
    K_FN("enter",     "⏎", KEY_ENTER),
    K_FN("backspace", "⌫",  KEY_BACKSPACE),
    K_FN("tab",       "⇥",   KEY_TAB),
    K_FN("esc",       "Esc",   KEY_ESC),
    K_FN("up",    "↑", KEY_UP),
    K_FN("down",  "↓", KEY_DOWN),
    K_FN("left",  "←", KEY_LEFT),
    K_FN("right", "→", KEY_RIGHT),
    K_FN("home",  "Home", KEY_HOME),
    K_FN("end",   "End",  KEY_END),

    /* F1 ~ F12 */
    K_FN("f1",  "F1",  KEY_F1),
    K_FN("f2",  "F2",  KEY_F2),
    K_FN("f3",  "F3",  KEY_F3),
    K_FN("f4",  "F4",  KEY_F4),
    K_FN("f5",  "F5",  KEY_F5),
    K_FN("f6",  "F6",  KEY_F6),
    K_FN("f7",  "F7",  KEY_F7),
    K_FN("f8",  "F8",  KEY_F8),
    K_FN("f9",  "F9",  KEY_F9),
    K_FN("f10", "F10", KEY_F10),
    K_FN("f11", "F11", KEY_F11),
    K_FN("f12", "F12", KEY_F12),

    /* ===== 修饰键 ===== */
    K_MOD("shift", "Shift", KEY_LEFTSHIFT, OSK_MOD_SHIFT),
    K_MOD("caps",  "Caps",  KEY_CAPSLOCK,  OSK_MOD_CAPS),
    K_MOD("ctrl",  "Ctrl",  KEY_LEFTCTRL,  OSK_MOD_CTRL),
    K_MOD("alt",   "Alt",   KEY_LEFTALT,   OSK_MOD_ALT),
    K_MOD("meta",  "Meta",  KEY_LEFTMETA,  OSK_MOD_META),

    /* Win 键：和 meta 同一个键码，单独给个 ⊞ 图标，方便和顶栏风格区分 */
    K_MOD("win",   "⊞",     KEY_LEFTMETA,  OSK_MOD_META),

    /* 控件键：换页按钮。不发按键事件，由 main.c 自己的回调处理。 */
    { "page", KEY_CLASS_CONTROL, KEY_ACTION_COMMAND, "1/2", NULL, NULL, NULL,
      0, 0, FALSE, 0, FALSE, FALSE, 0, "page" },

    /* ===== 字母：每个键一对上滑符号（普通 / Shift） =====
     * 上排 q..p 沿用数字行：普通滑出数字，Shift 状态滑出对应的上档符号。
     * 其余字母按物理键盘的符号键配对依次分配，所以和真实键盘一样能盲按。 */
    K_CHAR_UP("q", "q", "Q", "1", "!", KEY_Q, KEY_1,     FALSE,
                                             KEY_1,     TRUE),
    K_CHAR_UP("w", "w", "W", "2", "@", KEY_W, KEY_2,     FALSE,
                                             KEY_2,     TRUE),
    K_CHAR_UP("e", "e", "E", "3", "#", KEY_E, KEY_3,     FALSE,
                                             KEY_3,     TRUE),
    K_CHAR_UP("r", "r", "R", "4", "$", KEY_R, KEY_4,     FALSE,
                                             KEY_4,     TRUE),
    K_CHAR_UP("t", "t", "T", "5", "%", KEY_T, KEY_5,     FALSE,
                                             KEY_5,     TRUE),
    K_CHAR_UP("y", "y", "Y", "6", "^", KEY_Y, KEY_6,     FALSE,
                                             KEY_6,     TRUE),
    K_CHAR_UP("u", "u", "U", "7", "&", KEY_U, KEY_7,     FALSE,
                                             KEY_7,     TRUE),
    K_CHAR_UP("i", "i", "I", "8", "*", KEY_I, KEY_8,     FALSE,
                                             KEY_8,     TRUE),
    K_CHAR_UP("o", "o", "O", "9", "(", KEY_O, KEY_9,     FALSE,
                                             KEY_9,     TRUE),
    K_CHAR_UP("p", "p", "P", "0", ")", KEY_P, KEY_0,     FALSE,
                                             KEY_0,     TRUE),

    K_CHAR_UP("a", "a", "A", "-", "_", KEY_A, KEY_MINUS,     FALSE,
                                             KEY_MINUS,     TRUE),
    K_CHAR_UP("s", "s", "S", "=", "+", KEY_S, KEY_EQUAL,     FALSE,
                                             KEY_EQUAL,     TRUE),
    K_CHAR_UP("d", "d", "D", "[", "{", KEY_D, KEY_LEFTBRACE, FALSE,
                                             KEY_LEFTBRACE, TRUE),
    K_CHAR_UP("f", "f", "F", "]", "}", KEY_F, KEY_RIGHTBRACE, FALSE,
                                             KEY_RIGHTBRACE, TRUE),
    K_CHAR_UP("g", "g", "G", "\\", "|", KEY_G, KEY_BACKSLASH, FALSE,
                                             KEY_BACKSLASH, TRUE),
    K_CHAR_UP("h", "h", "H", ";", ":", KEY_H, KEY_SEMICOLON, FALSE,
                                             KEY_SEMICOLON, TRUE),
    K_CHAR_UP("j", "j", "J", "'", "\"", KEY_J, KEY_APOSTROPHE, FALSE,
                                             KEY_APOSTROPHE, TRUE),
    K_CHAR_UP("k", "k", "K", ",", "<", KEY_K, KEY_COMMA,     FALSE,
                                             KEY_COMMA,     TRUE),
    K_CHAR_UP("l", "l", "L", ".", ">", KEY_L, KEY_DOT,       FALSE,
                                             KEY_DOT,       TRUE),

    K_CHAR_UP("z", "z", "Z", "/", "?", KEY_Z, KEY_SLASH,     FALSE,
                                             KEY_SLASH,     TRUE),
    K_CHAR_UP("x", "x", "X", "`", "~", KEY_X, KEY_GRAVE,     FALSE,
                                             KEY_GRAVE,     TRUE),
    K_CHAR_UP("c", "c", "C", "-", "_", KEY_C, KEY_MINUS,     FALSE,
                                             KEY_MINUS,     TRUE),
    K_CHAR_UP("v", "v", "V", "=", "+", KEY_V, KEY_EQUAL,     FALSE,
                                             KEY_EQUAL,     TRUE),
    K_CHAR_UP("b", "b", "B", "[", "{", KEY_B, KEY_LEFTBRACE, FALSE,
                                             KEY_LEFTBRACE, TRUE),
    K_CHAR_UP("n", "n", "N", "]", "}", KEY_N, KEY_RIGHTBRACE, FALSE,
                                             KEY_RIGHTBRACE, TRUE),
    K_CHAR_UP("m", "m", "M", "\\", "|", KEY_M, KEY_BACKSLASH, FALSE,
                                             KEY_BACKSLASH, TRUE),
};

const KeyDef *keylib_get(const char *id) {
    for (gsize i = 0; i < G_N_ELEMENTS(builtin_keys); ++i)
        if (strcmp(builtin_keys[i].id, id) == 0)
            return &builtin_keys[i];
    return NULL;
}
