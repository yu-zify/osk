#include "layout.h"
#include "gamepad_layout.h"
#include "gamepad_editor.h"
#include "widgets/keybutton.h"
#include "widgets/stick.h"
#include "widgets/touchpad.h"

#include <linux/input-event-codes.h>
#include <string.h>

/* ===== 游戏布局 =====
 *
 * 与其他布局的区别：
 *   - 用 GtkFixed 绝对定位，控件可自由摆放（其他布局是 GtkBox 流式排列）
 *   - 按键走 HOLD 语义：按住即持续按住，松手才释放（真实键盘行为）
 *   - 不响应上滑、不做连发
 *   - 支持编辑模式：拖动排列、增删控件、保存配置
 */

/* ===== 键盘「按住」的引用计数 =====
 * 同一个 code 可能被多个按键引用（例如两个 shift），
 * 用引用计数保证其中一个松开时另一个仍然按着。 */
typedef struct {
    unsigned short code;
    gint           refcount;
} HeldKey;

static GArray *g_held_keys = NULL;

static void held_key_down(App *app, unsigned short code) {
    if (!g_held_keys)
        g_held_keys = g_array_new(FALSE, FALSE, sizeof(HeldKey));

    for (guint i = 0; i < g_held_keys->len; ++i) {
        HeldKey *h = &g_array_index(g_held_keys, HeldKey, i);
        if (h->code == code) {
            if (h->refcount++ == 0)
                osk_keyboard_key_down(app->kbd, code);
            return;
        }
    }

    HeldKey n = { code, 1 };
    g_array_append_val(g_held_keys, n);
    osk_keyboard_key_down(app->kbd, code);
}

static void held_key_up(App *app, unsigned short code) {
    if (!g_held_keys) return;

    for (guint i = 0; i < g_held_keys->len; ++i) {
        HeldKey *h = &g_array_index(g_held_keys, HeldKey, i);
        if (h->code != code) continue;

        if (h->refcount > 0 && --h->refcount == 0)
            osk_keyboard_key_up(app->kbd, code);
        return;
    }
}

/* 切出游戏布局时把所有按住的键放掉，否则会 sticky */
void layout_game_release_all(App *app) {
    if (g_held_keys) {
        for (guint i = 0; i < g_held_keys->len; ++i) {
            HeldKey *h = &g_array_index(g_held_keys, HeldKey, i);
            if (h->refcount > 0)
                osk_keyboard_key_up(app->kbd, h->code);
        }
        g_array_set_size(g_held_keys, 0);
    }
    if (app && app->gamepad)
        osk_gamepad_release_all(app->gamepad);
}

/* ===== HOLD 回调 ===== */

static void on_key_hold_press(const KeyDef *def, gpointer user_data) {
    App *app = user_data;
    if (!app || !def) return;
    held_key_down(app, def->code);
}

static void on_key_hold_release(const KeyDef *def, gpointer user_data) {
    App *app = user_data;
    if (!app || !def) return;
    held_key_up(app, def->code);
}

static void on_gamepad_hold_press(const KeyDef *def, gpointer user_data) {
    App *app = user_data;
    if (!app || !def || !app->gamepad) return;
    osk_gamepad_button(app->gamepad, def->code, TRUE);
}

static void on_gamepad_hold_release(const KeyDef *def, gpointer user_data) {
    App *app = user_data;
    if (!app || !def || !app->gamepad) return;
    osk_gamepad_button(app->gamepad, def->code, FALSE);
}

static void on_mouse_hold_press(const KeyDef *def, gpointer user_data) {
    App *app = user_data;
    if (!app || !def || !app->mouse) return;
    osk_mouse_button(app->mouse, def->code, TRUE);
}

static void on_mouse_hold_release(const KeyDef *def, gpointer user_data) {
    App *app = user_data;
    if (!app || !def || !app->mouse) return;
    osk_mouse_button(app->mouse, def->code, FALSE);
}

/* ===== 虚拟 KeyDef（手柄/鼠标键用） ===== */

/* 手柄和鼠标按键在 keylib 里没有对应的 KeyDef，
 * 这里临时造一个，挂在控件上一起释放。
 * label 用 g_strdup，所以释放时要连它一起 free。 */
static void virtual_def_free(gpointer p) {
    KeyDef *d = p;
    if (!d) return;
    g_free((gpointer)d->label);
    g_free(d);
}

static KeyDef *make_virtual_def(const char *label, guint code,
                                 KeyAction action) {
    KeyDef *d = g_new0(KeyDef, 1);
    d->id        = "virtual";
    d->klass     = KEY_CLASS_FUNCTION;
    d->action    = action;
    d->label     = g_strdup(label);
    d->code      = code;
    d->is_letter = FALSE;
    return d;
}

/* ===== 从 GpItem 造控件 ===== */

/* 前向声明：各 make_* 都要用它，定义在文件后部 */
static gdouble item_alpha(const GpItem *item);

static void apply_transparent_key_style(KeyButton *kb) {
    gtk_style_context_add_class(
        gtk_widget_get_style_context(key_button_widget(kb)),
        "osk-transparent-key");
}

static GtkWidget *make_key_widget(App *app, GpItem *item) {
    const KeyDef *def = keylib_get(item->id ? item->id : "space");
    if (!def) {
        g_printerr("[OSK] 游戏布局：未定义的键 \"%s\"\n",
                   item->id ? item->id : "(null)");
        return NULL;
    }

    KeyButton *kb = key_button_new(def, NULL, NULL, app);
    key_button_set_fixed_size(kb, item->w, item->h);
    key_button_set_hold_mode(kb, on_key_hold_press, on_key_hold_release);
    apply_transparent_key_style(kb);

    /* 游戏布局里按键没有上滑功能，右上角那两个角标字符纯属干扰，
     * 加上这个类由 CSS 彻底隐藏（不只是清空文字，连占位也去掉）。 */
    gtk_style_context_add_class(
        gtk_widget_get_style_context(key_button_widget(kb)),
        "osk-no-swipe-hint");

    /* 背景透明度。按键用 CSS 画背景，没法直接调 alpha，
     * 所以用 widget opacity 整体淡化（文字也跟着变淡，观感一致）。 */
    gtk_widget_set_opacity(key_button_widget(kb), item_alpha(item));
    return key_button_widget(kb);
}

