#include "first_run.h"

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <pwd.h>
#include <string.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>

#define SETUP_GROUP "first_run"

/* 仅这些固定标题对应输入面板。设置/编辑对话框必须继续接受焦点。 */
static const struct {
    const char *group;
    const char *title;
} panel_rules[] = {
    { "osk-keyboard", "OSK 屏幕键盘" },
    { "osk-touchpad", "触控板" },
};

/* 提权执行的内容固定在二进制里，不执行用户目录中的脚本。
 * 用户名通过单独的 argv 传入；root 侧再按 UID 校验一次。
 * udev 规则只修改 /dev/uinput 节点；加入 input 组也会授予该组已有
 * 的输入设备访问权限，这一点在首次运行提示中明确说明。 */
static const char permission_script[] =
    "set -eu\n"
    "PATH=/usr/sbin:/usr/bin:/sbin:/bin\n"
    "export PATH\n"
    "target_user=$1\n"
    "target_uid=$2\n"
    "[ \"$(id -u -- \"$target_user\")\" = \"$target_uid\" ]\n"
    "[ \"$target_uid\" -ne 0 ]\n"
    "if ! getent group input >/dev/null; then groupadd --system input; fi\n"
    "usermod -a -G input -- \"$target_user\"\n"
    "install -d -m 0755 /etc/udev/rules.d\n"
    "rule=$(mktemp /etc/udev/rules.d/.osk-uinput.XXXXXX)\n"
    "trap 'rm -f -- \"$rule\"' EXIT HUP INT TERM\n"
    "printf '%s\\n' '# OSK virtual input device' "
    "'SUBSYSTEM==\"misc\", KERNEL==\"uinput\", GROUP=\"input\", MODE=\"0660\", TAG+=\"uaccess\"' > \"$rule\"\n"
    "chmod 0644 \"$rule\"\n"
    "mv -f -- \"$rule\" /etc/udev/rules.d/70-osk-uinput.rules\n"
    "trap - EXIT HUP INT TERM\n"
    "install -d -m 0755 /etc/modules-load.d\n"
    "module=$(mktemp /etc/modules-load.d/.osk-uinput.XXXXXX)\n"
    "trap 'rm -f -- \"$module\"' EXIT HUP INT TERM\n"
    "printf '%s\\n' uinput > \"$module\"\n"
    "chmod 0644 \"$module\"\n"
    "mv -f -- \"$module\" /etc/modules-load.d/osk-uinput.conf\n"
    "trap - EXIT HUP INT TERM\n"
    "modprobe uinput\n"
    "udevadm control --reload-rules\n"
    "udevadm trigger --action=add --subsystem-match=misc --sysname-match=uinput\n"
    "udevadm settle --timeout=5\n";

static void show_message(GtkMessageType type, const char *primary,
                         const char *detail) {
    GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
                                               type, GTK_BUTTONS_OK,
                                               "%s", primary);
    gtk_window_set_title(GTK_WINDOW(dialog), "OSK 首次运行配置");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                              "%s", detail);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

/* KConfig StringList 使用逗号；不能用 GKeyFile 默认的分号列表。 */
static gboolean append_rule(GString *list, const char *group) {
    char **parts = g_strsplit(list->str, ",", -1);
    gboolean found = FALSE;
    for (gsize i = 0; parts[i]; ++i) {
        if (g_strcmp0(g_strstrip(parts[i]), group) == 0) {
            found = TRUE;
            break;
        }
    }
    g_strfreev(parts);
    if (found) return FALSE;
    if (list->len && list->str[list->len - 1] != ',')
        g_string_append_c(list, ',');
    g_string_append(list, group);
    return TRUE;
}

static gint compare_rule_numbers(gconstpointer a, gconstpointer b) {
    gint left = *(const gint *)a;
    gint right = *(const gint *)b;
    return (left > right) - (left < right);
}

static gboolean reconfigure_kwin(void) {
    GError *error = NULL;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    GVariant *reply = NULL;
    if (bus) {
        reply = g_dbus_connection_call_sync(bus, "org.kde.KWin", "/KWin",
                                            "org.kde.KWin", "reconfigure",
                                            NULL, NULL, G_DBUS_CALL_FLAGS_NONE,
                                            3000, NULL, &error);
        g_object_unref(bus);
    }
    if (!reply) {
        g_printerr("[OSK] KWin 规则已保存，但暂未重载：%s\n",
                   error ? error->message : "会话总线不可用");
        g_clear_error(&error);
        return FALSE;
    }
    g_variant_unref(reply);
    return TRUE;
}

