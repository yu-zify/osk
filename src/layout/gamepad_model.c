#include "gamepad_model.h"

#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <string.h>

#define GP_GROUP          "game_layout"
#define GP_KEY_DESIGN_W   "design_w"
#define GP_KEY_DESIGN_H   "design_h"
#define GP_KEY_ITEM_FMT   "item%d"

/* 画布的设计尺寸。控件坐标都按这个尺寸记录，
 * 换到别的分辨率时等比缩放，布局不会散架。 */
#define GP_DEFAULT_W 800
#define GP_DEFAULT_H 400

/* ===== 手柄按键表 ===== */

static const GpButtonInfo g_buttons[] = {
    { "A",       BTN_SOUTH  },
    { "B",       BTN_EAST   },
    { "X",       BTN_NORTH  },
    { "Y",       BTN_WEST   },
    { "LB",      BTN_TL     },
    { "RB",      BTN_TR     },
    { "Select",  BTN_SELECT },
    { "Start",   BTN_START  },
    { "Up",      BTN_DPAD_UP    },
    { "Down",    BTN_DPAD_DOWN  },
    { "Left",    BTN_DPAD_LEFT  },
    { "Right",   BTN_DPAD_RIGHT },
};

const GpButtonInfo *gp_button_table(gsize *count) {
    if (count) *count = G_N_ELEMENTS(g_buttons);
    return g_buttons;
}

const char *gp_button_name(gint code) {
    for (gsize i = 0; i < G_N_ELEMENTS(g_buttons); ++i)
        if (g_buttons[i].code == code)
            return g_buttons[i].name;
    return "?";
}

gint gp_button_from_name(const char *name) {
    if (!name) return -1;
    for (gsize i = 0; i < G_N_ELEMENTS(g_buttons); ++i)
        if (strcmp(g_buttons[i].name, name) == 0)
            return g_buttons[i].code;
    return -1;
}

/* ===== 生命周期 ===== */

static void item_free(gpointer p) {
    GpItem *it = p;
    if (!it) return;
    g_free(it->id);
    g_free(it->label);
    for (int d = 0; d < GP_DIR_COUNT; ++d)
        g_free(it->dir_id[d]);
    g_free(it);
}

static GpItem *item_new(GpItemType type, gint x, gint y, gint w, gint h) {
    GpItem *it = g_new0(GpItem, 1);
    it->type = type;
    it->x = x;
    it->y = y;
    it->w = w;
    it->h = h;
    it->code = 0;
    /* 负数表示「用该控件类型的默认透明度」，构建时再决定 */
    it->bg_alpha = -1.0;
    return it;
}

GpLayout *gp_layout_copy(const GpLayout *src) {
    GpLayout *dst = g_new0(GpLayout, 1);
    dst->items = g_ptr_array_new_with_free_func(item_free);
    dst->design_w = src ? src->design_w : GP_DEFAULT_W;
    dst->design_h = src ? src->design_h : GP_DEFAULT_H;

    if (!src || !src->items) return dst;

    for (guint i = 0; i < src->items->len; ++i) {
        GpItem *s = g_ptr_array_index(src->items, i);
        GpItem *d = item_new(s->type, s->x, s->y, s->w, s->h);
        d->id    = g_strdup(s->id);
        d->label = g_strdup(s->label);
        d->code  = s->code;
        d->eight_way = s->eight_way;
        d->bg_alpha  = s->bg_alpha;
        for (int k = 0; k < GP_DIR_COUNT; ++k)
            d->dir_id[k] = g_strdup(s->dir_id[k]);
        g_ptr_array_add(dst->items, d);
    }
    return dst;
}

void gp_layout_free(GpLayout *lay) {
    if (!lay) return;
    if (lay->items) g_ptr_array_free(lay->items, TRUE);
    g_free(lay);
}

/* ===== 增删改 ===== */