static GtkWidget *make_gamepad_widget(App *app, GpItem *item) {
    KeyDef *def = make_virtual_def(gp_button_name(item->code), item->code,
                                    KEY_ACTION_GAMEPAD_BTN);

    KeyButton *kb = key_button_new(def, NULL, NULL, app);
    key_button_set_fixed_size(kb, item->w, item->h);
    key_button_set_hold_mode(kb, on_gamepad_hold_press,
                              on_gamepad_hold_release);
    apply_transparent_key_style(kb);
    gtk_widget_set_opacity(key_button_widget(kb), item_alpha(item));

    g_object_set_data_full(G_OBJECT(key_button_widget(kb)),
                           "osk-vdef", def, virtual_def_free);
    return key_button_widget(kb);
}

static GtkWidget *make_mouse_widget(App *app, GpItem *item) {
    const char *label = "LMB";
    if (item->code == BTN_RIGHT)  label = "RMB";
    if (item->code == BTN_MIDDLE) label = "MMB";

    KeyDef *def = make_virtual_def(label, item->code,
                                    KEY_ACTION_MOUSE_BTN);

    KeyButton *kb = key_button_new(def, NULL, NULL, app);
    key_button_set_fixed_size(kb, item->w, item->h);
    key_button_set_hold_mode(kb, on_mouse_hold_press,
                              on_mouse_hold_release);
    apply_transparent_key_style(kb);
    gtk_widget_set_opacity(key_button_widget(kb), item_alpha(item));

    g_object_set_data_full(G_OBJECT(key_button_widget(kb)),
                           "osk-vdef", def, virtual_def_free);
    return key_button_widget(kb);
}

/* ===== 编辑模式：透明捕获层 =====
 *
 * 这一层铺满整个画布，位于所有控件之上，负责三件事：
 *   1. 单击某个控件 → 弹出该控件的编辑菜单
 *   2. 拖动某个控件 → 改变位置
 *   3. 在空白区域长按 → 新建一个控件
 *
 * 因为事件先到这一层，下面的控件永远收不到点击，
 * 所以编辑时不会误触发按键 —— 这正是需求要的「控件不调用点击方法」。
 */

typedef enum {
    EDIT_ACTION_NONE,
    EDIT_ACTION_DRAG,       /* 正在拖动某个控件 */
    EDIT_ACTION_MAYBE_TAP,  /* 按下了但还没决定是拖动还是单击 */
} EditAction;

typedef struct {
    App      *app;
    GtkWidget *canvas;      /* GtkFixed，控件都在它里面 */
    GtkWidget *capture;     /* 本捕获层自己 */

    GpItem    *hit;         /* 按下时命中的控件；NULL 表示空白处 */
    GtkWidget *hit_widget;  /* 命中的控件对应的 GtkWidget */

    gint      press_x, press_y;   /* 按下点（画布坐标） */
    gint      orig_x,  orig_y;    /* 命中控件按下前的坐标 */

    EditAction action;
    gboolean   dragging;          /* 是否已经超过拖动阈值 */
    guint      long_press_id;     /* 空白处长按定时器 */

    /* 长按已达成、但菜单要等抬手再弹。
     *
     * 为什么不能当场弹：长按期间手指还按在屏幕上，
     * 如果这时弹出菜单，紧接着的「抬手」事件会被菜单当作
     * 一次外部点击（或直接选中菜单项），菜单立刻消失。
     * 所以这里只做标记，真正 popup 放到 release 里。 */
    gboolean   long_press_armed;
} EditCtx;

/* 命中测试：给画布坐标，找最上层包含该点的控件。
 * 从后往前找，因为后加入的控件在 GtkFixed 里画在上层。 */
static GpItem *hit_test(App *app, GtkWidget *canvas,
                        gint x, gint y, GtkWidget **out_widget) {
    GpState *st = app->gp;
    if (!st || !st->model || !st->model->items) return NULL;

    for (gint i = (gint)st->model->items->len - 1; i >= 0; --i) {
        GpItem *it = g_ptr_array_index(st->model->items, (guint)i);
        if (x >= it->x && x < it->x + it->w &&
            y >= it->y && y < it->y + it->h) {
            if (out_widget) {
                /* 控件在 GtkFixed 里的顺序与模型顺序一致，
                 * 按序号直接取对应 widget。 */
                GList *ch = gtk_container_get_children(
                    GTK_CONTAINER(canvas));
                GtkWidget *target = NULL;
                guint idx = 0;
                for (GList *l = ch; l; l = l->next, ++idx) {
                    if (idx == (guint)i) { target = GTK_WIDGET(l->data); break; }
                }
                g_list_free(ch);
                *out_widget = target;
            }
            return it;
        }
    }
    if (out_widget) *out_widget = NULL;
    return NULL;
}

/* 前向声明 */
static void edit_ctx_free(gpointer p);
static void show_item_menu(EditCtx *ctx, GpItem *item, GdkEventButton *ev);

/* 弹菜单的统一定位方式。
 *
 * 不能用 gtk_menu_popup_at_pointer(menu, NULL)：
 *   它要读「当前正在处理的事件」，而长按菜单是在定时器回调里弹出的，
 *   那时已经脱离了事件处理上下文，GTK 拿不到指针位置，
 *   结果就是菜单不显示（或跳到屏幕角落）。
 *
 * 这里改用 gtk_menu_popup_at_rect，用控件内的坐标自己算一个矩形定位，
 * 定时器场景和事件回调场景都能正常工作。 */
