#ifndef OSK_UINPUT_MOUSE_H
#define OSK_UINPUT_MOUSE_H

#include <glib.h>
#include <linux/input-event-codes.h> /* ← 新增 */

typedef struct OskMouse OskMouse;

OskMouse *osk_mouse_new(const char *device, GError **error);

/* 相对移动，dx/dy 是像素增量，可为负 */
void osk_mouse_move_rel(OskMouse *m, gdouble dx, gdouble dy);

/* 滚轮：dy 正值向下滚，dx 正值向右滚 */
void osk_mouse_scroll(OskMouse *m, gint dx, gint dy);

/* button 传 BTN_LEFT / BTN_MIDDLE / BTN_RIGHT */
void osk_mouse_button(OskMouse *m, guint button, gboolean pressed);

void osk_mouse_free(OskMouse *m);

#endif