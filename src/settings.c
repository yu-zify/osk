#include "settings.h"

/* （app_rebuild_layout / app_apply_config / app_apply_font 声明在 app.h） */
typedef struct {
    App       *app;
    GtkWidget *window;

    /* 控件 */
    GtkWidget *scale_opacity;
    GtkWidget *scale_pad_opacity;
    GtkWidget *scale_mouse_speed;
    GtkWidget *spin_width;
    GtkWidget *spin_height;
    GtkWidget *spin_spacing;
    GtkWidget *spin_font;
    GtkWidget *spin_corner;
    GtkWidget *lbl_opacity;
    GtkWidget *lbl_pad_opacity;
    GtkWidget *lbl_mouse_speed;
    GtkWidget *lbl_size;

    guint save_timeout;      /* 透明度滑块落盘用的延时源 */
} SettingsUI;

/* ===== 配置落盘 ===== */

/* 把当前 config 写回文件。设置改动很小、频率也低，直接写就行；
 * 这样即使程序被 kill 或者崩溃，下次启动仍然是用户调好的样子。 */
static void persist(SettingsUI *ui) {
    config_save(&ui->app->config);
}

/* 透明度滑块的延时落盘：拖动过程中反复改值，只在停下来之后写一次磁盘 */
static gboolean opacity_save_tick(gpointer data) {
    SettingsUI *ui = data;
    ui->save_timeout = 0;
    persist(ui);
    return G_SOURCE_REMOVE;
}

static void on_opacity_changed(GtkRange *range, gpointer data) {
    SettingsUI *ui = data;
    ui->app->config.opacity = gtk_range_get_value(range);

    char buf[32];
    g_snprintf(buf, sizeof(buf), "%.0f%%",
               ui->app->config.opacity * 100.0);
    gtk_label_set_text(GTK_LABEL(ui->lbl_opacity), buf);

    app_apply_config(ui->app);

    /* 拖动滑块每移动一格都会走这里，不能每格都写一次磁盘。
     * 延后 400ms 落盘，拖动过程中只写最后一次。 */
    if (ui->save_timeout)
        g_source_remove(ui->save_timeout);
    ui->save_timeout = g_timeout_add(400, opacity_save_tick, ui);
}

/* 触控板透明度：只影响触控板弹窗，不动主窗口 */
static void on_pad_opacity_changed(GtkRange *range, gpointer data) {
    SettingsUI *ui = data;
    ui->app->config.pad_opacity = gtk_range_get_value(range);

    char buf[32];
    g_snprintf(buf, sizeof(buf), "%.0f%%",
               ui->app->config.pad_opacity * 100.0);
    gtk_label_set_text(GTK_LABEL(ui->lbl_pad_opacity), buf);

    app_apply_pad_config(ui->app);

    /* 与主透明度共用同一个落盘延时源 */
    if (ui->save_timeout)
        g_source_remove(ui->save_timeout);
    ui->save_timeout = g_timeout_add(400, opacity_save_tick, ui);
}

/* 触控板指针速度。触控板控件每次都从 config 读，所以这里
 * 只需更新 config 并落盘，不需要重建控件。 */
static void on_mouse_speed_changed(GtkRange *range, gpointer data) {
    SettingsUI *ui = data;
    ui->app->config.mouse_speed = gtk_range_get_value(range);

    char buf[32];
    g_snprintf(buf, sizeof(buf), "%.2f×", ui->app->config.mouse_speed);
    gtk_label_set_text(GTK_LABEL(ui->lbl_mouse_speed), buf);

    if (ui->save_timeout)
        g_source_remove(ui->save_timeout);
    ui->save_timeout = g_timeout_add(400, opacity_save_tick, ui);
}

/* GtkSpinButton 的 value-changed / GtkButton 的 clicked 都走这个函数，
 * 统一按当前控件值同步 config 并重排布局。 */
