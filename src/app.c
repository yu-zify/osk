#include "app.h"

static gboolean shift_active(const App *app) {
    return app->shift_state != SHIFT_OFF;
}

/* Shift 或 Caps 任一激活时，字母键显示/输入大写 */
static gboolean want_upper(const App *app) {
    return shift_active(app) || app->caps;
}

/* ===== 修饰键外观 =====
 * 统一用按钮颜色表示激活状态，不再在按钮上显示圆点。
 *   Shift 单次 → 蓝色 (mod-oneshot)
 *   Shift 锁定 → 红色 (mod-locked)
 *   Ctrl / Alt / Win / Caps 激活 → 蓝色 (mod-oneshot)
 */
static void update_shift_button(KeyButton *kb, ShiftState state) {
    if (!kb) return;
    key_button_set_label(kb, "Shift");
    key_button_set_active_class(kb, "mod-oneshot", state == SHIFT_ONESHOT);
    key_button_set_active_class(kb, "mod-locked",  state == SHIFT_LOCKED);
}

/* Ctrl/Alt/Win/Caps 的激活外观。
 *
 * 用 mod-oneshot（蓝色）而不是 mod-locked（红色）：
 *   - 蓝色表示「只对下一个键生效」，符合 Ctrl/Alt/Win 的单次语义
 *   - Caps 是真正的锁定，但为了和这几个键保持一致的观感也用蓝色；
 *     如果希望 Caps 用红色区分，把下面拆成两个函数即可。 */
static void update_plain_modifier(KeyButton *kb, const char *name,
                                   gboolean active) {
    if (!kb) return;
    key_button_set_label(kb, name);
    key_button_set_active_class(kb, "mod-oneshot", active);
}

/* ===== 按键行为 ===== */

/* Shift 三态循环：
 *   点一下 → 蓝色，单次大写（输入一个字符后自动恢复）
 *   点两下 → 红色，锁定大写
 *   点三下 → 恢复普通颜色，回到小写
 */
static void on_shift_clicked(App *app) {
    switch (app->shift_state) {
    case SHIFT_OFF:     app->shift_state = SHIFT_ONESHOT; break;
    case SHIFT_ONESHOT: app->shift_state = SHIFT_LOCKED;  break;
    case SHIFT_LOCKED:
    default:            app->shift_state = SHIFT_OFF;     break;
    }
    app_refresh_labels(app);
}

/* Ctrl / Alt / Win：两态循环 —— 点一下变蓝色单次生效，再点取消。
 *
 * 不提供「锁定」档是有意的：这几个键一旦锁定，之后每个字符都会带上
 * 修饰键，很容易出现「按什么都没反应」的困惑。单次足够覆盖
 * Ctrl+C / Alt+Tab 这类常见组合。 */
static void on_plain_modifier_clicked(ModState *st) {
    *st = (*st == MOD_OFF) ? MOD_ONESHOT : MOD_OFF;
}

static void on_modifier_clicked(App *app, const KeyDef *def) {
    switch (def->modifier_bit) {
    case OSK_MOD_SHIFT:
        on_shift_clicked(app);
        return;   /* on_shift_clicked 内部已经 refresh 了 */
    case OSK_MOD_CTRL:  on_plain_modifier_clicked(&app->ctrl); break;
    case OSK_MOD_ALT:   on_plain_modifier_clicked(&app->alt);  break;
    case OSK_MOD_META:  on_plain_modifier_clicked(&app->meta); break;
    case OSK_MOD_CAPS:  app->caps = !app->caps; break;
    default: return;
    }
    app_refresh_labels(app);
}

/* 主键是否需要与 Shift 一起发送 */
static gboolean need_shift_mod(const App *app, const KeyDef *def) {
    if (def->klass == KEY_CLASS_CHAR) {
        if (def->is_letter) {
            /* 字母：Shift 或 Caps 任一激活即为大写 */
            return want_upper(app);
        }
        /* 数字/符号：只有 Shift 生效时才需要，Caps 不影响 */
        return shift_active(app);
    }
    return shift_active(app);
}

