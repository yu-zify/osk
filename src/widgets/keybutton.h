#ifndef OSK_KEYBUTTON_H
#define OSK_KEYBUTTON_H

#include <gtk/gtk.h>
#include "keylib/key.h"

typedef struct _KeyButton KeyButton;

typedef void (*KeyButtonActivateFunc)(const KeyDef *def,
                                       gboolean is_repeat,
                                       gpointer user_data);

typedef void (*KeyButtonSwipeFunc)(const KeyDef *def, gpointer user_data);

/* 长按语义的两个回调：手指按下时调 on_press，松手时调 on_release。
 * 两者一定成对出现（取消手势时也会补一次 release）。 */
typedef void (*KeyButtonHoldFunc)(const KeyDef *def, gpointer user_data);

/* HOLD 模式下按住并拖动：手指每移动一帧回调一次，
 * dx/dy 是相对上一帧的位移（像素）。
 *
 * 为什么不用 motion-notify-event：多点触控时「模拟指针」只有一根
 * （第一个落下的手指），第二根手指的移动只走 touch/gesture 路径，
 * 模拟的 motion 事件不会发给第二个按住的控件 —— 表现为
 * 「摇杆先按住时，鼠标键能按住却拖不动」。手势是按序列跟踪的，
 * 每个控件拿到的是自己那根手指的移动，天然没有这个问题。 */
typedef void (*KeyButtonHoldDragFunc)(const KeyDef *def,
                                       gdouble dx, gdouble dy,
                                       gpointer user_data);

KeyButton *key_button_new(const KeyDef *def,
                           KeyButtonActivateFunc on_activate,
                           KeyButtonSwipeFunc on_swipe,
                           gpointer user_data);

/* 切换成「真实键盘」模式：按下即持续按住，松手才释放。
 *
 * 与默认模式的区别：
 *   - 不发 is_repeat 连发（按住重复是文本输入才需要的行为）
 *   - 不响应上滑（游戏里一个键就一个功能，上滑会误触）
 *   - 按下立即生效，不需要等到松手
 *
 * 游戏布局用它来实现「按住 W 一直前进」这种真实按键状态。 */
void key_button_set_hold_mode(KeyButton *kb,
                               KeyButtonHoldFunc on_press,
                               KeyButtonHoldFunc on_release);

/* HOLD 模式下的按住拖拽回调（配合 on_press/on_release 使用）。
 * 只对用得着的控件设置（比如游戏布局的鼠标键）；不设置则拖动被忽略。 */
void key_button_set_hold_drag(KeyButton *kb,
                               KeyButtonHoldDragFunc on_drag);

GtkWidget    *key_button_widget(KeyButton *kb);

/* 中间主字符 */
void          key_button_set_label(KeyButton *kb, const char *text);

/* 按主标签字符数刷新 txt1..txt5 样式类（字号自适应，避免长标签被截断）。
 * 换了字号或改了标签内容后如果直接动了 GTK 标签，需要手动调用一次。 */
void          key_button_update_txt_class(KeyButton *kb);

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
