#ifndef OSK_UINPUT_TOUCHPAD_H
#define OSK_UINPUT_TOUCHPAD_H

#include <glib.h>

typedef struct OskTouchpad OskTouchpad;

/* 创建一个虚拟触控板。
 * width / height 是逻辑坐标范围（如 0..width），决定分辨率。
 * 一般用 4096 左右即可，太大会丢精度，太小会卡顿。 */
OskTouchpad *osk_touchpad_new(const char *device,
                               int width, int height,
                               GError **error);

/* 手指按下。id 是本次触摸的唯一 ID（由调用者分配，不能重复）。
 * x / y 是归一化到 [0, width] × [0, height] 的坐标。 */
void osk_touchpad_down(OskTouchpad *tp, guint id, int x, int y);

/* 手指移动。id 必须是已 down 的手指。 */
void osk_touchpad_move(OskTouchpad *tp, guint id, int x, int y);

/* 手指抬起。 */
void osk_touchpad_up(OskTouchpad *tp, guint id);

void osk_touchpad_free(OskTouchpad *tp);

#endif