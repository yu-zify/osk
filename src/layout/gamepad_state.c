#include "gamepad_layout.h"
#include "layout.h"
#include "app.h"

/* ===== 游戏布局运行时状态 =====
 *
 * 模型（GpLayout）是纯数据，可以存进配置文件；
 * 这里负责它的生命周期，以及「编辑模式」的开关与重建。
 */

static GpState g_gp = {0};

GpState *gamepad_state(void) {
    return &g_gp;
}

/* 释放状态（程序退出时调用） */
void gamepad_state_free(App *app) {
    (void)app;
    if (g_gp.model) {
        gp_layout_free(g_gp.model);
        g_gp.model = NULL;
    }
    g_gp.selected = NULL;
    g_gp.editing = FALSE;
}

/* 确保模型已加载。app 里的指针与全局状态指向同一份。 */
GpLayout *gamepad_ensure_model(App *app) {
    if (!g_gp.model)
        g_gp.model = gp_layout_new_default();

    if (app)
        app->gp = &g_gp;

    return g_gp.model;
}

/* 把当前布局写进配置文件的主配置。
 * 复用 config.c 的那套读写，避免两套配置文件。 */
void gamepad_save(void) {
    if (!g_gp.model) return;
    config_save_gamepad(g_gp.model);
}

void gamepad_set_editing(App *app, gboolean editing) {
    if (!app) return;

    g_gp.editing = editing;
    g_gp.selected = NULL;

    /* 编辑模式开关会改变控件是否可拖动，需要重建布局 */
    if (app->layout == layout_game_ops())
        app_rebuild_layout(app);
}
