#include "uinput_gamepad.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/uinput.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define OSK_GAMEPAD_ERROR (osk_gamepad_error_quark())

/* 摇杆值域。用 -32767..32767 而不是 -32768..32767，
 * 正负对称，算死区的时候不用为负数多写一个分支。 */
#define STICK_MIN (-32767)
#define STICK_MAX ( 32767)

/* 摇杆死区：手指很难精确回到正中心，留一点容差避免漂移 */
#define STICK_DEADZONE 800

struct OskGamepad {
    int fd;
    gboolean created;
};

static GQuark osk_gamepad_error_quark(void) {
    return g_quark_from_static_string("osk-gamepad-error");
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

/* 钳制到合法值域 */
static gint clamp_stick(gint v) {
    if (v > STICK_MAX) v = STICK_MAX;
    if (v < STICK_MIN) v = STICK_MIN;
    return v;
}

OskGamepad *osk_gamepad_new(const char *device, GError **error) {
    OskGamepad *gp = g_new0(OskGamepad, 1);
    gp->fd = -1;

    gp->fd = open(device, O_WRONLY | O_CLOEXEC);
    if (gp->fd < 0) {
        g_set_error(error, OSK_GAMEPAD_ERROR, errno,
                    "无法打开 %s：%s", device, g_strerror(errno));
        g_free(gp);
        return NULL;
    }

    ioctl(gp->fd, UI_SET_EVBIT, EV_KEY);
    ioctl(gp->fd, UI_SET_EVBIT, EV_ABS);
    ioctl(gp->fd, UI_SET_EVBIT, EV_SYN);

    /* 手柄按键：ABXY + 双肩键 + start/select + 十字键 */
    static const unsigned short keys[] = {
        BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST,
        BTN_TL, BTN_TR,
        BTN_SELECT, BTN_START,
        BTN_DPAD_UP, BTN_DPAD_DOWN, BTN_DPAD_LEFT, BTN_DPAD_RIGHT,
    };
    for (gsize i = 0; i < G_N_ELEMENTS(keys); ++i)
        ioctl(gp->fd, UI_SET_KEYBIT, keys[i]);

    /* 四根摇杆轴 */
    static const unsigned short axes[] = { ABS_X, ABS_Y, ABS_RX, ABS_RY };
    for (gsize i = 0; i < G_N_ELEMENTS(axes); ++i) {
        struct uinput_abs_setup abs = {0};
        abs.code = axes[i];
        abs.absinfo.minimum = STICK_MIN;
        abs.absinfo.maximum = STICK_MAX;
        abs.absinfo.fuzz    = 16;
        abs.absinfo.flat    = STICK_DEADZONE;
        ioctl(gp->fd, UI_ABS_SETUP, &abs);
        ioctl(gp->fd, UI_SET_ABSBIT, axes[i]);
    }

    struct uinput_setup setup = {0};
    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor  = 0x1234;
    setup.id.product = 0x567A;
    setup.id.version = 1;
    g_strlcpy(setup.name, "osk-gamepad", sizeof(setup.name));

    if (ioctl(gp->fd, UI_DEV_SETUP, &setup) < 0 ||
        ioctl(gp->fd, UI_DEV_CREATE) < 0) {
        g_set_error(error, OSK_GAMEPAD_ERROR, errno,
                    "UI_DEV_SETUP/CREATE 失败：%s", g_strerror(errno));
        close(gp->fd);
        g_free(gp);
        return NULL;
    }

    gp->created = TRUE;
    g_print("[OSK] uinput 手柄就绪：%s\n", device);
    return gp;
}

void osk_gamepad_set_stick_left(OskGamepad *gp, gint x, gint y) {
    if (!gp || !gp->created) return;
    write_event(gp->fd, EV_ABS, ABS_X, clamp_stick(x));
    write_event(gp->fd, EV_ABS, ABS_Y, clamp_stick(y));
    write_event(gp->fd, EV_SYN, SYN_REPORT, 0);
}

void osk_gamepad_set_stick_right(OskGamepad *gp, gint x, gint y) {
    if (!gp || !gp->created) return;
    write_event(gp->fd, EV_ABS, ABS_RX, clamp_stick(x));
    write_event(gp->fd, EV_ABS, ABS_RY, clamp_stick(y));
    write_event(gp->fd, EV_SYN, SYN_REPORT, 0);
}

void osk_gamepad_button(OskGamepad *gp, guint code, gboolean pressed) {
    if (!gp || !gp->created) return;
    write_event(gp->fd, EV_KEY, code, pressed ? 1 : 0);
    write_event(gp->fd, EV_SYN, SYN_REPORT, 0);
}

void osk_gamepad_release_all(OskGamepad *gp) {
    if (!gp || !gp->created) return;

    osk_gamepad_set_stick_left(gp, 0, 0);
    osk_gamepad_set_stick_right(gp, 0, 0);

    static const unsigned short keys[] = {
        BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST,
        BTN_TL, BTN_TR,
        BTN_SELECT, BTN_START,
        BTN_DPAD_UP, BTN_DPAD_DOWN, BTN_DPAD_LEFT, BTN_DPAD_RIGHT,
    };
    for (gsize i = 0; i < G_N_ELEMENTS(keys); ++i)
        write_event(gp->fd, EV_KEY, keys[i], 0);
    write_event(gp->fd, EV_SYN, SYN_REPORT, 0);
}

void osk_gamepad_free(OskGamepad *gp) {
    if (!gp) return;
    if (gp->created) {
        osk_gamepad_release_all(gp);
        ioctl(gp->fd, UI_DEV_DESTROY);
    }
    if (gp->fd >= 0)
        close(gp->fd);
    g_free(gp);
}
