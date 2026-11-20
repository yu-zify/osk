/* 直接包含实现以测试内部的纯配置合并，不调用 GTK 初始化/提权/设备访问。 */
#include "../src/first_run.c"

static void assert_panel_rules(GKeyFile *kf) {
    for (gsize i = 0; i < G_N_ELEMENTS(panel_rules); ++i) {
        const char *group = panel_rules[i].group;
        g_assert_true(g_key_file_get_boolean(kf, group, "above", NULL));
        g_assert_false(g_key_file_get_boolean(kf, group, "acceptfocus", NULL));
        g_assert_cmpint(g_key_file_get_integer(kf, group, "aboverule", NULL), ==, 2);
        g_assert_cmpint(g_key_file_get_integer(kf, group, "acceptfocusrule", NULL), ==, 2);
        g_assert_cmpint(g_key_file_get_integer(kf, group, "wmclassmatch", NULL), ==, 1);
        g_assert_cmpint(g_key_file_get_integer(kf, group, "titlematch", NULL), ==, 1);
        char *title = g_key_file_get_string(kf, group, "title", NULL);
        g_assert_cmpstr(title, ==, panel_rules[i].title);
        g_free(title);
    }
}

static void check_merge(const char *input, const char *expected, gint count) {
    GKeyFile *kf = g_key_file_new();
    GError *error = NULL;
    g_assert_true(g_key_file_load_from_data(kf, input, -1,
                                            G_KEY_FILE_KEEP_COMMENTS, &error));
    g_assert_no_error(error);
    update_kwin_rule_config(kf);
    char *order = g_key_file_get_string(kf, "General", "rules", NULL);
    g_assert_cmpstr(order, ==, expected);
    g_assert_cmpint(g_key_file_get_integer(kf, "General", "count", NULL), ==, count);
    if (g_key_file_has_key(kf, "General", "Order", NULL)) {
        char *modern = g_key_file_get_string(kf, "General", "Order", NULL);
        g_assert_cmpstr(modern, ==, expected);
        g_free(modern);
    }
    /* 旧规则数据不改动；重复合并后没有重复项或顺序漂移。 */
    if (g_key_file_has_group(kf, "user-rule")) {
        char *value = g_key_file_get_string(kf, "user-rule", "title", NULL);
        g_assert_cmpstr(value, ==, "User's app");
        g_free(value);
    }
    assert_panel_rules(kf);
    char *once = g_key_file_to_data(kf, NULL, NULL);
    update_kwin_rule_config(kf);
    char *twice = g_key_file_to_data(kf, NULL, NULL);
    g_assert_cmpstr(once, ==, twice);
    g_free(once);
    g_free(twice);
    g_free(order);
    g_key_file_free(kf);
}

static void test_empty(void) {
    check_merge("[General]\n", "osk-keyboard,osk-touchpad", 2);
}

static void test_legacy_numbers(void) {
    check_merge("[General]\ncount=2\n[2]\nabove=true\n[1]\nabove=false\n",
                "osk-keyboard,osk-touchpad,1,2", 4);
}

static void test_legacy_list(void) {
    check_merge("[General]\nrules=user-rule,second\ncount=2\n"
                "[user-rule]\ntitle=User's app\n[second]\nabove=false\n",
                "osk-keyboard,osk-touchpad,user-rule,second", 4);
}

static void test_modern_order(void) {
    check_merge("[General]\nOrder=user-rule\n[user-rule]\ntitle=User's app\n",
                "osk-keyboard,osk-touchpad,user-rule", 3);
}

static void test_both_indexes(void) {
    check_merge("[General]\nOrder=second,user-rule\nrules=user-rule,third\n"
                "[user-rule]\ntitle=User's app\n[second]\nabove=false\n"
                "[third]\nabove=true\n",
                "osk-keyboard,osk-touchpad,second,user-rule,third", 5);
}

static void test_existing_panels(void) {
    check_merge("[General]\nrules=user-rule,osk-touchpad,osk-keyboard\n"
                "[user-rule]\ntitle=User's app\n",
                "osk-keyboard,osk-touchpad,user-rule", 3);
}

static void test_missing_index(void) {
    check_merge("[user-rule]\ntitle=User's app\n[second]\nabove=false\n",
                "osk-keyboard,osk-touchpad,user-rule,second", 4);
}

static void test_unindexed_group(void) {
    check_merge("[General]\nOrder=user-rule\n[user-rule]\ntitle=User's app\n"
                "[unindexed]\nabove=false\n",
                "osk-keyboard,osk-touchpad,user-rule,unindexed", 4);
}

static void test_permission_script_syntax(void) {
    /* -n 只做语法解析，绝不执行安装命令。 */
    char *argv[] = { "/bin/sh", "-n", "-c", (char *)permission_script, NULL };
    GError *error = NULL;
    gint status = 0;
    g_assert_true(g_spawn_sync(NULL, argv, NULL, 0,
                                NULL, NULL, NULL, NULL, &status, &error));
    g_assert_no_error(error);
    g_assert_true(WIFEXITED(status));
    g_assert_cmpint(WEXITSTATUS(status), ==, 0);
}

int main(int argc, char **argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/first-run/empty", test_empty);
    g_test_add_func("/first-run/legacy-numbers", test_legacy_numbers);
    g_test_add_func("/first-run/legacy-list", test_legacy_list);
    g_test_add_func("/first-run/modern-order", test_modern_order);
    g_test_add_func("/first-run/both-indexes", test_both_indexes);
    g_test_add_func("/first-run/existing-panels", test_existing_panels);
    g_test_add_func("/first-run/missing-index", test_missing_index);
    g_test_add_func("/first-run/unindexed-group", test_unindexed_group);
    g_test_add_func("/first-run/permission-script-syntax", test_permission_script_syntax);
    return g_test_run();
}