static void popup_menu_at(EditCtx *ctx, GtkWidget *menu,
                           gint x, gint y) {
    GdkWindow *win = gtk_widget_get_window(ctx->capture);
    if (!win) {
        /* 极端情况兜底：没有 GdkWindow 时直接弹，位置可能不准但至少能用 */
        gtk_menu_popup_at_pointer(GTK_MENU(menu), NULL);
        return;
    }

    GdkRectangle rect = { x, y, 1, 1 };
    gtk_menu_popup_at_rect(GTK_MENU(menu), win, &rect,
                            GDK_GRAVITY_NORTH_WEST,
                            GDK_GRAVITY_NORTH_WEST, NULL);
}

/* ---- 空白处长按：新建控件 ---- */

static gboolean on_blank_long_press(gpointer data) {
    EditCtx *ctx = data;
    ctx->long_press_id = 0;

    /* 长按期间如果已经变成拖动，就不新建了 */
    if (ctx->action != EDIT_ACTION_MAYBE_TAP || ctx->hit)
        return G_SOURCE_REMOVE;

    /* 只做标记，不在这里弹菜单 —— 见 EditCtx.long_press_armed 的说明。
     * 菜单会在 on_capture_release 里弹出，那时手指已经离开屏幕，
     * 不会有「抬手事件把菜单关掉」的问题。 */
    ctx->long_press_armed = TRUE;
    return G_SOURCE_REMOVE;
}

static void cancel_long_press(EditCtx *ctx) {
    if (ctx->long_press_id) {
        g_source_remove(ctx->long_press_id);
        ctx->long_press_id = 0;
    }
}

/* ---- 指针事件 ---- */

static gboolean on_capture_press(GtkWidget *w, GdkEventButton *ev,
                                  gpointer data) {
    (void)w;
    EditCtx *ctx = data;
    if (ev->button != GDK_BUTTON_PRIMARY) return TRUE;

    ctx->press_x = (gint)ev->x;
    ctx->press_y = (gint)ev->y;
    ctx->dragging = FALSE;
    ctx->action = EDIT_ACTION_MAYBE_TAP;
    ctx->long_press_armed = FALSE;   /* 每次按下都复位，避免上一轮残留 */

    ctx->hit = hit_test(ctx->app, ctx->canvas, ctx->press_x, ctx->press_y,
                         &ctx->hit_widget);

    if (ctx->hit) {
        ctx->action = EDIT_ACTION_MAYBE_TAP;
        ctx->orig_x = ctx->hit->x;
        ctx->orig_y = ctx->hit->y;
        cancel_long_press(ctx);
    } else {
        /* 空白处：起一个长按定时器 */
        cancel_long_press(ctx);
        ctx->long_press_id = g_timeout_add(600, on_blank_long_press, ctx);
    }
    return TRUE;   /* 吃掉事件，绝不让它传下去 */
}

static gboolean on_capture_motion(GtkWidget *w, GdkEventMotion *ev,
                                   gpointer data) {
    (void)w;
    EditCtx *ctx = data;

    /* 空白处：如果手指移动超过阈值，说明用户不是想长按，取消定时器。
     * 不做这一步的话，按住后滑走再松开也会弹出新建菜单。 */
    if (!ctx->hit) {
        gint mdx = (gint)ev->x - ctx->press_x;
        gint mdy = (gint)ev->y - ctx->press_y;
        if (ABS(mdx) > 12 || ABS(mdy) > 12)
            cancel_long_press(ctx);
        return TRUE;
    }

    /* 按在控件上时：MAYBE_TAP 和 DRAG 两个状态都要继续处理。
     *
     * 这里以前是个 bug：条件写成 action != MAYBE_TAP 就返回，
     * 而第一次移动时 action 刚被置成 DRAG，于是后续所有 motion
     * 都被这一句挡掉 —— 表现就是「只有一帧跟手」。 */
    if (ctx->action != EDIT_ACTION_MAYBE_TAP &&
        ctx->action != EDIT_ACTION_DRAG)
        return TRUE;

    gint dx = (gint)ev->x - ctx->press_x;
    gint dy = (gint)ev->y - ctx->press_y;

    /* 超过阈值才认定为拖动，避免手抖把「单击」变成「移动 1px」 */
    if (!ctx->dragging) {
        if (ABS(dx) < 4 && ABS(dy) < 4)
            return TRUE;
        ctx->dragging = TRUE;
        ctx->action = EDIT_ACTION_DRAG;
    }

    GpItem *it = ctx->hit;
    it->x = ctx->orig_x + dx;
    it->y = ctx->orig_y + dy;

    GpLayout *model = ctx->app->gp->model;
    gp_layout_clamp_item(model, it);

    if (ctx->hit_widget) {
        gtk_fixed_move(GTK_FIXED(ctx->canvas),
                       ctx->hit_widget, it->x, it->y);
    }
    return TRUE;
}

static gboolean on_capture_release(GtkWidget *w, GdkEventButton *ev,
                                    gpointer data) {
    (void)w;
    EditCtx *ctx = data;
    if (ev->button != GDK_BUTTON_PRIMARY) return TRUE;

    cancel_long_press(ctx);

    /* 记录长按状态后再清字段，因为弹菜单要用到 press_x/press_y */
    gboolean armed = ctx->long_press_armed;
    ctx->long_press_armed = FALSE;

    if (ctx->action == EDIT_ACTION_DRAG) {
        /* 拖完了：位置已经写进模型，这里不用额外处理 */
    } else if (ctx->hit) {
        /* 按下又松手、没拖动 → 单击控件，弹菜单 */
        show_item_menu(ctx, ctx->hit, ev);
    } else if (armed) {
        /* 空白处长按达成 → 在这里弹新建菜单。
         * 此时手指已经抬起，不会再有释放事件把菜单关掉。 */
        show_item_menu(ctx, NULL, ev);
    }
    /* 空白处短按：什么都不做 */

    ctx->action = EDIT_ACTION_NONE;
    ctx->hit = NULL;
    ctx->hit_widget = NULL;
    ctx->dragging = FALSE;
    return TRUE;
}

/* ---- 菜单 ---- */

