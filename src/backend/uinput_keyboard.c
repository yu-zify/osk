#include "uinput_keyboard.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/uinput.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define OSK_KEYBOARD_ERROR (osk_keyboard_error_quark())

struct OskKeyboard {
    int fd;
    gboolean created;
};

static GQuark osk_keyboard_error_quark(void) {
    return g_quark_from_static_string("osk-keyboard-error");
}

static gboolean write_event(int fd, unsigned short type,
                            unsigned short code, int value) {
    struct input_event ev = {0};
    ev.type  = type;
    ev.code  = code;
    ev.value = value;
    ssize_t n;
    do {
        n = write(fd, &ev, sizeof(ev));
    } while (n < 0 && errno == EINTR);
    return n == (ssize_t)sizeof(ev);
}

OskKeyboard *osk_keyboard_new(const char *device, GError **error) {
    OskKeyboard *kbd = g_new0(OskKeyboard, 1);
    kbd->fd = -1;

    /* 关键：不用 O_NONBLOCK，从根上避免 EAGAIN 把设备搞死 */
    kbd->fd = open(device, O_WRONLY | O_CLOEXEC);
    if (kbd->fd < 0) {
        g_set_error(error, OSK_KEYBOARD_ERROR, errno,
                    "无法打开 %s：%s", device, g_strerror(errno));
        g_free(kbd);
        return NULL;
    }

    if (ioctl(kbd->fd, UI_SET_EVBIT, EV_KEY) < 0) {
        g_set_error(error, OSK_KEYBOARD_ERROR, errno,
                    "UI_SET_EVBIT 失败：%s", g_strerror(errno));
        goto fail;
    }

    /* 教学简化：把 1..KEY_MICMUTE 全注册。
       生产里应只注册真正需要的键，见 Step 5 的优化。 */
    for (unsigned short c = KEY_ESC; c <= KEY_MICMUTE; ++c)
        ioctl(kbd->fd, UI_SET_KEYBIT, c);

    struct uinput_setup setup = {0};
    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor  = 0x1234;
    setup.id.product = 0x5678;
    setup.id.version = 1;
    g_strlcpy(setup.name, "osk-keyboard", sizeof(setup.name));

    if (ioctl(kbd->fd, UI_DEV_SETUP, &setup) < 0) {
        g_set_error(error, OSK_KEYBOARD_ERROR, errno,
                    "UI_DEV_SETUP 失败：%s", g_strerror(errno));
        goto fail;
    }
    if (ioctl(kbd->fd, UI_DEV_CREATE) < 0) {
        g_set_error(error, OSK_KEYBOARD_ERROR, errno,
                    "UI_DEV_CREATE 失败：%s", g_strerror(errno));
        goto fail;
    }

    kbd->created = TRUE;
    return kbd;

fail: {
        int saved = errno;
        if (kbd->fd >= 0) close(kbd->fd);
        g_free(kbd);
        if (error && *error == NULL)
            g_set_error(error, OSK_KEYBOARD_ERROR, saved,
                        "初始化 uinput 失败");
        return NULL;
    }
}

/* ===== 按下 / 释放分离 =====
 *
 * 游戏布局要的是「真实键盘」的行为：手指按住时键一直处于按下状态，
 * 游戏引擎才能读到持续按住（跑步、蓄力、连射都是这么实现的）。
 * 原来的 osk_keyboard_chord() 是「按下+立刻抬起」的原子操作，
 * 只能用来打字，做不了这个。
 *
 * 这两个函数不自动处理修饰键 —— 调用方需要自己配对调用，
 * 保证每个 press 都有对应的 release，否则键会卡住。
 */

void osk_keyboard_key_down(OskKeyboard *kbd, unsigned short code) {
    if (!kbd || !kbd->created) return;
    write_event(kbd->fd, EV_KEY, code, 1);
    write_event(kbd->fd, EV_SYN, SYN_REPORT, 0);
}

void osk_keyboard_key_up(OskKeyboard *kbd, unsigned short code) {
    if (!kbd || !kbd->created) return;
    write_event(kbd->fd, EV_KEY, code, 0);
    write_event(kbd->fd, EV_SYN, SYN_REPORT, 0);
}

void osk_keyboard_chord(OskKeyboard *kbd, unsigned short code,
                        const unsigned short *mods, gsize n) {
    if (!kbd || !kbd->created) return;

    /* 1. 按下所有修饰键 */
    for (gsize i = 0; i < n; ++i)
        write_event(kbd->fd, EV_KEY, mods[i], 1);
    if (n > 0)
        write_event(kbd->fd, EV_SYN, SYN_REPORT, 0);

    /* 2. 主键按下 */
    write_event(kbd->fd, EV_KEY, code, 1);
    write_event(kbd->fd, EV_SYN, SYN_REPORT, 0);

    /* 3. 主键释放 */
    write_event(kbd->fd, EV_KEY, code, 0);
    write_event(kbd->fd, EV_SYN, SYN_REPORT, 0);

    /* 4. 逆序释放修饰键 */
    for (gsize i = n; i > 0; --i)
        write_event(kbd->fd, EV_KEY, mods[i - 1], 0);
    if (n > 0)
        write_event(kbd->fd, EV_SYN, SYN_REPORT, 0);
}

/* 松开所有当前按下的键。
 * 退出游戏布局、切换布局、窗口销毁时都要调一次 ——
 * 否则切走的那一刻按住的键会永远停在按下状态（sticky key），
 * 表现就是切回来之后系统一直以为你在按 W。 */
void osk_keyboard_release_all(OskKeyboard *kbd) {
    if (!kbd || !kbd->created) return;

    for (unsigned short c = 0; c <= KEY_MAX; ++c) {
        /* 只对可能注册过的键发抬起，避免每次遍历上千个码位刷爆缓冲 */
        if (c > KEY_MICMUTE && c < BTN_MISC) continue;
        write_event(kbd->fd, EV_KEY, c, 0);
    }
    write_event(kbd->fd, EV_SYN, SYN_REPORT, 0);
}

void osk_keyboard_free(OskKeyboard *kbd) {
    if (!kbd) return;
    if (kbd->created)
        ioctl(kbd->fd, UI_DEV_DESTROY);
    if (kbd->fd >= 0)
        close(kbd->fd);
    g_free(kbd);
}