static gsize collect_modifiers(const App *app, const KeyDef *def,
                                unsigned short *mods) {
    gsize n = 0;

    if (need_shift_mod(app, def)) mods[n++] = KEY_LEFTSHIFT;
    if (app->ctrl == MOD_ONESHOT) mods[n++] = KEY_LEFTCTRL;
    if (app->alt  == MOD_ONESHOT) mods[n++] = KEY_LEFTALT;
    if (app->meta == MOD_ONESHOT) mods[n++] = KEY_LEFTMETA;
    return n;
}

/* 一个键发出去之后，把所有「单次」修饰键复位。
 *
 * Shift 有单独的 consume_oneshot（因为它还带锁定档），
 * Ctrl/Alt/Win 统一走这里。 */
static void consume_plain_modifiers(App *app) {
    gboolean changed = FALSE;

    if (app->ctrl == MOD_ONESHOT) { app->ctrl = MOD_OFF; changed = TRUE; }
    if (app->alt  == MOD_ONESHOT) { app->alt  = MOD_OFF; changed = TRUE; }
    if (app->meta == MOD_ONESHOT) { app->meta = MOD_OFF; changed = TRUE; }

    if (changed)
        app_refresh_labels(app);
}

/* 单次大写：用完一个字符就回到小写 */
static void consume_oneshot(App *app) {
    if (app->shift_state != SHIFT_ONESHOT)
        return;
    app->shift_state = SHIFT_OFF;
}

/* 字母键主标签随 shift/caps 切换大小写。
 * 右上角两档上滑角标由 keybutton 维护，这里只更新高亮。 */
static void update_char_button(App *app, KeyButton *kb) {
    const KeyDef *def = key_button_def(kb);
    if (!def || def->klass != KEY_CLASS_CHAR) return;
    gboolean upper = want_upper(app);

    if (def->is_letter && def->label_shift) {
        key_button_set_label(kb, upper ? def->label_shift : def->label);
        key_button_set_active_class(kb, "key-upper", upper);
    }

    /* 上滑是否走 Shift 档：字母键跟随 shift/caps，其他跟随 Shift */
    gboolean upper_swipe = def->is_letter ? upper : shift_active(app);
    key_button_set_up_labels(kb, def->label_up, def->label_up_shift,
                             upper_swipe);
}

/* ===== 公共接口 ===== */

void app_apply_config(App *app) {
    if (!app || !app->window) return;
    gtk_widget_set_opacity(app->window, app->config.opacity);
}

/* 触控板窗口的透明度。它是独立窗口，所以和主窗口分开设置 ——
 * 键盘通常要看得清，触控板往往希望更透以免挡视线。 */
void app_apply_pad_config(App *app) {
    if (!app || !app->mouse_panel) return;
    gtk_widget_set_opacity(app->mouse_panel, app->config.pad_opacity);
}

/* 强制窗口走客户端装饰（CSD）。
 *
 * ===== 为什么必须这么做 =====
 *
 * KWin 对 GTK 应用会优先强制服务端装饰（SSD），并拒绝应用发出的
 * 「不要装饰」请求。这是 KDE 的已知行为（Bug 484800）。
 *
 * 所以 gtk_window_set_decorated(FALSE) 在 Wayland + KWin 下形同虚设：
 * 它只是通过 xdg-decoration 协议「请求」，而 KWin 选择无视。
 *
 * 绕过的关键：调用 gtk_window_set_titlebar()。
 * 一旦窗口有了 titlebar 控件，GTK 就会走 CSD 路径，
 * 合成器随即放手让应用自己画 —— 此时再 set_decorated(FALSE)，
 * GTK 就不画那层标题栏了，得到真正无边框的窗口。
 *
 * 用一个空的 GtkGrid 而不是 NULL：
 * 传 NULL 是「移除 titlebar」的语义，会让 GTK 回到 SSD 模式，
 * 正好是我们要避免的。空 Grid 既占位又不显示任何东西。 */
/* ===== 自绘窗口白边 =====
 *
 * 为什么不用 CSS border：
 *   试过把 border 画在 .decoration 上，但 GtkMenu 弹出菜单同样带
 *   decoration 节点，那条全局规则会把菜单背景一起抹掉，菜单变透明。
 *   而画在 window 上又会被 decoration 节点盖住，看不见。
 *
 * 为什么画在子控件上而不是窗口本身：
 *   CSD 模式下 GtkWindow 的内容由内部的 decoration 节点绘制，
 *   窗口自身的 draw 信号不一定会被调用。而窗口的直属子控件
 *   （我们那个外层 GtkBox）是必定会被绘制的，在那里画最可靠。
 *
 * 坐标系是子控件自身，它铺满窗口内容区，所以效果等同于窗口外沿。
 * 线宽 1px 时要偏移 0.5 才落在像素中心，否则会糊成 2px 的灰边。 */

