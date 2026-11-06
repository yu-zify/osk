#ifndef OSK_GAMEPAD_LAYOUT_MODEL_H
#define OSK_GAMEPAD_LAYOUT_MODEL_H

#include <glib.h>

/* 注意：这里只用 glib，不包含 gtk 或 layout.h。
 * 因为 app.h 会包含本文件，而 layout.h 又包含 app.h，
 * 让本文件保持「零依赖」是打破这个循环的关键。
 * 需要 GTK 类型的地方（编辑面板）自己 include <gtk/gtk.h>。 */

/* ===== 游戏布局的自定义控件模型 =====
 *
 * 游戏布局是一个可以自由摆放控件的画布（GtkFixed），所以不能像其他布局那样
 * 用固定代码写死键位表。这里定义「一个控件是什么」，并负责存取到配置文件。
 *
 * 坐标用画布相对坐标（像素）。为了在不同分辨率的屏幕上还能用，同时记录
 * 画布的设计尺寸（design_w / design_h）；载入时按当前窗口等比缩放。
 */

typedef enum {
    GP_ITEM_KEY,        /* 键盘按键：引用 keylib 里的 id（"w" / "space" / "shift"） */
    GP_ITEM_GAMEPAD,    /* 手柄按键：引用 BTN_* 码 */
    GP_ITEM_MOUSE,      /* 鼠标按键：引用 BTN_LEFT 等 */
    GP_ITEM_STICK_L,    /* 左摇杆 */
    GP_ITEM_STICK_R,    /* 右摇杆 */
    GP_ITEM_TOUCHPAD,   /* 触控板（鼠标模拟） */
} GpItemType;

/* 摇杆的四方向绑定。
 *
 * 摇杆本身输出的是 ABS 模拟轴（游戏原生支持，力感和「走/跑」靠推杆幅度区分）。
 * 但有些游戏只认键盘，所以额外允许往四个方向各绑一个键：
 * 推开超过阈值时触发，回到死区时松开。
 *
 * 两套输出同时存在、互不干扰 —— 轴值照发，方向键按需叠加。
 * dir_key 为 NULL 表示该方向不绑键。 */
typedef enum {
    GP_DIR_UP,
    GP_DIR_DOWN,
    GP_DIR_LEFT,
    GP_DIR_RIGHT,
    GP_DIR_COUNT
} GpDir;

typedef struct {
    GpItemType type;
    gint  x, y;             /* 画布坐标 */
    gint  w, h;             /* 尺寸 */
    char *id;               /* GP_ITEM_KEY 用：keylib id；其他类型可为 NULL */
    gint  code;             /* GP_ITEM_GAMEPAD / GP_ITEM_MOUSE 用：BTN_* */
    char *label;            /* 显示文字；NULL 时由 id/code 推导 */

    /* —— 仅摇杆用：四方向键位绑定 ——
     * dir_id 存 keylib 的 id（如 "up" / "w"），NULL 表示该方向不绑定。
     * 用 id 而不是键码，是为了和键盘按键用同一套表示，配置文件也可读。 */
    char *dir_id[GP_DIR_COUNT];

    /* —— 仅摇杆用：八向支持 ——
     * FALSE（默认）：斜推只触发主轴方向。
     * TRUE：每个轴独立判定，斜推同时触发两个方向。 */
    gboolean eight_way;

    /* 背景透明度 0..1。负数表示「用控件类型的默认值」，
     * 这样键盘按键、摇杆、触控板可以各有各的默认观感。 */
    gdouble bg_alpha;
} GpItem;

typedef struct {
    GPtrArray *items;       /* GpItem*，GPtrArray 带 free_func */
    gint design_w, design_h;
} GpLayout;

/* 创建一个带默认内容的布局（首次运行时用） */
GpLayout *gp_layout_new_default(void);

/* 重建时临时用：从现有布局深拷贝 */
GpLayout *gp_layout_copy(const GpLayout *src);

void gp_layout_free(GpLayout *lay);

/* 增删控件。坐标和尺寸会被钳制到画布内。 */
GpItem *gp_layout_add(GpLayout *lay, GpItemType type,
                      gint x, gint y, gint w, gint h);

void gp_layout_remove(GpLayout *lay, GpItem *item);

/* 把控件限制在画布内（拖动后调用）。
 * 画布尺寸来自 design_w/design_h，运行时由 gamepad_sync_canvas_size 同步成真实大小。 */
void gp_layout_clamp_item(GpLayout *lay, GpItem *item);

/* 同步画布的运行时尺寸。
 *
 * design_w/design_h 表示「当前画布有多大」，用于把控件钳制在可见范围内。
 * 画布在不同屏幕上尺寸不同，所以每次 size-allocate 都要调一次 ——
 * 否则控件会被限制在一个过时的尺寸里，表现为「拖到中途就拖不动」。 */
void gamepad_sync_canvas_size(GpLayout *lay, gint w, gint h);

/* —— 持久化 ——
 * 存到主配置文件的 [game_layout] 组：
 *   design_w=800
 *   design_h=400
 *   item0=key:w:40:40:60:60
 *   item1=gamepad:BTN_SOUTH:120:40:60:60
 *   item2=stick_l:280:120:140:140
 * 一行一个控件，纯文本、可手改、易调试。 */

/* 写入 GKeyFile（由 config.c 统一保存文件）。GKeyFile 属于 glib。 */
void gp_layout_save_to_keyfile(const GpLayout *lay, GKeyFile *kf);

/* 从 GKeyFile 读取。没有该组或内容为空时返回默认布局。
 * 格式损坏的行会被跳过并警告，不会让整个布局失效。 */
GpLayout *gp_layout_load_from_keyfile(GKeyFile *kf);

/* 手柄按键码与可读名的互转，供序列化使用 */
const char *gp_button_name(gint code);
gint        gp_button_from_name(const char *name);

/* 内置可选手柄按键表，供编辑模式的下拉菜单使用 */
typedef struct {
    const char *name;
    gint        code;
} GpButtonInfo;

const GpButtonInfo *gp_button_table(gsize *count);

#endif