/* 把控件限制在画布内。
 *
 * 注意 W/H 取自 lay->design_w/design_h，而这两个值在运行时会被
 * 同步成画布的真实尺寸（见 gamepad_sync_canvas_size）。
 * 早先它们固定是 800×400，而画布在透明布局下铺满整个屏幕，
 * 结果就是「拖到屏幕中间就停住，拖不过去」。
 *
 * 如果画布尺寸还没同步过（比如首次构建），保持原样不动 ——
 * 宁可让坐标暂时超界，也不要在未知画布大小的情况下把控件挤到左上角。 */
void gp_layout_clamp_item(GpLayout *lay, GpItem *it) {
    if (!lay || !it) return;

    if (it->w < 24) it->w = 24;
    if (it->h < 24) it->h = 24;

    gint W = lay->design_w;
    gint H = lay->design_h;

    /* 还没有有效画布尺寸时只做尺寸下限，不限制位置 */
    if (W <= 0 || H <= 0) return;

    if (it->w > W) it->w = W;
    if (it->h > H) it->h = H;

    if (it->x < 0) it->x = 0;
    if (it->y < 0) it->y = 0;
    if (it->x > W - it->w) it->x = W - it->w;
    if (it->y > H - it->h) it->y = H - it->h;
}

void gamepad_sync_canvas_size(GpLayout *lay, gint w, gint h) {
    if (!lay) return;
    if (w > 0) lay->design_w = w;
    if (h > 0) lay->design_h = h;
}

GpItem *gp_layout_add(GpLayout *lay, GpItemType type,
                      gint x, gint y, gint w, gint h) {
    if (!lay || !lay->items) return NULL;
    GpItem *it = item_new(type, x, y, w, h);
    gp_layout_clamp_item(lay, it);
    g_ptr_array_add(lay->items, it);
    return it;
}

void gp_layout_remove(GpLayout *lay, GpItem *item) {
    if (!lay || !lay->items || !item) return;
    /* 用 remove_fast 会打乱顺序，但布局顺序不影响渲染（GtkFixed 按坐标画），
     * 这里仍然用 remove 保持稳定顺序，方便调试时对照配置文件。 */
    g_ptr_array_remove(lay->items, item);
}

/* ===== 默认布局 =====
 * 参考手机游戏的屏幕分区：左侧方向盘、右侧动作键、底部功能键。
 * 坐标按 800×400 设计。 */
GpLayout *gp_layout_new_default(void) {
    GpLayout *lay = g_new0(GpLayout, 1);
    lay->items = g_ptr_array_new_with_free_func(item_free);
    lay->design_w = GP_DEFAULT_W;
    lay->design_h = GP_DEFAULT_H;

    /* —— 左侧：WASD（真实键盘按住语义）—— */
    GpItem *it;
    it = gp_layout_add(lay, GP_ITEM_KEY, 60, 120, 64, 64); it->id = g_strdup("w");
    it = gp_layout_add(lay, GP_ITEM_KEY, 60, 190, 64, 64); it->id = g_strdup("s");
    it = gp_layout_add(lay, GP_ITEM_KEY, 20, 155, 64, 64); it->id = g_strdup("a");
    it = gp_layout_add(lay, GP_ITEM_KEY, 100, 155, 64, 64); it->id = g_strdup("d");

    /* —— 右侧：动作键 —— */
    it = gp_layout_add(lay, GP_ITEM_KEY, 660, 190, 64, 64); it->id = g_strdup("space");
    it = gp_layout_add(lay, GP_ITEM_KEY, 700, 120, 64, 64); it->id = g_strdup("shift");
    it = gp_layout_add(lay, GP_ITEM_KEY, 620, 120, 64, 64); it->id = g_strdup("ctrl");

    /* —— 左下：左摇杆 —— */
    gp_layout_add(lay, GP_ITEM_STICK_L, 30, 250, 140, 140);

    /* —— 右下：右摇杆 —— */
    gp_layout_add(lay, GP_ITEM_STICK_R, 630, 250, 140, 140);

    return lay;
}

/* ===== 序列化 =====
 *
 * 每行格式：  <type>:<arg>:<x>:<y>:<w>:<h>
 *   type = key | gamepad | mouse | stick_l | stick_r | touchpad
 *   arg  = key 的 keylib id，或 btn 的数字码（用名字也行，见 parse）
 */