/* 真正执行「改映射」：把选中的键 id 写进 item。
 *
 * 注意这里可能改变 item 的类型：映射菜单里除了键盘键，
 * 还提供鼠标左右键。选中鼠标项时把 type 改成 GP_ITEM_MOUSE、
 * code 改成 BTN_*，控件重建后就成了鼠标键。 */
static void on_pick_key_id(GtkMenuItem *mi, gpointer data) {
    (void)data;
    GpItem *item = g_object_get_data(G_OBJECT(mi), "gp-item");
    const char *id = g_object_get_data(G_OBJECT(mi), "gp-key-id");
    App *app = g_object_get_data(G_OBJECT(mi), "gp-app");
    if (!item) return;

    /* 鼠标键：用 gp-mouse 传 BTN_* 码 */
    gpointer mouse_code = g_object_get_data(G_OBJECT(mi), "gp-mouse");
    if (mouse_code) {
        item->type = GP_ITEM_MOUSE;
        item->code = GPOINTER_TO_INT(mouse_code);
        g_free(item->id);
        item->id = NULL;

        gamepad_save();
        if (app) app_rebuild_layout(app);
        return;
    }

    if (!id) return;

    /* 键盘键：如果之前是鼠标键，类型要改回来。
     * code 也清掉 —— 虽然 make_key_widget 不读它，
     * 但留着会让配置文件里出现无意义的数字。 */
    item->type = GP_ITEM_KEY;
    item->code = 0;
    g_free(item->id);
    item->id = g_strdup(id);

    gamepad_save();
    if (app) app_rebuild_layout(app);   /* 让新映射立刻可见 */
}

static void on_delete_item(GtkMenuItem *mi, gpointer data) {
    (void)data;
    GpItem *item = g_object_get_data(G_OBJECT(mi), "gp-item");
    App *app = g_object_get_data(G_OBJECT(mi), "gp-app");
    if (!item || !app || !app->gp || !app->gp->model) return;

    gp_layout_remove(app->gp->model, item);
    gamepad_save();
    app_rebuild_layout(app);
}

/* 尺寸调整 */
static void on_set_size(GtkMenuItem *mi, gpointer data) {
    (void)data;
    GpItem *item = g_object_get_data(G_OBJECT(mi), "gp-item");
    App *app = g_object_get_data(G_OBJECT(mi), "gp-app");
    gint w = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(mi), "gp-w"));
    gint h = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(mi), "gp-h"));
    if (!item || !app) return;

    item->w = w;
    item->h = h;
    gp_layout_clamp_item(app->gp->model, item);

    gamepad_save();
    app_rebuild_layout(app);
}

/* 新建控件 */
static void on_create_item(GtkMenuItem *mi, gpointer data) {
    (void)data;
    App *app = g_object_get_data(G_OBJECT(mi), "gp-app");
    gint type = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(mi), "gp-type"));
    gint x = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(mi), "gp-x"));
    gint y = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(mi), "gp-y"));
    if (!app || !app->gp || !app->gp->model) return;

    GpItem *it = gp_layout_add(app->gp->model, (GpItemType)type,
                                x, y, 64, 64);
    if (!it) return;

    /* 给个合理的默认内容 */
    if (it->type == GP_ITEM_KEY) {
        it->id = g_strdup("space");
        it->w = 64; it->h = 64;
    } else if (it->type == GP_ITEM_GAMEPAD) {
        it->code = BTN_SOUTH;
    } else if (it->type == GP_ITEM_MOUSE) {
        it->code = BTN_LEFT;
    } else if (it->type == GP_ITEM_STICK_L ||
               it->type == GP_ITEM_STICK_R) {
        it->w = 140; it->h = 140;
    } else if (it->type == GP_ITEM_TOUCHPAD) {
        it->w = 220; it->h = 140;
    }
    gp_layout_clamp_item(app->gp->model, it);

    gamepad_save();
    app_rebuild_layout(app);
}

/* 摇杆专用的「方向绑定」入口。
 * 这项配置有四个下拉框，塞不进右键菜单，所以单独开一个面板。 */
static void on_open_stick_binds(GtkMenuItem *mi, gpointer data) {
    (void)data;
    GpItem *item = g_object_get_data(G_OBJECT(mi), "gp-item");
    App *app = g_object_get_data(G_OBJECT(mi), "gp-app");
    if (!item || !app) return;
    gamepad_stick_binds_open(app, item);
}

/* 切换八向支持 */
static void on_toggle_eight_way(GtkCheckMenuItem *mi, gpointer data) {
    (void)data;
    GpItem *item = g_object_get_data(G_OBJECT(mi), "gp-item");
    App *app = g_object_get_data(G_OBJECT(mi), "gp-app");
    if (!item || !app) return;

    item->eight_way = gtk_check_menu_item_get_active(mi);
    gamepad_save();
    app_rebuild_layout(app);
}

/* 设置背景透明度。alpha 通过 "gp-alpha" 传（乘 100 存成整数，
 * 避免用 double 指针传递）。 */
static void on_set_alpha(GtkMenuItem *mi, gpointer data) {
    (void)data;
    GpItem *item = g_object_get_data(G_OBJECT(mi), "gp-item");
    App *app = g_object_get_data(G_OBJECT(mi), "gp-app");
    gint pct = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(mi), "gp-alpha"));
    if (!item || !app) return;

    item->bg_alpha = pct / 100.0;
    gamepad_save();
    app_rebuild_layout(app);
}

/* ===== 位置与尺寸对话框 =====
 *
 * 拖动虽然直观，但很难精确对齐 —— 多个控件想排成整齐的网格、
 * 或者想精确到某个像素时，拖动做不到。所以这里提供数值输入。
 *
 * 坐标和尺寸用同一个单位（画布像素），这样「x=100, w=100」
 * 就表示从 100 到 200，可以直接心算出对齐位置。
 *
 * 上下文用结构体传递：早先用一个 GtkWidget 指针数组把 GpItem 和 App
 * 强转进去，类型不安全且难读。 */
