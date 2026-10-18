#ifndef OSK_TOUCHPAD_WIDGET_H
#define OSK_TOUCHPAD_WIDGET_H

#include <gtk/gtk.h>
#include "backend/uinput_touchpad.h"

typedef struct _TouchpadWidget TouchpadWidget;

TouchpadWidget *touchpad_widget_new(OskTouchpad *backend);

GtkWidget *touchpad_widget_gtk(TouchpadWidget *tw);

#endif