/* 圆角半径。比按键自身的圆角（8px）略小，视觉上更收敛。 */
#define BORDER_RADIUS 10.0

/* 画一个圆角矩形路径。
 *
 * cairo 没有内置圆角矩形，要自己用 arc 拼四段。
 * 半径会被夹到宽高的一半，避免小窗口上圆弧交叠画出怪异形状。 */
static void rounded_rect(cairo_t *cr, gdouble x, gdouble y,
                          gdouble w, gdouble h, gdouble r) {
    gdouble max_r = MIN(w, h) / 2.0;
    if (r > max_r) r = max_r;
    if (r < 0) r = 0;

    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r,     r, -G_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0,          G_PI / 2);
    cairo_arc(cr, x + r,     y + h - r, r, G_PI / 2,   G_PI);
    cairo_arc(cr, x + r,     y + r,     r, G_PI,       3 * G_PI / 2);
    cairo_close_path(cr);
}

static gboolean on_border_draw(GtkWidget *w, cairo_t *cr, gpointer data) {
    (void)data;

    GtkAllocation a;
    gtk_widget_get_allocation(w, &a);
    if (a.width <= 0 || a.height <= 0) return FALSE;

    cairo_save(cr);

    /* 描边：半透明白，圆角。0.5 偏移让 1px 线落在像素中心。 */
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.55);
    cairo_set_line_width(cr, 1.0);
    rounded_rect(cr, 0.5, 0.5, a.width - 1.0, a.height - 1.0, BORDER_RADIUS);
    cairo_stroke(cr);

    cairo_restore(cr);

    /* 返回 FALSE：让 GTK 继续自己的绘制流程，不接管 */
    return FALSE;
}

void app_attach_border(GtkWidget *win) {
    if (!win) return;

    /* 优先挂到窗口的直属子控件上（内容区）。
     * 子控件必定被绘制，而窗口自身在 CSD 模式下不一定。
     * 窗口还没加子控件时退回挂窗口本身。 */
    GtkWidget *target = gtk_bin_get_child(GTK_BIN(win));
    if (!target) target = win;

    gtk_widget_set_app_paintable(target, TRUE);

    /* 避免重复挂同一个控件 */
    if (g_object_get_data(G_OBJECT(target), "osk-border-attached"))
        return;
    g_object_set_data(G_OBJECT(target), "osk-border-attached",
                      GINT_TO_POINTER(1));

    g_signal_connect(target, "draw", G_CALLBACK(on_border_draw), NULL);
}

void app_force_csd(GtkWindow *win) {
    if (!win) return;

    /* 只在还没有 titlebar 时创建 —— 这个函数会被反复调用
     * （map 后、每次切布局），每次都 new 一个 Grid 会泄漏。
     *
     * gtk_window_get_titlebar 返回 NULL 表示当前是 SSD 模式，
     * 需要设一个空 titlebar 把 GTK 拉回 CSD。 */
    if (!gtk_window_get_titlebar(win)) {
        GtkWidget *tb = gtk_grid_new();

        /* 压掉标题栏的高度：GtkWindow 会给 titlebar 套一层
         * 容器并按常规控件测量，空 Grid 仍可能占掉几像素到
         * 一整行的高度，导致内容整体下移。
         * 尺寸请求设为 0，让它在布局里不占空间。 */
        gtk_widget_set_size_request(tb, 0, 0);

        gtk_window_set_titlebar(win, tb);
    }

    gtk_window_set_decorated(win, FALSE);
}