typedef struct {
    GtkWidget *dlg;
    GtkWidget *spin_x, *spin_y;
    GtkWidget *spin_w, *spin_h;
    GtkWidget *lbl_status;        /* 「已应用」提示 */
    GpItem    *item;
    App       *app;
} GeometryDlg;

static void on_geometry_ok(GtkWidget *w, gpointer data) {
    (void)w;
    GeometryDlg *d = data;

    d->item->x = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(d->spin_x));
    d->item->y = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(d->spin_y));
    d->item->w = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(d->spin_w));
    d->item->h = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(d->spin_h));

    /* 钳制要在四个值都写完之后做：先设 x 后设 w 时，
     * 中间态可能超出画布而被错误夹住。 */
    gp_layout_clamp_item(d->app->gp->model, d->item);

    gamepad_save();
    app_rebuild_layout(d->app);

    /* 不关闭对话框 —— 定位往往需要反复试几组数字，
     * 每次都要重新打开就太麻烦了。
     *
     * 但要把钳制后的真实值写回输入框：用户填了超界的数字时，
     * 界面上的数据必须和实际生效的一致，否则会以为设置没生效。 */
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(d->spin_x), d->item->x);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(d->spin_y), d->item->y);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(d->spin_w), d->item->w);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(d->spin_h), d->item->h);

    if (d->lbl_status)
        gtk_label_set_text(GTK_LABEL(d->lbl_status), "已应用");
}

static void on_geometry_cancel(GtkWidget *w, gpointer data) {
    (void)w;
    GeometryDlg *d = data;
    gtk_widget_destroy(d->dlg);
}

/* 对话框销毁时清掉主窗口上记录的句柄，
 * 否则下次点击菜单会拿到已释放的窗口指针。 */
static void on_geometry_destroy(GtkWidget *w, gpointer data) {
    (void)w;
    App *app = data;
    if (app && app->window &&
        g_object_get_data(G_OBJECT(app->window), "osk-geomdlg-win") == w)
        g_object_set_data(G_OBJECT(app->window), "osk-geomdlg-win", NULL);
}

/* 造一行「标签 + 数值框」 */
static GtkWidget *geometry_row(const char *label, const char *unit,
                                gint min, gint max, gint value,
                                GtkWidget **out_spin) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    GtkWidget *l = gtk_label_new(label);
    gtk_widget_set_size_request(l, 48, -1);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(row), l, FALSE, FALSE, 0);

    /* step 设为 1：坐标和尺寸都以像素为单位，逐像素微调 */
    GtkWidget *spin = gtk_spin_button_new_with_range(min, max, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value);
    gtk_widget_set_can_focus(spin, FALSE);
    gtk_box_pack_start(GTK_BOX(row), spin, TRUE, TRUE, 0);

    if (unit) {
        GtkWidget *u = gtk_label_new(unit);
        gtk_style_context_add_class(gtk_widget_get_style_context(u),
                                     "osk-dim");
        gtk_box_pack_start(GTK_BOX(row), u, FALSE, FALSE, 0);
    }

    *out_spin = spin;
    return row;
}

static void on_edit_geometry(GtkMenuItem *mi, gpointer data) {
    (void)data;
    GpItem *item = g_object_get_data(G_OBJECT(mi), "gp-item");
    App *app = g_object_get_data(G_OBJECT(mi), "gp-app");
    if (!item || !app) return;

    /* 已经为这个控件开着对话框就复用，不要叠新的。
     * 用 gp_item 记录当前对话框绑的是哪个控件。 */
    GtkWidget *existing = g_object_get_data(G_OBJECT(app->window),
                                             "osk-geomdlg-win");
    if (existing) {
        GpItem *cur = g_object_get_data(G_OBJECT(existing), "gp_item");
        if (cur == item) {
            gtk_window_present(GTK_WINDOW(existing));
            return;
        }
        /* 绑的是别的控件：先关掉旧的，再为当前控件开新的 */
        gtk_widget_destroy(existing);
    }

    GeometryDlg *d = g_new0(GeometryDlg, 1);
    d->item = item;
    d->app  = app;

    GtkWidget *dlg = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    d->dlg = dlg;
    gtk_window_set_title(GTK_WINDOW(dlg), "位置与尺寸");
    gtk_window_set_default_size(GTK_WINDOW(dlg), 300, 240);
    gtk_window_set_keep_above(GTK_WINDOW(dlg), TRUE);
    gtk_window_set_position(GTK_WINDOW(dlg), GTK_WIN_POS_CENTER);

    /* 与主窗口一致：无系统装饰，自绘白边 */
    app_setup_subwindow(dlg);

    /* 非模态：应用后要能看到画布上的实际效果，还能继续拖动别的控件 */
    gtk_window_set_modal(GTK_WINDOW(dlg), FALSE);

    /* 记录到主窗口，便于复用与清理 */
    g_object_set_data(G_OBJECT(app->window), "osk-geomdlg-win", dlg);
    g_object_set_data(G_OBJECT(dlg), "gp_item", item);

    /* 统一外壳：标题栏（可拖动）+ 关闭按钮 */
    GtkWidget *content = app_subwindow_shell(dlg, "位置与尺寸");

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 14);
    gtk_box_pack_start(GTK_BOX(content), box, TRUE, TRUE, 0);

    /* 内容装好后再挂白边 */
    app_finish_subwindow(dlg);

    GtkWidget *hint = gtk_label_new("坐标与尺寸使用同一单位（画布像素）");
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(hint),
                                 "osk-dim");
    gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                       FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), geometry_row("X", "px", 0, 8000,
                                                   item->x, &d->spin_x),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), geometry_row("Y", "px", 0, 8000,
                                                   item->y, &d->spin_y),
                       FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                       FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), geometry_row("宽", "px", 24, 8000,
                                                   item->w, &d->spin_w),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), geometry_row("高", "px", 24, 8000,
                                                   item->h, &d->spin_h),
                       FALSE, FALSE, 0);

    GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    /* 按钮语义：「应用」反复微调后不关窗，「关闭」才退出。
     * 定位常常要试好几组数字，每次都重开对话框太麻烦。 */
    GtkWidget *ok = gtk_button_new_with_label("应用");
    GtkWidget *close_btn = gtk_button_new_with_label("关闭");
    gtk_widget_set_can_focus(ok, FALSE);
    gtk_widget_set_can_focus(close_btn, FALSE);
    gtk_box_pack_start(GTK_BOX(btns), ok, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btns), close_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), btns, FALSE, FALSE, 0);

    /* 状态提示：应用后显示「已应用」，并给出钳制后的实际值 */
    d->lbl_status = gtk_label_new("");
    gtk_widget_set_halign(d->lbl_status, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(d->lbl_status),
                                 "osk-dim");
    gtk_box_pack_start(GTK_BOX(box), d->lbl_status, FALSE, FALSE, 0);

    g_signal_connect(ok, "clicked", G_CALLBACK(on_geometry_ok), d);
    g_signal_connect(close_btn, "clicked",
                     G_CALLBACK(on_geometry_cancel), d);
    g_signal_connect(dlg, "destroy",
                     G_CALLBACK(on_geometry_destroy), app);
    g_object_set_data_full(G_OBJECT(dlg), "osk-geomdlg", d, g_free);

    gtk_widget_show_all(dlg);
}


