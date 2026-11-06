#ifndef OSK_APP_H
#define OSK_APP_H

#include <gtk/gtk.h>
#include "backend/uinput_keyboard.h"
#include "keylib/key.h"
#include "widgets/keybutton.h"
#include "backend/uinput_mouse.h"
#include "backend/uinput_gamepad.h"

/* 游戏布局的控件模型：GpLayout / GpItem。
 * 放在这里是因为 App 要持有它，而 config 的存取签名也要用到。 */
#include "layout/gamepad_model.h"

/* 前向声明，避免循环包含 */
typedef struct _LayoutOps LayoutOps;
typedef struct _GpState   GpState;

typedef enum {
    SHIFT_OFF,       /* 未激活，按钮默认颜色 */
    SHIFT_ONESHOT,   /* 只对下一个键生效，然后自动关闭；按钮蓝色 */
    SHIFT_LOCKED,    /* 持续生效，直到手动关闭；按钮红色 */
} ShiftState;

/* Ctrl / Alt / Win 这类修饰键的状态。
 *
 * 与 Shift 的区别：不需要「锁定」那一档，只有两态循环 ——
 *   点一下 → 蓝色（只对下一个键生效）
 *   再点一下 → 关闭
 *
 * 这样单次修饰键既够用（Ctrl+C 之类），又不会因为忘记关掉而
 * 让后续所有输入都带上修饰键。 */
typedef enum {
    MOD_OFF,         /* 未激活 */
    MOD_ONESHOT,     /* 只对下一个键生效，按钮蓝色 */
} ModState;

/* 可在设置里调整的外观参数 */
typedef struct {
    gint    key_width;        /* 按键固定宽度（像素） */
    gint    key_height;       /* 按键固定高度（像素） */
    gint    key_spacing;      /* 按键间距 */
    gdouble opacity;          /* 键盘整体不透明度 0.2 ~ 1.0 */
    gdouble pad_opacity;      /* 触控板窗口不透明度 0.2 ~ 1.0 */
    gint    font_size;        /* 按键主标签字号（像素） */
    gint    corner_font_size; /* 右上角上滑字符字号（像素） */
} OskConfig;

/* —— 外观参数的默认值与取值范围 ——
 * 以前默认值硬编码在 main.c、范围写在 settings.c 的微调框里、钳制写在
 * app_apply_font 里，三处各一套数字，改一处就会和另一处对不上。
 * 现在统一放在这里，读配置、写配置、UI 控件、字号计算都引用同一份。 */
#define OSK_KEY_WIDTH_DEFAULT          50
#define OSK_KEY_HEIGHT_DEFAULT         60
#define OSK_KEY_SPACING_DEFAULT         1
#define OSK_OPACITY_DEFAULT           1.0
#define OSK_FONT_SIZE_DEFAULT          28
#define OSK_CORNER_FONT_SIZE_DEFAULT   12

#define OSK_KEY_WIDTH_MIN              20
#define OSK_KEY_WIDTH_MAX             160
#define OSK_KEY_HEIGHT_MIN             20
#define OSK_KEY_HEIGHT_MAX            160
#define OSK_KEY_SPACING_MIN             0
#define OSK_KEY_SPACING_MAX            30
#define OSK_FONT_SIZE_MIN               8
#define OSK_FONT_SIZE_MAX              64
#define OSK_CORNER_FONT_SIZE_MIN        6
#define OSK_CORNER_FONT_SIZE_MAX       40
#define OSK_OPACITY_MIN               0.2
#define OSK_OPACITY_MAX               1.0

/* 具名标签：其他头文件（如 touchpad.h）需要前向声明 App 时
 * 用 struct _App，与这里的定义保持一致。 */
typedef struct _App {
    OskKeyboard *kbd;
    OskMouse    *mouse;
    OskGamepad  *gamepad;          /* 手柄：游戏布局的摇杆与手柄按键用 */

    ShiftState shift_state;        /* 三态循环：OFF → ONESHOT → LOCKED → OFF */

    /* Ctrl / Alt / Win：单次修饰键，用完自动关闭 */
    ModState ctrl, alt, meta;

    /* Caps：真正的开关，保持锁定语义 */
    gboolean caps;

    gint     page;                 /* 当前页，0 = 第一页；只有多页布局用得到 */

    OskConfig config;

    GPtrArray *key_buttons;
    KeyButton *shift_btns[2];
    gsize      shift_btn_count;
    KeyButton *ctrl_btn, *alt_btn, *meta_btn, *caps_btn;
    KeyButton *page_btn;           /* 顶栏换页按钮，不随布局重建 */

    GtkWidget       *window;
    GtkWidget       *layout_host;
    GtkWidget       *drag_area;    /* 顶栏拖动区，游戏布局下隐藏 */
    GtkWidget       *game_edit_btn; /* 游戏布局编辑按钮，只在游戏布局显示 */

    /* 顶栏的三个部分。游戏布局下要让按钮居中排列，
     * 需要能分别控制左侧按钮、拖动区、右侧按钮组。 */
    GtkWidget       *toolbar;
    GtkWidget       *toolbar_left;
    GtkWidget       *toolbar_right;
    GtkWidget       *home_end_box;  /* home/end 按钮组，游戏布局下隐藏 */

    /* 触控板弹窗与它的按钮。触控板不再是布局，改为独立窗口。 */
    GtkWidget       *mouse_btn;     /* 顶栏的鼠标按钮，游戏布局下隐藏 */
    GtkWidget       *mouse_panel;   /* 弹窗，NULL 表示没打开 */

    const LayoutOps *layout;

    /* 游戏布局的编辑状态（模型 + 是否编辑中）。
     * 注意不要和上面的 OskGamepad *gamepad 重名 —— 那个是 uinput 设备句柄，
     * 这个是布局数据，两者完全无关。 */
    GpState *gp;
} App;