static void apply_size_from_ui(SettingsUI *ui) {
    ui->app->config.key_width   = gtk_spin_button_get_value_as_int(
        GTK_SPIN_BUTTON(ui->spin_width));
    ui->app->config.key_height  = gtk_spin_button_get_value_as_int(
        GTK_SPIN_BUTTON(ui->spin_height));
    ui->app->config.key_spacing = gtk_spin_button_get_value_as_int(
        GTK_SPIN_BUTTON(ui->spin_spacing));

    char buf[64];
    g_snprintf(buf, sizeof(buf), "%d × %d  间距 %d",
               ui->app->config.key_width,
               ui->app->config.key_height,
               ui->app->config.key_spacing);
    gtk_label_set_text(GTK_LABEL(ui->lbl_size), buf);

    app_rebuild_layout(ui->app);
    persist(ui);
}

/* GtkSpinButton::value-changed */
static void on_size_spin_changed(GtkSpinButton *btn, gpointer data) {
    (void)btn;
    apply_size_from_ui((SettingsUI *)data);
}

/* 字号：只重载动态 CSS，不需要重建布局。
 * 主标签字号和右上角上滑字符字号一起同步，两者共用一次 CSS 重载。 */
static void apply_font_from_ui(SettingsUI *ui) {
    ui->app->config.font_size = gtk_spin_button_get_value_as_int(
        GTK_SPIN_BUTTON(ui->spin_font));
    ui->app->config.corner_font_size = gtk_spin_button_get_value_as_int(
        GTK_SPIN_BUTTON(ui->spin_corner));
    app_apply_font(ui->app);
    persist(ui);
}

/* GtkSpinButton::value-changed —— 字号单独处理 */
static void on_font_spin_changed(GtkSpinButton *btn, gpointer data) {
    (void)btn;
    apply_font_from_ui((SettingsUI *)data);
}

/* GtkButton::clicked */
static void on_apply_clicked(GtkButton *btn, gpointer data) {
    (void)btn;
    apply_size_from_ui((SettingsUI *)data);
    apply_font_from_ui((SettingsUI *)data);
}

static void on_reset(GtkButton *btn, gpointer data) {
    (void)btn;
    SettingsUI *ui = data;

    /* 默认值统一从 app.h 的那组宏取，不再各写一份数字 */
    gtk_range_set_value(GTK_RANGE(ui->scale_opacity), OSK_OPACITY_DEFAULT);
    gtk_range_set_value(GTK_RANGE(ui->scale_pad_opacity),
                         OSK_OPACITY_DEFAULT);
    gtk_range_set_value(GTK_RANGE(ui->scale_mouse_speed),
                         OSK_MOUSE_SPEED_DEFAULT);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->spin_width),
                              OSK_KEY_WIDTH_DEFAULT);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->spin_height),
                              OSK_KEY_HEIGHT_DEFAULT);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->spin_spacing),
                              OSK_KEY_SPACING_DEFAULT);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->spin_font),
                              OSK_FONT_SIZE_DEFAULT);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->spin_corner),
                              OSK_CORNER_FONT_SIZE_DEFAULT);

    /* 上面每次 set_value 都会触发 value-changed 回调，各自落盘一次；
     * 这里再显式同步一遍，保证 config 和控件值完全一致。 */
    apply_size_from_ui(ui);
    apply_font_from_ui(ui);
}

static GtkWidget *make_labeled_row(const char *title, GtkWidget *control,
                                    GtkWidget *value_label) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *lbl = gtk_label_new(title);
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_widget_set_size_request(lbl, 64, -1);
    gtk_box_pack_start(GTK_BOX(row), lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), control, TRUE, TRUE, 0);
    if (value_label) {
        gtk_widget_set_size_request(value_label, 96, -1);
        gtk_widget_set_halign(value_label, GTK_ALIGN_END);
        gtk_box_pack_start(GTK_BOX(row), value_label, FALSE, FALSE, 0);
    }
    return row;
}

static GtkWidget *make_spin(gint min, gint max, gint step, gint value) {
    GtkWidget *spin = gtk_spin_button_new_with_range(min, max, step);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value);
    gtk_widget_set_can_focus(spin, FALSE);
    return spin;
}

/* 设置窗口销毁时释放 SettingsUI。
 * 关键：先撤掉未到期的落盘定时器，否则它在 ui 已经 free 之后才触发，
 * 就是 use-after-free（和触控板那个 tap_timer 同一类问题）。 */
static void settings_ui_free(gpointer data) {
    SettingsUI *ui = data;
    if (!ui) return;
    if (ui->save_timeout) {
        g_source_remove(ui->save_timeout);
        ui->save_timeout = 0;
    }
    g_free(ui);
}