/* 只修改内存中的规则；独立于 D-Bus/提权，便于验证旧规则不丢失。 */
static void update_kwin_rule_config(GKeyFile *kf) {
    /* 新 KWin 使用 Order，旧版本使用 rules，更老的格式只有 count
     * 和 [1]...[N]。合并所有有效索引，防止 KWin 迁移时丢掉已有规则。 */
    gboolean has_order = g_key_file_has_key(kf, "General", "Order", NULL);
    char *order = g_key_file_get_string(kf, "General", "Order", NULL);
    char *legacy = g_key_file_get_string(kf, "General", "rules", NULL);
    GString *list = g_string_new(order ? order : "");
    if (legacy && *legacy) {
        char **old_parts = g_strsplit(legacy, ",", -1);
        for (gsize i = 0; old_parts[i]; ++i) {
            const char *group = g_strstrip(old_parts[i]);
            if (*group) append_rule(list, group);
        }
        g_strfreev(old_parts);
    }
    if (!list->len) {
        gint count = g_key_file_get_integer(kf, "General", "count", NULL);
        /* 逐个检查实际存在的数字组，避免异常 count 导致超长循环。 */
        gsize n_groups = 0;
        char **groups = g_key_file_get_groups(kf, &n_groups);
        GArray *numbers = g_array_new(FALSE, FALSE, sizeof(gint));
        for (gsize i = 0; i < n_groups; ++i) {
            char *end = NULL;
            gint64 number = g_ascii_strtoll(groups[i], &end, 10);
            if (end && !*end && number > 0 && number <= count) {
                gint value = (gint)number;
                g_array_append_val(numbers, value);
            }
        }
        g_array_sort(numbers, compare_rule_numbers);
        for (guint i = 0; i < numbers->len; ++i) {
            char *group = g_strdup_printf("%d", g_array_index(numbers, gint, i));
            append_rule(list, group);
            g_free(group);
        }
        g_array_free(numbers, TRUE);
        g_strfreev(groups);
    }
    /* 新版 KWin 也会读取索引之外的实际规则组；一并保留，避免
     * 其旧格式迁移清理阶段误删用户规则。 */
    gsize n_groups = 0;
    char **groups = g_key_file_get_groups(kf, &n_groups);
    for (gsize i = 0; i < n_groups; ++i) {
        if (strcmp(groups[i], "General") != 0 && groups[i][0] != '$')
            append_rule(list, groups[i]);
    }
    g_strfreev(groups);
    g_free(order);
    g_free(legacy);

    /* KWin 按列表顺序选首个生效规则。精确匹配的 OSK 规则放前面，
     * 避免被已有的通配规则抢先覆盖；其他规则的相对顺序不变。 */
    GString *priority = g_string_new("");
    for (gsize i = 0; i < G_N_ELEMENTS(panel_rules); ++i)
        append_rule(priority, panel_rules[i].group);
    char **existing = g_strsplit(list->str, ",", -1);
    for (gsize i = 0; existing[i]; ++i) {
        const char *group = g_strstrip(existing[i]);
        if (*group) append_rule(priority, group);
    }
    g_strfreev(existing);
    g_string_free(list, TRUE);
    list = priority;

    for (gsize i = 0; i < G_N_ELEMENTS(panel_rules); ++i) {
        const char *group = panel_rules[i].group;
        append_rule(list, group);
        /* 固定标识由 main() 在 gtk_init 前设置，避免匹配到其他程序。 */
        g_key_file_set_string(kf, group, "Description", panel_rules[i].title);
        g_key_file_set_string(kf, group, "wmclass", "osk");
        g_key_file_set_integer(kf, group, "wmclassmatch", 1); /* ExactMatch */
        g_key_file_set_boolean(kf, group, "wmclasscomplete", FALSE);
        g_key_file_set_string(kf, group, "title", panel_rules[i].title);
        g_key_file_set_integer(kf, group, "titlematch", 1);
        g_key_file_set_boolean(kf, group, "above", TRUE);
        g_key_file_set_integer(kf, group, "aboverule", 2); /* Force */
        g_key_file_set_boolean(kf, group, "acceptfocus", FALSE);
        g_key_file_set_integer(kf, group, "acceptfocusrule", 2);
    }
    /* 两个版本的索引如同时存在必须一致，否则新版迁移会覆盖 Order。 */
    g_key_file_set_string(kf, "General", "rules", list->str);
    if (has_order)
        g_key_file_set_string(kf, "General", "Order", list->str);
    char **parts = g_strsplit(list->str, ",", -1);
    gint count = 0;
    for (gsize i = 0; parts[i]; ++i)
        if (*parts[i]) ++count;
    g_key_file_set_integer(kf, "General", "count", count);
    g_strfreev(parts);
    g_string_free(list, TRUE);
}