static const char *type_to_str(GpItemType t) {
    switch (t) {
        case GP_ITEM_KEY:      return "key";
        case GP_ITEM_GAMEPAD:  return "gamepad";
        case GP_ITEM_MOUSE:    return "mouse";
        case GP_ITEM_STICK_L:  return "stick_l";
        case GP_ITEM_STICK_R:  return "stick_r";
        case GP_ITEM_TOUCHPAD: return "touchpad";
        case GP_ITEM_WHEEL:    return "wheel";
    }
    return "key";
}

static GpItemType type_from_str(const char *s) {
    if (!s) return GP_ITEM_KEY;
    if (strcmp(s, "key")      == 0) return GP_ITEM_KEY;
    if (strcmp(s, "gamepad")  == 0) return GP_ITEM_GAMEPAD;
    if (strcmp(s, "mouse")    == 0) return GP_ITEM_MOUSE;
    if (strcmp(s, "stick_l")  == 0) return GP_ITEM_STICK_L;
    if (strcmp(s, "stick_r")  == 0) return GP_ITEM_STICK_R;
    if (strcmp(s, "touchpad") == 0) return GP_ITEM_TOUCHPAD;
    if (strcmp(s, "wheel")    == 0) return GP_ITEM_WHEEL;
    return GP_ITEM_KEY;
}

void gp_layout_save_to_keyfile(const GpLayout *lay, GKeyFile *kf) {
    if (!lay || !kf) return;

    /* 先清掉旧内容，避免删掉控件后残留旧行 */
    g_key_file_remove_group(kf, GP_GROUP, NULL);

    g_key_file_set_integer(kf, GP_GROUP, GP_KEY_DESIGN_W, lay->design_w);
    g_key_file_set_integer(kf, GP_GROUP, GP_KEY_DESIGN_H, lay->design_h);

    for (guint i = 0; i < lay->items->len; ++i) {
        GpItem *it = g_ptr_array_index(lay->items, i);

        char *arg;
        switch (it->type) {
        case GP_ITEM_KEY:
            arg = g_strdup(it->id ? it->id : "space");
            break;
        case GP_ITEM_GAMEPAD:
            arg = g_strdup_printf("%s", gp_button_name(it->code));
            break;
        case GP_ITEM_MOUSE:
            arg = g_strdup_printf("%d", it->code);
            break;
        default:
            arg = g_strdup("-");     /* 摇杆/触控板没有 arg */
            break;
        }

        /* 基础部分：type:arg:x:y:w:h —— 所有控件都有这六段 */
        GString *line = g_string_new(NULL);
        g_string_append_printf(line, "%s:%s:%d:%d:%d:%d",
                               type_to_str(it->type), arg,
                               it->x, it->y, it->w, it->h);

        /* 第 7、8 段：八向标志、背景透明度。
         * 这两项对所有控件都有意义（透明度尤其），所以放在方向绑定之前。
         * 旧配置文件只有六段，解析时缺这两段就取默认值。 */
        g_string_append_printf(line, ":%d:%.2f",
                               it->eight_way ? 1 : 0,
                               it->bg_alpha);

        /* 摇杆再追加四段方向绑定：up:down:left:right
         * 未绑定的方向写 "-"。
         * 全部追加在末尾，parse 时按段数逐段可选处理。 */
        if (it->type == GP_ITEM_STICK_L || it->type == GP_ITEM_STICK_R) {
            for (int d = 0; d < GP_DIR_COUNT; ++d)
                g_string_append_printf(line, ":%s",
                                       it->dir_id[d] ? it->dir_id[d] : "-");
        }

        char *key = g_strdup_printf(GP_KEY_ITEM_FMT, i);
        g_key_file_set_string(kf, GP_GROUP, key, line->str);
        g_free(key);
        g_string_free(line, TRUE);
        g_free(arg);
    }
}

/* 解析一行。
 *
 * 格式（从旧到新，逐段可选）：
 *   type:arg:x:y:w:h                    旧格式，六段
 *   type:arg:x:y:w:h:eight:alpha        加八向与透明度
 *   type:arg:x:y:w:h:eight:alpha:up:down:left:right   摇杆再加方向绑定
 *
 * 段数不够就按默认值处理，所以旧配置文件依然能读。 */