/* ===== 子窗口统一框架 =====
 *
 * 主窗口有一整套自绘顶栏（拖动区 + 按钮）。子窗口如果只是
 * 「一块内容 + 一个白边」，用户既没法拖动也没法关闭 ——
 * 去掉系统装饰之后这两个能力就彻底没有了。
 *
 * 所以这里提供一个统一的外壳：
 *
 *     ┌──────── 白边（画在窗口子控件的外沿）────────┐
 *     │ ┌──── 标题栏：标题 + 拖动区 + 关闭按钮 ────┐ │
 *     │ ├────────────────────────────────────────┤ │
 *     │ │            内容区（调用方填）            │ │
 *     │ └────────────────────────────────────────┘ │
 *     └────────────────────────────────────────────┘
 *
 * 白边必须画在「最外层子控件」上，不能画在内容区上 ——
 * 否则边框会陷进内容里，看起来像一圈内框（这正是之前的 bug）。
 */

/* 拖动标题栏移动窗口 */
static gboolean on_subwin_drag(GtkWidget *w, GdkEventButton *ev,
                                gpointer data) {
    (void)w;
    GtkWindow *win = data;
    if (ev->button != 1) return FALSE;

    gtk_window_begin_move_drag(win, 1,
                                (gint)ev->x_root, (gint)ev->y_root,
                                ev->time);
    return TRUE;
}

static void on_subwin_close(GtkButton *btn, gpointer data) {
    (void)btn;
    gtk_widget_destroy(GTK_WIDGET(data));
}

GtkWidget *app_subwindow_shell(GtkWidget *win, const char *title) {
    if (!win) return NULL;

    /* 外壳：白边就画在这个 box 上 */
    GtkWidget *shell = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(shell),
                                 "osk-subwin-shell");
    gtk_container_add(GTK_CONTAINER(win), shell);

    /* --- 标题栏 --- */
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(bar),
                                 "osk-subwin-bar");

    /* 标题（放在一个可拖动的 EventBox 里） */
    GtkWidget *drag = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(drag), FALSE);
    gtk_widget_set_hexpand(drag, TRUE);
    gtk_widget_add_events(drag, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(drag, "button-press-event",
                     G_CALLBACK(on_subwin_drag), win);

    GtkWidget *lbl = gtk_label_new(title ? title : "");
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(lbl),
                                 "osk-subwin-title");
    gtk_container_add(GTK_CONTAINER(drag), lbl);
    gtk_box_pack_start(GTK_BOX(bar), drag, TRUE, TRUE, 0);

    /* 关闭按钮 */
    GtkWidget *close_btn = gtk_button_new_with_label("✕");
    gtk_widget_set_can_focus(close_btn, FALSE);
    gtk_widget_set_focus_on_click(close_btn, FALSE);
    gtk_widget_set_tooltip_text(close_btn, "关闭");
    gtk_style_context_add_class(gtk_widget_get_style_context(close_btn),
                                 "osk-tool");
    gtk_style_context_add_class(gtk_widget_get_style_context(close_btn),
                                 "osk-subwin-close");
    g_signal_connect(close_btn, "clicked",
                     G_CALLBACK(on_subwin_close), win);
    gtk_box_pack_start(GTK_BOX(bar), close_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(shell), bar, FALSE, FALSE, 0);

    /* --- 内容区 ---
     * 返回这个容器，调用方往里塞自己的东西。 */
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(content),
                                 "osk-subwin-content");
    gtk_box_pack_start(GTK_BOX(shell), content, TRUE, TRUE, 0);

    return content;
}

void app_setup_subwindow(GtkWidget *win) {
    if (!win) return;

    /* KWin 会强制 SSD，必须先用空 titlebar 逼 GTK 走 CSD，
     * 否则 set_decorated(FALSE) 不起作用。 */
    app_force_csd(GTK_WINDOW(win));

    gtk_style_context_add_class(gtk_widget_get_style_context(win),
                                 "osk-subwindow");
}

/* 白边挂在窗口最外层子控件（shell）的外沿 —— 必须是
 * app_subwindow_shell 建好的那个 box，这样边框在内容之外，
 * 不会像之前那样陷进内容里变成一圈内框。 */
void app_finish_subwindow(GtkWidget *win) {
    app_attach_border(win);
}

