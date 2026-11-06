#ifndef OSK_UINPUT_KEYBOARD_H
#define OSK_UINPUT_KEYBOARD_H

#include <glib.h>

typedef struct OskKeyboard OskKeyboard;

OskKeyboard *osk_keyboard_new(const char *device, GError **error);

void osk_keyboard_chord(OskKeyboard *kbd, unsigned short code,
                        const unsigned short *mods, gsize n);

/* —— 用于游戏布局：真实按住 / 松开 ——
 * 手指按住期间键保持按下，松手才发抬起。游戏引擎靠这个判断长按。
 * 必须成对调用，否则键会卡在按下状态。 */
void osk_keyboard_key_down(OskKeyboard *kbd, unsigned short code);
void osk_keyboard_key_up(OskKeyboard *kbd, unsigned short code);

/* 释放所有键。切出游戏布局或退出时调用，防止 sticky key。 */
void osk_keyboard_release_all(OskKeyboard *kbd);

void osk_keyboard_free(OskKeyboard *kbd);

#endif