static gboolean install_kwin_rules(void) {
    char *path = g_build_filename(g_get_user_config_dir(), "kwinrulesrc", NULL);
    GKeyFile *kf = g_key_file_new();
    GError *error = NULL;
    gboolean ok = FALSE;

    if (!g_key_file_load_from_file(kf, path,
                                   G_KEY_FILE_KEEP_COMMENTS |
                                   G_KEY_FILE_KEEP_TRANSLATIONS, &error)) {
        if (!g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            g_printerr("[OSK] 保留现有 KWin 规则，无法解析 %s：%s\n",
                       path, error->message);
            goto out;
        }
        g_clear_error(&error);
    }
    /* KConfig 的转义列表/特殊组名不能按普通 GKeyFile 逗号表改写。
     * 遇到这种少见配置保留原文件，避免破坏已有规则。 */
    gboolean simple = TRUE;
    const char *indexes[] = { "rules", "Order" };
    for (gsize i = 0; i < G_N_ELEMENTS(indexes); ++i) {
        char *value = g_key_file_get_string(kf, "General", indexes[i], NULL);
        if (value && strchr(value, '\\')) simple = FALSE;
        g_free(value);
    }
    char **groups = g_key_file_get_groups(kf, NULL);
    for (gsize i = 0; groups[i]; ++i)
        if (strpbrk(groups[i], ",\\")) simple = FALSE;
    g_strfreev(groups);
    if (!simple) {
        g_printerr("[OSK] KWin 规则含特殊转义，保留原文件，请手工添加 OSK 规则。\n");
        goto out;
    }
    update_kwin_rule_config(kf);

    /* 备份原文件，合并后原子保存。已有备份不覆盖。 */
    char *backup = g_strconcat(path, ".before-osk", NULL);
    if (g_file_test(path, G_FILE_TEST_EXISTS) &&
        !g_file_test(backup, G_FILE_TEST_EXISTS)) {
        GFile *source = g_file_new_for_path(path);
        GFile *dest = g_file_new_for_path(backup);
        ok = g_file_copy(source, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, &error);
        g_object_unref(source);
        g_object_unref(dest);
        if (!ok) {
            g_printerr("[OSK] 无法备份 KWin 规则，未修改原文件：%s\n", error->message);
            g_free(backup);
            goto out;
        }
    }
    g_free(backup);
    ok = g_key_file_save_to_file(kf, path, &error);
    if (ok) {
        g_print("[OSK] 已合并首次运行 KWin 规则：%s\n", path);
        reconfigure_kwin();
    } else {
        g_printerr("[OSK] 保存 KWin 规则失败：%s\n", error->message);
    }
out:
    g_clear_error(&error);
    g_key_file_free(kf);
    g_free(path);
    return ok;
}

static gboolean install_input_permissions(void) {
    struct passwd *user = getpwuid(getuid());
    if (!user || getuid() == 0) {
        g_printerr("[OSK] 请使用普通桌面用户启动 OSK，不能确定需加入 input 组的用户。\n");
        return FALSE;
    }
    char *username = g_strdup(user->pw_name);
    char *uid = g_strdup_printf("%lu", (unsigned long)getuid());
    char *pkexec = g_find_program_in_path("pkexec");
    if (!pkexec) {
        g_printerr("[OSK] 未找到 pkexec，未安装 udev 规则和 input 组成员关系。\n");
        g_free(username);
        g_free(uid);
        return FALSE;
    }

    /* argv 无 shell 插值；仅系统规则安装提权，GTK 主程序仍是普通用户。 */
    char *argv[] = { pkexec, "/bin/sh", "-c", (char *)permission_script,
                     "osk-first-run", username, uid, NULL };
    GError *error = NULL;
    char *output = NULL;
    char *errors = NULL;
    gint status = 0;
    gboolean ok = g_spawn_sync(NULL, argv, NULL, 0, NULL, NULL,
                                &output, &errors, &status, &error);
    if (ok && (!WIFEXITED(status) || WEXITSTATUS(status) != 0)) {
        g_set_error_literal(&error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED,
                            "系统授权已取消或安装命令未成功完成");
        ok = FALSE;
    }
    if (!ok) {
        g_printerr("[OSK] 输入权限配置未完成：%s%s%s\n",
                   error ? error->message : "未知错误",
                   errors && *errors ? "\n" : "", errors ? errors : "");
    }
    g_clear_error(&error);
    g_free(output);
    g_free(errors);
    g_free(pkexec);
    g_free(username);
    g_free(uid);
    return ok;
}