/* —— 供 layout / widgets 调用的公共接口 —— */

/* 按键被激活时调用（匹配 KeyButtonActivateFunc） */
void app_on_key_activate(const KeyDef *def, gboolean is_repeat,
                          gpointer user_data);

/* 注册一个新建的 KeyButton，记录到 key_buttons 并登记修饰键按钮 */
void app_register_key_button(App *app, KeyButton *kb);

/* 根据 shift/caps 状态刷新所有键的标签与颜色 */
void app_refresh_labels(App *app);

/* 上滑输入（匹配 KeyButtonSwipeFunc） */
void app_on_key_swipe(const KeyDef *def, gpointer user_data);

/* 应用外观配置（透明度等） */
void app_apply_config(App *app);

/* 应用触控板窗口的透明度。触控板是独立窗口，单独控制。 */
void app_apply_pad_config(App *app);

/* 统一设置子窗口外观：去掉系统装饰 + 加自绘白边。
 *
 * Wayland 下没有系统装饰，所有子窗口（设置、编辑面板、对话框）
 * 都走这里，保证观感一致。 */
void app_setup_subwindow(GtkWidget *win);

/* 强制窗口使用客户端装饰（CSD）并去掉标题栏。
 *
 * KWin 对 GTK 应用会强制服务端装饰（SSD），并忽略
 * gtk_window_set_decorated(FALSE) 的请求（KDE Bug 484800）。
 *
 * 绕过办法是给窗口设一个空 titlebar —— 一旦有了 titlebar，
 * GTK 就进入 CSD 模式，合成器放手，再 set_decorated(FALSE)
 * 就真的没有边框了。主窗口和子窗口都必须调用。 */
void app_force_csd(GtkWindow *win);

/* 给窗口画一圈白色描边。
 *
 * 用 cairo 在 draw 信号里自绘，不走 CSS：
 * CSS 的 .decoration 规则是全局的，会把 GtkMenu 弹出菜单的
 * 背景一起抹掉（菜单也是带 decoration 的 toplevel）。
 *
 * 调用前窗口必须已经 app_paintable（本函数会设置）。 */
void app_attach_border(GtkWidget *win);

/* 创建子窗口的统一外壳：标题栏（可拖动）+ 关闭按钮 + 内容区。
 *
 * 返回内容区的容器，调用方往里面塞自己的控件。
 * 白边画在 shell 外沿，所以不会陷进内容里。
 *
 * 用法：
 *     GtkWidget *content = app_subwindow_shell(win, "设置");
 *     gtk_box_pack_start(GTK_BOX(content), my_widget, ...);
 *     app_finish_subwindow(win); */
GtkWidget *app_subwindow_shell(GtkWidget *win, const char *title);

/* 子窗口内容装好之后调用，挂上白边。
 *
 * 必须与 app_setup_subwindow 分开：后者通常在 gtk_container_add
 * 之前调用，此时窗口还没有子控件，边框会挂到窗口自身上，
 * 而 CSD 模式下窗口自身的 draw 不一定触发。
 *
 * 用法：
 *     app_setup_subwindow(win);
 *     GtkWidget *c = app_subwindow_shell(win, "标题");
 *     ... 填充 c ...
 *     app_finish_subwindow(win); */
void app_finish_subwindow(GtkWidget *win);

/* 应用字号（重载动态 CSS provider，不需要重建布局） */
void app_apply_font(App *app);

/* 按当前 config 重建布局（设置里改了键宽高/间距后调用） */
void app_rebuild_layout(App *app);

/* 切换布局。内部会先释放游戏布局里按住的键与摇杆，
 * 再重建布局并同步窗口属性。设置面板和编辑面板都走这个入口。 */
void app_switch_layout(App *app, const LayoutOps *ops);

/* —— 配置持久化 ——
 * 读写 $XDG_CONFIG_HOME/osk/config.ini（通常 ~/.config/osk/config.ini）。
 * 读写都会把值钳制到上面的取值范围内，所以手改配置文件写错也不会把界面搞坏。 */

/* 把默认值填进 config（不碰文件） */
void config_set_defaults(OskConfig *cfg);

/* 读取配置；文件不存在或字段缺失时用默认值补齐。
 * 返回是否真的读到了文件（仅用于日志）。 */
gboolean config_load(OskConfig *cfg);

/* 写回配置。失败时打印原因并返回 FALSE，不影响程序继续运行。 */
gboolean config_save(const OskConfig *cfg);

/* 当前使用的配置文件路径（内部静态字符串，不要释放） */
const char *config_path(void);

/* —— 游戏布局的持久化 ——
 * 和外观参数存在同一个文件里，用不同的组（[game_layout]），
 * 两个保存函数都会先读出现有内容再合并，互不覆盖。 */
void      config_save_gamepad(const GpLayout *lay);
GpLayout *config_load_gamepad(void);

#endif