/* 添加一个「子菜单项」，挂上 item/app 等上下文 */
static GtkWidget *menu_item_with(const char *label, GCallback cb,
                                  GpItem *item, App *app) {
    GtkWidget *mi = gtk_menu_item_new_with_label(label);
    g_object_set_data(G_OBJECT(mi), "gp-item", item);
    g_object_set_data(G_OBJECT(mi), "gp-app", app);
    g_signal_connect(mi, "activate", cb, NULL);
    return mi;
}

/* 菜单关闭后销毁它。
 * 每次弹菜单都新建一个 GtkMenu，不回收会一直堆积
 * （编辑时反复点选，几十次就积几十个菜单窗口）。
 *
 * 用 idle 延后一拍再销毁：selection-done 是在菜单项回调返回前后发出的，
 * 此时菜单还在信号发射栈上，直接 destroy 有重入风险。
 * 挂到主循环空闲时销毁最稳妥。 */
static gboolean destroy_menu_idle(gpointer data) {
    GtkWidget *menu = data;
    if (menu) gtk_widget_destroy(menu);
    return G_SOURCE_REMOVE;
}

static void on_menu_closed(GtkMenuShell *shell, gpointer data) {
    (void)data;
    g_idle_add(destroy_menu_idle, shell);
}

static void show_item_menu(EditCtx *ctx, GpItem *item, GdkEventButton *ev) {
    (void)ev;
    App *app = ctx->app;

    GtkWidget *menu = gtk_menu_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(menu),
                                 "osk-menu");

    /* 关掉就销毁，避免堆积 */
    g_signal_connect(menu, "selection-done",
                     G_CALLBACK(on_menu_closed), NULL);

    if (!item) {
        /* ---- 新建菜单 ---- */
        GtkWidget *title = gtk_menu_item_new_with_label("在此处添加控件");
        gtk_widget_set_sensitive(title, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), title);

        struct { const char *label; GpItemType type; } adds[] = {
            { "键盘按键", GP_ITEM_KEY      },
            { "手柄按键", GP_ITEM_GAMEPAD  },
            { "鼠标按键", GP_ITEM_MOUSE    },
            { "左摇杆",   GP_ITEM_STICK_L  },
            { "右摇杆",   GP_ITEM_STICK_R  },
            { "触控板",   GP_ITEM_TOUCHPAD },
        };

        for (gsize i = 0; i < G_N_ELEMENTS(adds); ++i) {
            GtkWidget *mi = menu_item_with(adds[i].label,
                                            G_CALLBACK(on_create_item),
                                            NULL, app);
            g_object_set_data(G_OBJECT(mi), "gp-type",
                              GINT_TO_POINTER((gint)adds[i].type));
            g_object_set_data(G_OBJECT(mi), "gp-x",
                              GINT_TO_POINTER(ctx->press_x));
            g_object_set_data(G_OBJECT(mi), "gp-y",
                              GINT_TO_POINTER(ctx->press_y));
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
        }

        gtk_widget_show_all(menu);
        popup_menu_at(ctx, menu, ctx->press_x, ctx->press_y);
        return;
    }

    /* ---- 已有控件的菜单 ---- */

    /* 键盘按键 / 鼠标按键：都提供映射菜单。
     *
     * 两类共用同一个菜单是有意为之 —— 否则把键盘键改成鼠标键之后
     * 就再也改不回来了（菜单里没有映射项）。 */
    if (item->type == GP_ITEM_KEY || item->type == GP_ITEM_MOUSE) {
        GtkWidget *sub = gtk_menu_item_new_with_label("按键映射");
        GtkWidget *submenu = gtk_menu_new();

        gsize n = 0;
        const KeyDef *all = keylib_all(&n);
        for (gsize i = 0; i < n; ++i) {
            /* 只列能真正发按键的项，跳过 page 这种控件键 */
            if (all[i].action != KEY_ACTION_EMIT) continue;

            GtkWidget *mi = gtk_menu_item_new_with_label(all[i].label);
            g_object_set_data(G_OBJECT(mi), "gp-item", item);
            g_object_set_data(G_OBJECT(mi), "gp-app", app);
            g_object_set_data(G_OBJECT(mi), "gp-key-id",
                              (gpointer)all[i].id);
            g_signal_connect(mi, "activate",
                             G_CALLBACK(on_pick_key_id), NULL);
            gtk_menu_shell_append(GTK_MENU_SHELL(submenu), mi);
        }

        /* 鼠标左右键：游戏里常用按键代替鼠标，
         * 把它们放进同一个映射菜单，选中后控件会变成鼠标键。 */
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu),
                              gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));

        struct { const char *label; guint code; } mouses[] = {
            { "鼠标左键", BTN_LEFT  },
            { "鼠标右键", BTN_RIGHT },
            { "鼠标中键", BTN_MIDDLE },
        };
        for (gsize i = 0; i < G_N_ELEMENTS(mouses); ++i) {
            GtkWidget *mi = gtk_menu_item_new_with_label(mouses[i].label);
            g_object_set_data(G_OBJECT(mi), "gp-item", item);
            g_object_set_data(G_OBJECT(mi), "gp-app", app);
            g_object_set_data(G_OBJECT(mi), "gp-mouse",
                              GINT_TO_POINTER((gint)mouses[i].code));
            g_signal_connect(mi, "activate",
                             G_CALLBACK(on_pick_key_id), NULL);
            gtk_menu_shell_append(GTK_MENU_SHELL(submenu), mi);
        }

        gtk_menu_item_set_submenu(GTK_MENU_ITEM(sub), submenu);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), sub);
    }

    /* 摇杆：方向键绑定 + 八向开关 */
    if (item->type == GP_ITEM_STICK_L || item->type == GP_ITEM_STICK_R) {
        GtkWidget *bind = menu_item_with("方向键绑定…",
                                          G_CALLBACK(on_open_stick_binds),
                                          item, app);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), bind);

        /* 八向支持：勾选后斜推会同时触发两个方向 */
        GtkWidget *eight = gtk_check_menu_item_new_with_label("八向支持");
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(eight),
                                        item->eight_way);
        g_object_set_data(G_OBJECT(eight), "gp-item", item);
        g_object_set_data(G_OBJECT(eight), "gp-app", app);
        g_signal_connect(eight, "toggled",
                         G_CALLBACK(on_toggle_eight_way), NULL);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), eight);
    }

    /* 背景透明度：所有控件通用 */
    {
        GtkWidget *sub = gtk_menu_item_new_with_label("背景透明度");
        GtkWidget *submenu = gtk_menu_new();

        static const gint alphas[] = { 0, 20, 40, 55, 75, 100 };
        for (gsize i = 0; i < G_N_ELEMENTS(alphas); ++i) {
            char buf[32];
            g_snprintf(buf, sizeof(buf), "%d%%", alphas[i]);

            GtkWidget *mi = menu_item_with(buf, G_CALLBACK(on_set_alpha),
                                            item, app);
            g_object_set_data(G_OBJECT(mi), "gp-alpha",
                              GINT_TO_POINTER(alphas[i]));
            gtk_menu_shell_append(GTK_MENU_SHELL(submenu), mi);
        }
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(sub), submenu);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), sub);
    }

    /* 位置与尺寸：数值输入，便于精确对齐。
     * 拖动适合粗调，但要排成整齐网格或精确到某个像素时只能靠数值。 */
    {
        GtkWidget *geom = menu_item_with("位置与尺寸…",
                                          G_CALLBACK(on_edit_geometry),
                                          item, app);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), geom);
    }

    /* 尺寸快捷：常用档位，比开对话框快 */
    {
        GtkWidget *sub = gtk_menu_item_new_with_label("尺寸快捷");
        GtkWidget *submenu = gtk_menu_new();

        static const gint sizes[][2] = {
            { 48, 48 }, { 64, 64 }, { 80, 80 }, { 96, 96 },
            { 140, 140 }, { 200, 200 },
        };
        for (gsize i = 0; i < G_N_ELEMENTS(sizes); ++i) {
            char buf[32];
            g_snprintf(buf, sizeof(buf), "%d × %d", sizes[i][0], sizes[i][1]);

            GtkWidget *mi = menu_item_with(buf, G_CALLBACK(on_set_size),
                                            item, app);
            g_object_set_data(G_OBJECT(mi), "gp-w",
                              GINT_TO_POINTER(sizes[i][0]));
            g_object_set_data(G_OBJECT(mi), "gp-h",
                              GINT_TO_POINTER(sizes[i][1]));
            gtk_menu_shell_append(GTK_MENU_SHELL(submenu), mi);
        }
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(sub), submenu);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), sub);
    }

    /* 删除 */
    {
        GtkWidget *del = menu_item_with("删除此控件",
                                         G_CALLBACK(on_delete_item),
                                         item, app);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), del);
    }

    gtk_widget_show_all(menu);
    popup_menu_at(ctx, menu, ctx->press_x, ctx->press_y);
}