gboolean osk_first_run_setup(void) {
    gboolean ready = TRUE;
    char *dir = g_build_filename(g_get_user_config_dir(), "osk", NULL);
    if (g_mkdir_with_parents(dir, 0700) < 0) {
        g_printerr("[OSK] 无法创建首次运行配置目录：%s\n", g_strerror(errno));
        g_free(dir);
        return TRUE; /* 不改变已有用户正常启动行为 */
    }
    char *path = g_build_filename(dir, "setup.ini", NULL);
    char *lock_path = g_build_filename(dir, "setup.lock", NULL);
    int lock_fd = g_open(lock_path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) < 0) {
        g_printerr("[OSK] 首次运行配置被占用或无法锁定，请稍后再试。\n");
        if (lock_fd >= 0) close(lock_fd);
        g_free(lock_path);
        g_free(path);
        g_free(dir);
        return FALSE;
    }

    GKeyFile *state = g_key_file_new();
    GError *error = NULL;
    if (g_key_file_load_from_file(state, path, G_KEY_FILE_NONE, &error)) {
        if (g_key_file_get_boolean(state, SETUP_GROUP, "attempted", NULL))
            goto out;
    } else if (!g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
        g_printerr("[OSK] 首次运行记录无法读取，未重复执行系统配置：%s\n", error->message);
        goto out;
    }
    g_clear_error(&error);

    /* 在提权前先记下已尝试：取消、失败、意外退出后都不反复弹授权。
     * 不以外观配置是否存在作为首次判断，升级已有版本也只执行一次。 */
    g_key_file_set_boolean(state, SETUP_GROUP, "attempted", TRUE);
    if (!g_key_file_save_to_file(state, path, &error)) {
        g_printerr("[OSK] 无法记录首次运行状态，未执行系统配置：%s\n", error->message);
        goto out;
    }
    gboolean kwin_ok = install_kwin_rules();

    show_message(GTK_MESSAGE_INFO, "首次运行：配置 OSK 的输入权限",
                 "接下来系统会请求管理员授权：安装仅针对 /dev/uinput 的 udev 规则，"
                 "配置开机加载 uinput，并将当前用户加入 input 组。"
                 "input 组成员可读取系统输入设备。\n\n"
                 "本操作仅自动尝试一次；取消或失败后不会在后续启动中反复弹窗。"
                 "KWin 置顶及拒绝焦点规则只匹配主键盘和触控板窗口。");
    gboolean permissions_ok = install_input_permissions();
    g_key_file_set_boolean(state, SETUP_GROUP, "kwin_rules_written", kwin_ok);
    g_key_file_set_boolean(state, SETUP_GROUP, "input_permissions_installed", permissions_ok);
    if (!g_key_file_save_to_file(state, path, &error)) {
        g_printerr("[OSK] 无法保存首次运行结果：%s\n", error->message);
        g_clear_error(&error);
    }

    /* uaccess 可给当前活动会话即时授权；input 组要等重新登录才更新。
     * 不通过 sudo 重新启动整个 GUI，也不将设备设成所有人可写。 */
    int input_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    gboolean input_ready = input_fd >= 0;
    if (input_ready) close(input_fd);
    if (!kwin_ok || !permissions_ok || !input_ready) {
        const char *detail;
        if (permissions_ok && !input_ready) {
            detail = "udev 规则和 input 组成员关系已安装。当前会话尚未获得 /dev/uinput 权限，"
                     "请注销并重新登录后再启动 OSK。\n\n首次配置不会自动重复。";
            ready = FALSE;
        } else {
            detail = "部分首次配置未完成，具体原因已打印到终端。若 /dev/uinput 已有权限，"
                     "OSK 会照常启动。\n\n后续启动不会重复执行；需要重试时删除"
                     " $XDG_CONFIG_HOME/osk/setup.ini（通常为 ~/.config/osk/setup.ini）后再启动。";
        }
        show_message(GTK_MESSAGE_WARNING, "首次运行配置结果", detail);
    } else {
        g_print("[OSK] 首次运行配置完成；input 组成员关系在下次登录后全面生效。\n");
    }

out:
    g_clear_error(&error);
    g_key_file_free(state);
    close(lock_fd);
    g_free(lock_path);
    g_free(path);
    g_free(dir);
    return ready;
}
