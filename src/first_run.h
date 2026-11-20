#ifndef OSK_FIRST_RUN_H
#define OSK_FIRST_RUN_H

/* 在创建输入设备和主窗口之前执行。
 * 每个用户只自动尝试一次；结果另存于 $XDG_CONFIG_HOME/osk/setup.ini。
 * 返回 FALSE 时需要先注销并重新登录，或修复输入设备权限后再启动。 */
#include <glib.h>

gboolean osk_first_run_setup(void);

#endif
