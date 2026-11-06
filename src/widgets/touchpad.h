#ifndef OSK_TOUCHPAD_WIDGET_H
#define OSK_TOUCHPAD_WIDGET_H

#include <gtk/gtk.h>
#include "backend/uinput_mouse.h"

typedef struct _TouchpadWidget TouchpadWidget;

/* App 只在这里用指针，前向声明即可 —— 不能 include app.h，
 * 因为 app.h 会间接包含本文件，形成循环。
 * 标签名必须与 app.h 的 typedef struct _App {...} App; 一致。 */
typedef struct _App App;

/* 创建触控板控件。
 *
 * app 用于「点击穿透」：注入点击前需要在光标处挖洞，
 * 这需要访问布局的输入区状态。传 NULL 时退化为直接注入
 * （点击会被 OSK 自己吃掉），行为与旧版一致。 */
TouchpadWidget *touchpad_widget_new(OskMouse *mouse, App *app);

GtkWidget      *touchpad_widget_gtk(TouchpadWidget *tw);

#endif
