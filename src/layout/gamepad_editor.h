#ifndef OSK_GAMEPAD_EDITOR_H
#define OSK_GAMEPAD_EDITOR_H

#include <gtk/gtk.h>
#include "app.h"
#include "gamepad_model.h"

/* 摇杆方向绑定面板。
 *
 * 主编辑流程已经在画布上内联完成（单击控件弹菜单、拖动改位置、
 * 空白长按新建），这个面板只负责一件在菜单里放不下的配置：
 * 摇杆四个方向各自绑定哪个键。
 *
 * 四个下拉框 + 一个「应用」，改动立即写进 item 并保存。 */
void gamepad_stick_binds_open(App *app, GpItem *stick);

#endif