/* ---- 生命周期 ---- */

static void edit_ctx_free(gpointer p) {
    EditCtx *ctx = p;
    if (!ctx) return;
    cancel_long_press(ctx);
    g_free(ctx);
}

static GtkWidget *make_edit_capture(App *app, GtkWidget *canvas) {
    EditCtx *ctx = g_new0(EditCtx, 1);
    ctx->app = app;
    ctx->canvas = canvas;

    GtkWidget *area = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(area), FALSE);
    gtk_widget_set_can_focus(area, FALSE);

    gtk_widget_add_events(area,
                          GDK_BUTTON_PRESS_MASK   |
                          GDK_BUTTON_RELEASE_MASK |
                          GDK_POINTER_MOTION_MASK);

    g_signal_connect(area, "button-press-event",
                     G_CALLBACK(on_capture_press), ctx);
    g_signal_connect(area, "motion-notify-event",
                     G_CALLBACK(on_capture_motion), ctx);
    g_signal_connect(area, "button-release-event",
                     G_CALLBACK(on_capture_release), ctx);

    g_object_set_data_full(G_OBJECT(area), "osk-editctx", ctx, edit_ctx_free);

    /* 捕获层必须进输入区，否则透明布局下点击会穿透到桌面 */
    g_object_set_data(G_OBJECT(area), "osk-input", GINT_TO_POINTER(1));

    ctx->capture = area;
    return area;
}

/* ===== 构建画布 ===== */

/* 把 GpItem 里的四方向绑定写入摇杆控件。
 * dir_id 存的是 keylib 的 id（"up" / "w" ...），这里翻译成键码。 */
