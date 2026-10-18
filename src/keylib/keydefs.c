#include "key.h"
#include <string.h>

#define K_CHAR(id, n, s, c) \
    { id, KEY_CLASS_CHAR, KEY_ACTION_EMIT, n, s, NULL, c, 0, TRUE,  0, NULL }
#define K_CHAR_UP(id, n, s, c, u, uc) \
    { id, KEY_CLASS_CHAR, KEY_ACTION_EMIT, n, s, u,    c, uc, TRUE,  0, NULL }
#define K_SYM(id, n, s, c) \
    { id, KEY_CLASS_CHAR, KEY_ACTION_EMIT, n, s, NULL, c, 0, FALSE, 0, NULL }
#define K_SYM_UP(id, n, s, c, u, uc) \
    { id, KEY_CLASS_CHAR, KEY_ACTION_EMIT, n, s, u,    c, uc, FALSE, 0, NULL }
#define K_FN(id, label, c) \
    { id, KEY_CLASS_FUNCTION, KEY_ACTION_EMIT, label, NULL, NULL, c, 0, FALSE, 0, NULL }
#define K_MOD(id, label, c, bit) \
    { id, KEY_CLASS_MODIFIER, KEY_ACTION_MODIFIER, label, NULL, NULL, c, 0, FALSE, bit, NULL }
#define K_HOLD(id, label, c) \
    { id, KEY_CLASS_FUNCTION, KEY_ACTION_EMIT, label, NULL, NULL, c, 0, FALSE, 0, NULL }

static const KeyDef builtin_keys[] = {
    /* 数字行 */
    K_SYM("`", "`", "~", KEY_GRAVE),
    K_SYM("1", "1", "!", KEY_1),
    K_SYM("2", "2", "@", KEY_2),
    K_SYM("3", "3", "#", KEY_3),
    K_SYM("4", "4", "$", KEY_4),
    K_SYM("5", "5", "%", KEY_5),
    K_SYM("6", "6", "^", KEY_6),
    K_SYM("7", "7", "&", KEY_7),
    K_SYM("8", "8", "*", KEY_8),
    K_SYM("9", "9", "(", KEY_9),
    K_SYM("0", "0", ")", KEY_0),
    K_SYM("-", "-", "_", KEY_MINUS),
    K_SYM("=", "=", "+", KEY_EQUAL),
    K_SYM("[", "[", "{", KEY_LEFTBRACE), 
    K_SYM("]", "]", "}", KEY_RIGHTBRACE), 
    K_SYM("\\", "\\", "|", KEY_BACKSLASH),

    /* 字母行 */
    

    K_CHAR("a", "a", "A", KEY_A),
    K_CHAR("s", "s", "S", KEY_S),
    K_CHAR("d", "d", "D", KEY_D),
    K_CHAR("f", "f", "F", KEY_F),
    K_CHAR("g", "g", "G", KEY_G),
    K_CHAR("h", "h", "H", KEY_H),
    K_CHAR("j", "j", "J", KEY_J),
    K_CHAR("k", "k", "K", KEY_K),
    K_CHAR("l", "l", "L", KEY_L),
    K_SYM(";", ";", ":", KEY_SEMICOLON),
    K_SYM("'", "'", "\"", KEY_APOSTROPHE),

    K_CHAR("z", "z", "Z", KEY_Z),
    K_CHAR("x", "x", "X", KEY_X),
    K_CHAR("c", "c", "C", KEY_C),
    K_CHAR("v", "v", "V", KEY_V),
    K_CHAR("b", "b", "B", KEY_B),
    K_CHAR("n", "n", "N", KEY_N),
    K_CHAR("m", "m", "M", KEY_M),
    K_SYM(",", ",", "<", KEY_COMMA),
    K_SYM(".", ".", ">", KEY_DOT),
    K_SYM("/", "/", "?", KEY_SLASH),

    /* 功能键 */
    K_FN("space",     "Space", KEY_SPACE),
    K_FN("enter",     "Enter", KEY_ENTER),
    K_FN("backspace", "Back",  KEY_BACKSPACE),
    K_FN("tab",       "Tab",   KEY_TAB),
    K_FN("esc",       "Esc",   KEY_ESC),
    K_FN("up",    "↑", KEY_UP),
    K_FN("down",  "↓", KEY_DOWN),
    K_FN("left",  "←", KEY_LEFT),
    K_FN("right", "→", KEY_RIGHT),

    /* 修饰键 */
    K_MOD("shift", "Shift", KEY_LEFTSHIFT, OSK_MOD_SHIFT),
    K_MOD("ctrl",  "Ctrl",  KEY_LEFTCTRL,  OSK_MOD_CTRL),
    K_MOD("alt",   "Alt",   KEY_LEFTALT,   OSK_MOD_ALT),
    K_MOD("meta",  "Meta",  KEY_LEFTMETA,  OSK_MOD_META),
    K_MOD("caps",  "Caps",  KEY_CAPSLOCK,  OSK_MOD_CAPS),
    
    K_CHAR_UP("q", "q", "Q", KEY_Q, "1", KEY_1),
    K_CHAR_UP("w", "w", "W", KEY_W, "2", KEY_2),
    K_CHAR_UP("e", "e", "E", KEY_E, "3", KEY_3),
    K_CHAR_UP("r", "r", "R", KEY_R, "4", KEY_4),
    K_CHAR_UP("t", "t", "T", KEY_T, "5", KEY_5),
    K_CHAR_UP("y", "y", "Y", KEY_Y, "6", KEY_6),
    K_CHAR_UP("u", "u", "U", KEY_U, "7", KEY_7),
    K_CHAR_UP("i", "i", "I", KEY_I, "8", KEY_8),
    K_CHAR_UP("o", "o", "O", KEY_O, "9", KEY_9),
    K_CHAR_UP("p", "p", "P", KEY_P, "0", KEY_0),
    
};

const KeyDef *keylib_get(const char *id) {
    for (gsize i = 0; i < G_N_ELEMENTS(builtin_keys); ++i)
        if (strcmp(builtin_keys[i].id, id) == 0)
            return &builtin_keys[i];
    return NULL;
}