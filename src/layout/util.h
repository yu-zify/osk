#ifndef OSK_LAYOUT_UTIL_H
#define OSK_LAYOUT_UTIL_H

#include <string.h>
#include <stdlib.h>
#include "layout.h"
#include "widgets/keybutton.h"

/* 从 keylib 里按 id 取键，创建 KeyButton，并注册到 App。
 * 未找到返回 NULL。 */
KeyButton *layout_make_key(App *app, const char *id);

/* 用一行 id 数组构造一个水平 GtkBox。
 * id 支持 "space*4" 这种倍数写法，表示占 4 个键宽。 */
GtkWidget *layout_build_row(App *app, const char *const *ids);

#endif