void app_on_key_activate(const KeyDef *def, gboolean is_repeat,
                          gpointer user_data) {
    App *app = user_data;
    if (!app || !def) return;

    if (def->action == KEY_ACTION_MODIFIER) {
        if (!is_repeat)
            on_modifier_clicked(app, def);
        return;
    }

    /* 控件键（如换页按钮）不发按键事件，由创建方自己的回调处理 */
    if (def->action == KEY_ACTION_COMMAND)
        return;

    unsigned short mods[4];
    gsize n = collect_modifiers(app, def, mods);

    osk_keyboard_chord(app->kbd, def->code, mods, n);

    if (is_repeat)
        g_print("[OSK] 重复 %s\n", def->id);
    else
        g_print("[OSK] 发送 %s（%zu 个修饰键）\n", def->id, n);

    /* 输入一个字符/功能键后，单次修饰键自动关闭。
     *
     * 注意只对真正的「输入类」按键生效：修饰键自己和控件键
     * 在上面已经 return 了，不会走到这里。 */
    if (def->klass == KEY_CLASS_CHAR || def->klass == KEY_CLASS_FUNCTION) {
        if (app->shift_state == SHIFT_ONESHOT) {
            consume_oneshot(app);
            app_refresh_labels(app);
        }
        /* Ctrl / Alt / Win 的单次状态 */
        consume_plain_modifiers(app);
    }
}

void app_register_key_button(App *app, KeyButton *kb) {
    if (!app || !kb) return;

    g_ptr_array_add(app->key_buttons, kb);

    const KeyDef *def = key_button_def(kb);

    switch (def->modifier_bit) {
    case OSK_MOD_SHIFT:
        if (app->shift_btn_count < 2)
            app->shift_btns[app->shift_btn_count++] = kb;
        break;
    case OSK_MOD_CTRL: app->ctrl_btn = kb; break;
    case OSK_MOD_ALT:  app->alt_btn  = kb; break;
    case OSK_MOD_META: app->meta_btn = kb; break;
    case OSK_MOD_CAPS: app->caps_btn = kb; break;
    }
}

void app_refresh_labels(App *app) {
    if (!app) return;

    /* 字符键：主标签大小写 + 上滑角标高亮 */
    if (app->key_buttons) {
        for (gsize i = 0; i < app->key_buttons->len; ++i)
            update_char_button(app, g_ptr_array_index(app->key_buttons, i));
    }

    for (gsize i = 0; i < app->shift_btn_count; ++i)
        update_shift_button(app->shift_btns[i], app->shift_state);

    update_plain_modifier(app->caps_btn, "Caps", app->caps);
    update_plain_modifier(app->ctrl_btn, "Ctrl", app->ctrl == MOD_ONESHOT);
    update_plain_modifier(app->alt_btn,  "Alt",  app->alt  == MOD_ONESHOT);
    update_plain_modifier(app->meta_btn, "Meta", app->meta == MOD_ONESHOT);
}

void app_on_key_swipe(const KeyDef *def, gpointer user_data) {
    App *app = user_data;
    if (!app || !def || !def->up_code) return;

    /* 上滑输出的是明确目标字符，按当前 Shift 档位二选一：
     *   - 普通档：label_up / up_code / up_shift
     *   - Shift 档：label_up_shift / up_code_shift / up_shift_shift
     * 哪一档生效和按钮右上角的高亮一致。 */
    gboolean upper = (def->klass == KEY_CLASS_CHAR && def->is_letter)
                     ? want_upper(app)
                     : shift_active(app);

    unsigned short code    = def->up_code;
    gboolean       need_sh = def->up_shift;
    const char    *label   = def->label_up;

    if (upper && def->up_code_shift) {
        code    = def->up_code_shift;
        need_sh = def->up_shift_shift;
        label   = def->label_up_shift;
    }

    unsigned short mods[4];
    gsize n = 0;
    if (need_sh)                  mods[n++] = KEY_LEFTSHIFT;
    if (app->ctrl == MOD_ONESHOT) mods[n++] = KEY_LEFTCTRL;
    if (app->alt  == MOD_ONESHOT) mods[n++] = KEY_LEFTALT;
    if (app->meta == MOD_ONESHOT) mods[n++] = KEY_LEFTMETA;

    osk_keyboard_chord(app->kbd, code, mods, n);
    g_print("[OSK] 上滑 %s → '%s'（%zu 个修饰键）\n",
            def->id, label ? label : "?", n);

    /* 上滑同样消耗单次修饰键 */
    if (app->shift_state == SHIFT_ONESHOT) {
        consume_oneshot(app);
        app_refresh_labels(app);
    }
    consume_plain_modifiers(app);
}
