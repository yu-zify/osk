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

void osk_keyboard_free(OskKeyboard *kbd) {
    if (!kbd) return;
    if (kbd->created)
        ioctl(kbd->fd, UI_DEV_DESTROY);
    if (kbd->fd >= 0)
        close(kbd->fd);
    g_free(kbd);
}