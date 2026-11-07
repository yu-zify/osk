#include "app.h"
#include "layout/gamepad_model.h"

#include <errno.h>
#include <glib/gstdio.h>

/* ===== 配置文件 =====
 *
 * 用 GKeyFile（ini 风格）而不是 GSettings：
 *   - 不依赖 dconf / gsettings schema 的安装流程，meson 里少一步
 *   - 用户可以手改，出错也容易看懂
 *   - 单文件，方便带着走
 *
 * 格式：
 *   [appearance]
 *   key_width=50
 *   key_height=60
 *   key_spacing=1
 *   opacity=1.0
 *   font_size=28
 *   corner_font_size=12
 */

#define GROUP_APPEARANCE "appearance"

#define KEY_WIDTH        "key_width"
#define KEY_HEIGHT       "key_height"
#define KEY_SPACING      "key_spacing"
#define KEY_OPACITY      "opacity"
#define KEY_PAD_OPACITY  "pad_opacity"
#define KEY_MOUSE_SPEED  "mouse_speed"
#define KEY_FONT_SIZE    "font_size"
#define KEY_CORNER_SIZE  "corner_font_size"

/* 路径只算一次。调用方拿到的是静态字符串，不要释放。 */
const char *config_path(void) {
    static char *path = NULL;
    if (!path)
        path = g_build_filename(g_get_user_config_dir(),
                                "osk", "config.ini", NULL);
    return path;
}

void config_set_defaults(OskConfig *cfg) {
    if (!cfg) return;
    cfg->key_width        = OSK_KEY_WIDTH_DEFAULT;
    cfg->key_height       = OSK_KEY_HEIGHT_DEFAULT;
    cfg->key_spacing      = OSK_KEY_SPACING_DEFAULT;
    cfg->opacity          = OSK_OPACITY_DEFAULT;
    cfg->pad_opacity      = OSK_OPACITY_DEFAULT;
    cfg->mouse_speed      = OSK_MOUSE_SPEED_DEFAULT;
    cfg->font_size        = OSK_FONT_SIZE_DEFAULT;
    cfg->corner_font_size = OSK_CORNER_FONT_SIZE_DEFAULT;
}

/* 文件里的值可能被手改坏了（负数、超大数、非数字），统一在这里兜住，
 * 免得后面拿去做 gtk_widget_set_size_request 或 CSS 拼接时出怪状。 */
static void sanitize(OskConfig *cfg) {
    cfg->key_width  = CLAMP(cfg->key_width,
                            OSK_KEY_WIDTH_MIN, OSK_KEY_WIDTH_MAX);
    cfg->key_height = CLAMP(cfg->key_height,
                            OSK_KEY_HEIGHT_MIN, OSK_KEY_HEIGHT_MAX);
    cfg->key_spacing = CLAMP(cfg->key_spacing,
                             OSK_KEY_SPACING_MIN, OSK_KEY_SPACING_MAX);
    cfg->font_size  = CLAMP(cfg->font_size,
                            OSK_FONT_SIZE_MIN, OSK_FONT_SIZE_MAX);
    cfg->corner_font_size = CLAMP(cfg->corner_font_size,
                                  OSK_CORNER_FONT_SIZE_MIN,
                                  OSK_CORNER_FONT_SIZE_MAX);

    /* NaN 的比较永远是 false，所以这一句同时挡住 NaN 和 0：
     * g_key_file_get_double 读到 "nan" 会成功返回 NaN。 */
    if (!(cfg->opacity >= OSK_OPACITY_MIN))
        cfg->opacity = OSK_OPACITY_DEFAULT;
    cfg->opacity = CLAMP(cfg->opacity, OSK_OPACITY_MIN, OSK_OPACITY_MAX);

    if (!(cfg->pad_opacity >= OSK_OPACITY_MIN))
        cfg->pad_opacity = OSK_OPACITY_DEFAULT;
    cfg->pad_opacity = CLAMP(cfg->pad_opacity,
                              OSK_OPACITY_MIN, OSK_OPACITY_MAX);

    if (!(cfg->mouse_speed >= OSK_MOUSE_SPEED_MIN))
        cfg->mouse_speed = OSK_MOUSE_SPEED_DEFAULT;
    cfg->mouse_speed = CLAMP(cfg->mouse_speed,
                              OSK_MOUSE_SPEED_MIN, OSK_MOUSE_SPEED_MAX);
}

/* 读一个整数键；键不存在或不是整数时保留原值（即默认值）。 */
static void read_int(GKeyFile *kf, const char *key, gint *out) {
    GError *err = NULL;
    gint v = g_key_file_get_integer(kf, GROUP_APPEARANCE, key, &err);
    if (err) {
        g_clear_error(&err);   /* 缺失是正常情况，用默认值就好 */
        return;
    }
    *out = v;
}

static void read_double(GKeyFile *kf, const char *key, gdouble *out) {
    GError *err = NULL;
    gdouble v = g_key_file_get_double(kf, GROUP_APPEARANCE, key, &err);
    if (err) {
        g_clear_error(&err);
        return;
    }
    *out = v;
}

