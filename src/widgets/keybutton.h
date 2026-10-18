#ifndef OSK_KEYBUTTON_H
#define OSK_KEYBUTTON_H

#include <gtk/gtk.h>
#include "keylib/key.h"

typedef struct _KeyButton KeyButton;

typedef void (*KeyButtonActivateFunc)(const KeyDef *def,
                                       gboolean is_repeat,
                                       gpointer user_data);

typedef void (*KeyButtonSwipeFunc)(const KeyDef *def, gpointer user_data);

KeyButton *key_button_new(const KeyDef *def,
                           KeyButtonActivateFunc on_activate,
                           KeyButtonSwipeFunc on_swipe,
                           gpointer user_data);

GtkWidget    *key_button_widget(KeyButton *kb);

/* 三个标签的 setter。NULL 或空字符串表示不显示 */
void          key_button_set_label(KeyButton *kb, const char *text);         /* 中间 */
void          key_button_set_shift_label(KeyButton *kb, const char *text);   /* 右上 */
void          key_button_set_up_label(KeyButton *kb, const char *text);      /* 左上 */

void          key_button_stop_repeat(KeyButton *kb);
const KeyDef *key_button_def(const KeyButton *kb);

#endif