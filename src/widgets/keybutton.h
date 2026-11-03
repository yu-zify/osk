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

/* 中间主字符 */
void          key_button_set_label(KeyButton *kb, const char *text);

/* 右上角的上滑角标：普通状态一个、Shift 状态一个。
 * 传 NULL/空串表示该档不显示。highlight_shift 为真时加亮 Shift 那一个。 */
void          key_button_set_up_labels(KeyButton *kb,
                                       const char *up,
                                       const char *up_shift,
                                       gboolean highlight_shift);

/* 固定按键尺寸 */
void          key_button_set_fixed_size(KeyButton *kb, gint width, gint height);

/* 修饰键的激活外观：class 由调用方给（mod-active / mod-locked） */
void          key_button_set_active_class(KeyButton *kb,
                                          const char *class_name,
                                          gboolean active);

void          key_button_stop_repeat(KeyButton *kb);
const KeyDef *key_button_def(const KeyButton *kb);

#endif