static GpItem *parse_item_line(const char *line) {
    if (!line) return NULL;

    /* 最多 12 段：6 基础 + 2 样式 + 4 方向 */
    gchar **parts = g_strsplit(line, ":", 12);
    guint n = g_strv_length(parts);

    if (n < 6) {
        g_printerr("[OSK] 游戏布局：跳过格式错误的行 \"%s\"\n", line);
        g_strfreev(parts);
        return NULL;
    }

    GpItemType type = type_from_str(parts[0]);

    GpItem *it = item_new(type,
                          atoi(parts[2]), atoi(parts[3]),
                          atoi(parts[4]), atoi(parts[5]));

    if (type == GP_ITEM_KEY) {
        it->id = g_strdup(parts[1]);
    } else if (type == GP_ITEM_GAMEPAD) {
        /* 优先按名字解析；配置里写数字码也接受 */
        gint code = gp_button_from_name(parts[1]);
        if (code < 0)
            code = atoi(parts[1]);
        it->code = code;
    } else if (type == GP_ITEM_MOUSE) {
        it->code = atoi(parts[1]);
    }

    /* 第 7、8 段：八向标志、背景透明度（可选） */
    if (n >= 7)
        it->eight_way = (atoi(parts[6]) != 0);
    if (n >= 8) {
        gdouble a = g_ascii_strtod(parts[7], NULL);
        /* 只接受 0..1；"-1" 之类表示沿用默认，保持 item_new 的初值 */
        if (a >= 0.0 && a <= 1.0)
            it->bg_alpha = a;
    }

    /* 摇杆的四方向绑定（第 9~12 段，可选） */
    if (type == GP_ITEM_STICK_L || type == GP_ITEM_STICK_R) {
        for (int d = 0; d < GP_DIR_COUNT; ++d) {
            guint idx = 8 + (guint)d;
            if (idx >= n) break;                 /* 旧格式，没有这一段 */
            if (strcmp(parts[idx], "-") == 0) continue;
            it->dir_id[d] = g_strdup(parts[idx]);
        }
    }

    g_strfreev(parts);
    return it;
}

GpLayout *gp_layout_load_from_keyfile(GKeyFile *kf) {
    if (!kf || !g_key_file_has_group(kf, GP_GROUP))
        return gp_layout_new_default();

    GpLayout *lay = g_new0(GpLayout, 1);
    lay->items = g_ptr_array_new_with_free_func(item_free);

    GError *err = NULL;
    gint w = g_key_file_get_integer(kf, GP_GROUP, GP_KEY_DESIGN_W, &err);
    if (err) { g_clear_error(&err); w = GP_DEFAULT_W; }
    gint h = g_key_file_get_integer(kf, GP_GROUP, GP_KEY_DESIGN_H, &err);
    if (err) { g_clear_error(&err); h = GP_DEFAULT_H; }

    lay->design_w = w > 0 ? w : GP_DEFAULT_W;
    lay->design_h = h > 0 ? h : GP_DEFAULT_H;

    /* 逐行读，直到某个序号缺失为止。
     * 用「读到缺失就停」而不是遍历 GKeyFile 所有键，是为了让顺序稳定，
     * 也避免读到用户手写进去的无关键。 */
    for (gint i = 0; ; ++i) {
        char *key = g_strdup_printf(GP_KEY_ITEM_FMT, i);
        char *line = g_key_file_get_string(kf, GP_GROUP, key, NULL);
        g_free(key);

        if (!line) break;

        GpItem *it = parse_item_line(line);
        g_free(line);

        if (it) {
            gp_layout_clamp_item(lay, it);
            g_ptr_array_add(lay->items, it);
        }
    }

    /* 文件里有组但一个控件都没解析出来 → 退回默认，避免出现空白画布 */
    if (lay->items->len == 0) {
        gp_layout_free(lay);
        g_printerr("[OSK] 游戏布局为空，使用默认布局\n");
        return gp_layout_new_default();
    }

    return lay;
}