static void apply_stick_binds(StickWidget *sw, const GpItem *item) {
    if (!sw || !item) return;

    static const StickDir order[STICK_DIR_COUNT] = {
        STICK_DIR_UP, STICK_DIR_DOWN, STICK_DIR_LEFT, STICK_DIR_RIGHT
    };

    for (int d = 0; d < STICK_DIR_COUNT; ++d) {
        const char *id = item->dir_id[d];
        if (!id || !*id) continue;

        const KeyDef *kd = keylib_get(id);
        if (!kd) {
            g_printerr("[OSK] 摇杆方向绑定：未定义的键 \"%s\"\n", id);
            continue;
        }
        stick_widget_bind_dir(sw, order[d], kd->code);
    }

    /* 八向模式 */
    stick_widget_set_eight_way(sw, item->eight_way);
}

/* 控件的背景透明度。
 * bg_alpha < 0 表示沿用该类型的默认值：
 *   摇杆 0.55（半透明底盘，能看见游戏画面）
 *   其他 0.55（和 .osk-transparent-key 的默认观感一致） */
static gdouble item_alpha(const GpItem *item) {
    if (item->bg_alpha >= 0.0)
        return CLAMP(item->bg_alpha, 0.0, 1.0);
    return 0.55;
}

static GtkWidget *make_item_widget(App *app, GpItem *item) {
    switch (item->type) {
    case GP_ITEM_KEY:      return make_key_widget(app, item);
    case GP_ITEM_GAMEPAD:  return make_gamepad_widget(app, item);
    case GP_ITEM_MOUSE:    return make_mouse_widget(app, item);

    case GP_ITEM_STICK_L: {
        StickWidget *sw = stick_widget_new(app->gamepad, FALSE, app->kbd);
        apply_stick_binds(sw, item);
        stick_widget_set_bg_alpha(sw, item_alpha(item));
        return stick_widget_gtk(sw);
    }
    case GP_ITEM_STICK_R: {
        StickWidget *sw = stick_widget_new(app->gamepad, TRUE, app->kbd);
        apply_stick_binds(sw, item);
        stick_widget_set_bg_alpha(sw, item_alpha(item));
        return stick_widget_gtk(sw);
    }
    case GP_ITEM_TOUCHPAD: {
        TouchpadWidget *tw = touchpad_widget_new(app->mouse, app);
        GtkWidget *area = touchpad_widget_gtk(tw);
        gtk_widget_set_opacity(area, item_alpha(item));
        return area;
    }
    }
    return NULL;
}

/* 画布尺寸变化时同步给模型，保证拖动钳制用的是真实边界。
 *
 * 不这么做的话，gp_layout_clamp_item 会拿一个过时的 design_w/h 去夹坐标，
 * 在大屏幕上就会出现「拖到屏幕中间就卡住」。 */
static gboolean on_canvas_size(GtkWidget *w, GtkAllocation *a, gpointer data) {
    (void)w;
    App *app = data;
    if (!app || !app->gp || !app->gp->model) return FALSE;

    gamepad_sync_canvas_size(app->gp->model, a->width, a->height);
    return FALSE;
}

static GtkWidget *game_create(App *app) {
    /* 模型由 main() 在启动时从配置载入；这里再兜一次底，
     * 保证任何入口（比如编辑面板先于布局初始化打开）都不会拿到空模型。 */
    GpLayout *model = gamepad_ensure_model(app);
    GpState  *st    = app->gp;
    gboolean  editing = st && st->editing;

    /* 画布：绝对定位 */
    GtkWidget *fixed = gtk_fixed_new();
    gtk_widget_set_halign(fixed, GTK_ALIGN_FILL);
    gtk_widget_set_valign(fixed, GTK_ALIGN_FILL);
    gtk_style_context_add_class(gtk_widget_get_style_context(fixed),
                                "osk-gamepad-canvas");

    /* 记录画布实际尺寸，供拖动时钳制用 */
    g_signal_connect(fixed, "size-allocate",
                     G_CALLBACK(on_canvas_size), app);

    if (!model) return fixed;

    for (guint i = 0; i < model->items->len; ++i) {
        GpItem *item = g_ptr_array_index(model->items, i);

        GtkWidget *w = make_item_widget(app, item);
        if (!w) continue;

        gtk_fixed_put(GTK_FIXED(fixed), w, item->x, item->y);
        gtk_widget_set_size_request(w, item->w, item->h);
    }

    if (!editing)
        return fixed;

    /* ===== 编辑模式 =====
     *
     * 用一个覆盖整个画布的透明层接管所有指针事件。
     *
     * 为什么不在每个控件上单独禁用：
     *   控件种类太多（KeyButton 的 EventBox、摇杆的 DrawingArea、
     *   触控板的 DrawingArea、GtkButton），逐个屏蔽容易漏，
     *   而且控件内部的手势/信号一旦触发就会真的发出按键事件 ——
     *   编辑时点一下 W 就真的打了个 w 出去，这显然不行。
     *
     * 覆盖层在 GtkOverlay 里位于最上层，事件先到它，控件收不到，
     * 天然实现「编辑时控件不响应点击」。
     *
     * 覆盖层自己负责三件事：
     *   - 单击某个控件 → 弹菜单（映射 / 大小 / 删除）
     *   - 拖动某个控件 → 改位置
     *   - 在空白处长按 → 新建控件
     */
    GtkWidget *overlay = gtk_overlay_new();
    gtk_widget_set_halign(overlay, GTK_ALIGN_FILL);
    gtk_widget_set_valign(overlay, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(overlay), fixed);

    GtkWidget *capture = make_edit_capture(app, fixed);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), capture);

    /* 让覆盖层跟 fixed 一样大、铺满 */
    gtk_widget_set_hexpand(capture, TRUE);
    gtk_widget_set_vexpand(capture, TRUE);

    return overlay;
}

static const LayoutOps game_ops = {
    .name = "game",
    .display_name = "游戏",
    .transparent = TRUE,          /* 输入穿透 */
    .visual_transparent = TRUE,   /* 视觉透明 */
    .hide_drag = TRUE,            /* 铺满屏幕，顶栏不放拖动区 */
    .create = game_create,
};

const LayoutOps *layout_game_ops(void) {
    return &game_ops;
}