gboolean config_load(OskConfig *cfg) {
    if (!cfg) return FALSE;

    config_set_defaults(cfg);

    const char *path = config_path();
    if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
        g_print("[OSK] 未找到配置文件，使用默认外观：%s\n", path);
        return FALSE;
    }

    GKeyFile *kf = g_key_file_new();
    GError *err = NULL;
    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, &err)) {
        g_printerr("[OSK] 读取配置失败，回退到默认外观：%s\n",
                   err ? err->message : "未知错误");
        g_clear_error(&err);
        g_key_file_free(kf);
        return FALSE;
    }

    read_int(kf, KEY_WIDTH,       &cfg->key_width);
    read_int(kf, KEY_HEIGHT,      &cfg->key_height);
    read_int(kf, KEY_SPACING,     &cfg->key_spacing);
    read_int(kf, KEY_FONT_SIZE,   &cfg->font_size);
    read_int(kf, KEY_CORNER_SIZE, &cfg->corner_font_size);
    read_double(kf, KEY_OPACITY,  &cfg->opacity);
    read_double(kf, KEY_PAD_OPACITY, &cfg->pad_opacity);
    read_double(kf, KEY_MOUSE_SPEED, &cfg->mouse_speed);

    g_key_file_free(kf);

    sanitize(cfg);
    g_print("[OSK] 已载入配置：%s\n", path);
    return TRUE;
}

/* 读出现有配置文件；不存在或损坏时返回一个空的 GKeyFile。
 * 保存时要在原有内容上合并，不能直接新建 —— 否则保存外观设置会把
 * [game_layout] 组整段抹掉，用户的游戏布局就丢了。 */
static GKeyFile *load_or_new(void) {
    GKeyFile *kf = g_key_file_new();
    const char *path = config_path();

    if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
        GError *err = NULL;
        if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, &err)) {
            /* 文件损坏：保留旧文件不覆盖，交给调用方决定 */
            g_printerr("[OSK] 配置文件无法解析，将重新生成：%s\n",
                       err ? err->message : "未知错误");
            g_clear_error(&err);
            g_key_file_free(kf);
            return NULL;
        }
    }
    return kf;
}

/* 确保配置目录存在。返回 FALSE 表示无法创建。 */
static gboolean ensure_config_dir(void) {
    const char *path = config_path();
    char *dir = g_path_get_dirname(path);

    gboolean ok = TRUE;
    if (g_mkdir_with_parents(dir, 0700) != 0) {
        g_printerr("[OSK] 无法创建配置目录 %s：%s\n", dir, g_strerror(errno));
        ok = FALSE;
    }
    g_free(dir);
    return ok;
}

/* 把 GKeyFile 落盘。g_key_file_save_to_file 内部先写临时文件再 rename，
 * 写一半断电不会留下半截配置。 */
static gboolean save_keyfile(GKeyFile *kf) {
    if (!kf) return FALSE;
    if (!ensure_config_dir()) {
        g_key_file_free(kf);
        return FALSE;
    }

    GError *err = NULL;
    gboolean ok = g_key_file_save_to_file(kf, config_path(), &err);
    if (!ok) {
        g_printerr("[OSK] 保存配置失败：%s\n",
                   err ? err->message : "未知错误");
        g_clear_error(&err);
    }
    g_key_file_free(kf);
    return ok;
}

/* 把外观参数写入已打开的 kf（不落盘） */
static void write_appearance(GKeyFile *kf, const OskConfig *cfg) {
    /* 数值都先钳制再落盘，保证文件里永远是合法值 */
    OskConfig c = *cfg;
    sanitize(&c);

    g_key_file_set_integer(kf, GROUP_APPEARANCE, KEY_WIDTH,  c.key_width);
    g_key_file_set_integer(kf, GROUP_APPEARANCE, KEY_HEIGHT, c.key_height);
    g_key_file_set_integer(kf, GROUP_APPEARANCE, KEY_SPACING, c.key_spacing);
    g_key_file_set_double (kf, GROUP_APPEARANCE, KEY_OPACITY,  c.opacity);
    g_key_file_set_double (kf, GROUP_APPEARANCE, KEY_PAD_OPACITY,
                            c.pad_opacity);
    g_key_file_set_double (kf, GROUP_APPEARANCE, KEY_MOUSE_SPEED,
                            c.mouse_speed);
    g_key_file_set_integer(kf, GROUP_APPEARANCE, KEY_FONT_SIZE, c.font_size);
    g_key_file_set_integer(kf, GROUP_APPEARANCE, KEY_CORNER_SIZE,
                           c.corner_font_size);
}

gboolean config_save(const OskConfig *cfg) {
    if (!cfg) return FALSE;

    /* 合并写入：保留文件里已有的 [game_layout] 等组 */
    GKeyFile *kf = load_or_new();
    if (!kf) return FALSE;

    write_appearance(kf, cfg);
    return save_keyfile(kf);
}

/* ===== 游戏布局的存取 =====
 *
 * 游戏布局和外观参数放同一个文件（不同组），这样用户只需要记住一个路径。
 * 两个 save 都走「读出→改自己的组→写回」的流程，互不覆盖。
 */

void config_save_gamepad(const GpLayout *lay) {
    if (!lay) return;

    GKeyFile *kf = load_or_new();
    if (!kf) return;

    gp_layout_save_to_keyfile(lay, kf);
    save_keyfile(kf);
}

GpLayout *config_load_gamepad(void) {
    GKeyFile *kf = g_key_file_new();
    const char *path = config_path();

    /* 文件不存在是正常情况（首次运行），直接用默认布局 */
    if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
        g_key_file_free(kf);
        return gp_layout_new_default();
    }

    GError *err = NULL;
    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, &err)) {
        g_printerr("[OSK] 读取游戏布局失败，使用默认：%s\n",
                   err ? err->message : "未知错误");
        g_clear_error(&err);
        g_key_file_free(kf);
        return gp_layout_new_default();
    }

    GpLayout *lay = gp_layout_load_from_keyfile(kf);
    g_key_file_free(kf);
    return lay;
}
