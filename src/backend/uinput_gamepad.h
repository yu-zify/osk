#ifndef OSK_UINPUT_GAMEPAD_H
#define OSK_UINPUT_GAMEPAD_H

#include <glib.h>

typedef struct OskGamepad OskGamepad;

/* 创建一个虚拟手柄设备。
 *
 * 摇杆走 EV_ABS 绝对轴（左摇杆 X/Y，右摇杆 RX/RY），
 * 这是游戏和 SDL/evdev 都能直接识别的标准做法。
 * 十字键和 ABXY 走 EV_KEY（BTN_*）。
 *
 * 值域统一用 -32767 ~ 32767，0 表示居中。 */
OskGamepad *osk_gamepad_new(const char *device, GError **error);

/* 设置左摇杆。x 向右为正，y 向下为正（与屏幕坐标系一致）。
 * 值域 -32767 ~ 32767，越界会被钳制；死区由内核 absinfo.flat 处理。 */
void osk_gamepad_set_stick_left(OskGamepad *gp, gint x, gint y);

/* 设置右摇杆 */
void osk_gamepad_set_stick_right(OskGamepad *gp, gint x, gint y);

/* 手柄按键：code 传 BTN_SOUTH / BTN_EAST / BTN_NORTH / BTN_WEST /
 * BTN_TL / BTN_TR / BTN_SELECT / BTN_START / BTN_DPAD_UP 等 */
void osk_gamepad_button(OskGamepad *gp, guint code, gboolean pressed);

/* 松开所有手柄按键并让摇杆回中 */
void osk_gamepad_release_all(OskGamepad *gp);

void osk_gamepad_free(OskGamepad *gp);

#endif
