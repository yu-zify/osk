#ifndef OSK_GAMEPAD_LAYOUT_H
#define OSK_GAMEPAD_LAYOUT_H

#include <gtk/gtk.h>
#include "layout.h"
#include "gamepad_model.h"

/* 游戏布局的运行时状态。
 * 布局对象本身是静态的（LayoutOps），但游戏布局要记住「当前这份可编辑的
 * 控件表」以及编辑模式开关，所以单独放一个状态结构。 */
typedef struct _GpState {
    GpLayout  *model;        /* 控件模型（可编辑，可持久化） */
    gboolean   editing;      /* 是否处于编辑模式 */
    GpItem    *selected;     /* 编辑模式下选中的控件 */
} GpState;

/* 取得全局的游戏布局状态，供编辑面板与持久化调用 */
GpState *gamepad_state(void);

/* 确保模型已加载（首次调用时创建默认布局），并把它挂到 app 上。
 * 返回当前模型指针。 */
GpLayout *gamepad_ensure_model(App *app);

/* 释放状态（程序退出时调用） */
void gamepad_state_free(App *app);

/* 保存当前布局到主配置文件 */
void gamepad_save(void);

/* 开关编辑模式。会触发布局重建，让控件是否可拖动随之改变。 */
void gamepad_set_editing(App *app, gboolean editing);

#endif