static void on_settings_destroy(GtkWidget *w, gpointer data) {
    App *app = data;
    if (app && app->window)
        g_object_set_data(G_OBJECT(app->window), "osk-settings-window", NULL);
    (void)w;
}

void settings_open(App *app) {
    if (!app) return;

    /* 已打开就不重复创建 */
    GtkWidget *existing = g_object_get_data(G_OBJECT(app->window),
                                            "osk-settings-window");
    if (existing) {
        gtk_window_present(GTK_WINDOW(existing));
        return;
    }

    SettingsUI *ui = g_new0(SettingsUI, 1);
    ui->app = app;

    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    ui->window = win;
    gtk_window_set_title(GTK_WINDOW(win), "设置");
    gtk_window_set_default_size(GTK_WINDOW(win), 420, 340);
    gtk_window_set_keep_above(GTK_WINDOW(win), TRUE);
    gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_CENTER);

    /* 与主窗口一致：不要系统装饰，改用自绘白边 */
    app_setup_subwindow(win);

    g_object_set_data(G_OBJECT(app->window), "osk-settings-window", win);
    g_object_set_data_full(G_OBJECT(win), "osk-settings-ui", ui,
                           settings_ui_free);
    /* 关闭时清掉主窗口上的句柄，避免悬空指针 */
    g_signal_connect(win, "destroy", G_CALLBACK(on_settings_destroy), app);

    /* 统一外壳：标题栏 + 拖动 + 关闭按钮 */
    GtkWidget *content = app_subwindow_shell(win, "设置");

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(box), 14);
    gtk_box_pack_start(GTK_BOX(content), box, TRUE, TRUE, 0);

    /* 内容装好后再挂白边 —— 白边画在外壳外沿，不会陷进内容里 */
    app_finish_subwindow(win);

    /* --- 透明度 --- */
    GtkWidget *lbl_op = gtk_label_new("");
    ui->lbl_opacity = lbl_op;
    ui->scale_opacity = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                  OSK_OPACITY_MIN,
                                                  OSK_OPACITY_MAX, 0.01);
    gtk_scale_set_value_pos(GTK_SCALE(ui->scale_opacity), GTK_POS_LEFT);
    gtk_range_set_value(GTK_RANGE(ui->scale_opacity), app->config.opacity);
    gtk_widget_set_hexpand(ui->scale_opacity, TRUE);
    g_signal_connect(ui->scale_opacity, "value-changed",
                     G_CALLBACK(on_opacity_changed), ui);
    gtk_box_pack_start(GTK_BOX(box),
                       make_labeled_row("透明度", ui->scale_opacity, lbl_op),
                       FALSE, FALSE, 0);

    /* --- 触控板透明度 ---
     * 触控板是独立窗口，和键盘的透明度独立控制：
     * 键盘通常要看得清，触控板往往希望更透一点以免挡视线。 */
    GtkWidget *lbl_pad = gtk_label_new("");
    ui->lbl_pad_opacity = lbl_pad;
    ui->scale_pad_opacity = gtk_scale_new_with_range(
        GTK_ORIENTATION_HORIZONTAL, OSK_OPACITY_MIN, OSK_OPACITY_MAX, 0.01);
    gtk_scale_set_value_pos(GTK_SCALE(ui->scale_pad_opacity), GTK_POS_LEFT);
    gtk_range_set_value(GTK_RANGE(ui->scale_pad_opacity),
                         app->config.pad_opacity);
    gtk_widget_set_hexpand(ui->scale_pad_opacity, TRUE);
    g_signal_connect(ui->scale_pad_opacity, "value-changed",
                     G_CALLBACK(on_pad_opacity_changed), ui);
    gtk_box_pack_start(GTK_BOX(box),
                       make_labeled_row("触控板透明度",
                                         ui->scale_pad_opacity, lbl_pad),
                       FALSE, FALSE, 0);

    /* --- 指针速度 ---
     * 影响触控板拖动时指针移动的倍率。1.00× 是基准。 */
    GtkWidget *lbl_speed = gtk_label_new("");
    ui->lbl_mouse_speed = lbl_speed;
    ui->scale_mouse_speed = gtk_scale_new_with_range(
        GTK_ORIENTATION_HORIZONTAL,
        OSK_MOUSE_SPEED_MIN, OSK_MOUSE_SPEED_MAX, 0.05);
    gtk_scale_set_value_pos(GTK_SCALE(ui->scale_mouse_speed), GTK_POS_LEFT);
    gtk_range_set_value(GTK_RANGE(ui->scale_mouse_speed),
                         app->config.mouse_speed);
    gtk_widget_set_hexpand(ui->scale_mouse_speed, TRUE);
    g_signal_connect(ui->scale_mouse_speed, "value-changed",
                     G_CALLBACK(on_mouse_speed_changed), ui);
    gtk_box_pack_start(GTK_BOX(box),
                       make_labeled_row("指针速度",
                                         ui->scale_mouse_speed, lbl_speed),
                       FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box),
                       gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                       FALSE, FALSE, 0);

    /* --- 按键尺寸 --- */
    ui->spin_width   = make_spin(OSK_KEY_WIDTH_MIN, OSK_KEY_WIDTH_MAX, 1,
                                 app->config.key_width);
    ui->spin_height  = make_spin(OSK_KEY_HEIGHT_MIN, OSK_KEY_HEIGHT_MAX, 1,
                                 app->config.key_height);
    ui->spin_spacing = make_spin(OSK_KEY_SPACING_MIN, OSK_KEY_SPACING_MAX, 1,
                                 app->config.key_spacing);
    ui->spin_font    = make_spin(OSK_FONT_SIZE_MIN, OSK_FONT_SIZE_MAX, 1,
                                 app->config.font_size);
    ui->spin_corner  = make_spin(OSK_CORNER_FONT_SIZE_MIN,
                                 OSK_CORNER_FONT_SIZE_MAX, 1,
                                 app->config.corner_font_size);

    gtk_box_pack_start(GTK_BOX(box),
                       make_labeled_row("键宽", ui->spin_width, NULL),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box),
                       make_labeled_row("键高", ui->spin_height, NULL),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box),
                       make_labeled_row("间距", ui->spin_spacing, NULL),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box),
                       make_labeled_row("字号", ui->spin_font, NULL),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box),
                       make_labeled_row("角标字号", ui->spin_corner, NULL),
                       FALSE, FALSE, 0);

    GtkWidget *lbl_size = gtk_label_new("");
    ui->lbl_size = lbl_size;

    GtkWidget *apply = gtk_button_new_with_label("应用尺寸");
    gtk_widget_set_can_focus(apply, FALSE);
    g_signal_connect(apply, "clicked", G_CALLBACK(on_apply_clicked), ui);

    GtkWidget *reset = gtk_button_new_with_label("恢复默认");
    gtk_widget_set_can_focus(reset, FALSE);
    g_signal_connect(reset, "clicked", G_CALLBACK(on_reset), ui);

    /* 微调框直接改数值也立刻生效 */
    g_signal_connect(ui->spin_width, "value-changed",
                     G_CALLBACK(on_size_spin_changed), ui);
    g_signal_connect(ui->spin_height, "value-changed",
                     G_CALLBACK(on_size_spin_changed), ui);
    g_signal_connect(ui->spin_spacing, "value-changed",
                     G_CALLBACK(on_size_spin_changed), ui);
    g_signal_connect(ui->spin_font, "value-changed",
                     G_CALLBACK(on_font_spin_changed), ui);
    g_signal_connect(ui->spin_corner, "value-changed",
                     G_CALLBACK(on_font_spin_changed), ui);

    GtkWidget *btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(btns), apply, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btns), reset, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), btns, FALSE, FALSE, 0);

    gtk_widget_set_halign(lbl_size, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(box), lbl_size, FALSE, FALSE, 0);

    /* 初始文案 */
    on_opacity_changed(GTK_RANGE(ui->scale_opacity), ui);
    on_pad_opacity_changed(GTK_RANGE(ui->scale_pad_opacity), ui);
    on_mouse_speed_changed(GTK_RANGE(ui->scale_mouse_speed), ui);
    {
        char buf[64];
        g_snprintf(buf, sizeof(buf), "%d × %d  间距 %d",
                   app->config.key_width,
                   app->config.key_height,
                   app->config.key_spacing);
        gtk_label_set_text(GTK_LABEL(lbl_size), buf);
    }

    gtk_widget_show_all(win);
}
