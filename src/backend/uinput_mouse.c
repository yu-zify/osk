#include "uinput_mouse.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/uinput.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define OSK_MOUSE_ERROR (osk_mouse_error_quark())

struct OskMouse {
    int fd;
    gboolean created;
};

static GQuark osk_mouse_error_quark(void) {
    return g_quark_from_static_string("osk-mouse-error");
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

OskMouse *osk_mouse_new(const char *device, GError **error) {
    OskMouse *m = g_new0(OskMouse, 1);
    m->fd = -1;

    m->fd = open(device, O_WRONLY | O_CLOEXEC);
    if (m->fd < 0) {
        g_set_error(error, OSK_MOUSE_ERROR, errno,
                    "无法打开 %s：%s", device, g_strerror(errno));
        g_free(m);
        return NULL;
    }

    ioctl(m->fd, UI_SET_EVBIT, EV_KEY);
    ioctl(m->fd, UI_SET_EVBIT, EV_REL);
    ioctl(m->fd, UI_SET_EVBIT, EV_SYN);

    ioctl(m->fd, UI_SET_KEYBIT, BTN_LEFT);
    ioctl(m->fd, UI_SET_KEYBIT, BTN_MIDDLE);
    ioctl(m->fd, UI_SET_KEYBIT, BTN_RIGHT);

    ioctl(m->fd, UI_SET_RELBIT, REL_X);
    ioctl(m->fd, UI_SET_RELBIT, REL_Y);
    ioctl(m->fd, UI_SET_RELBIT, REL_WHEEL);
    ioctl(m->fd, UI_SET_RELBIT, REL_HWHEEL);

    struct uinput_setup setup = {0};
    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor  = 0x1234;
    setup.id.product = 0x5679;
    setup.id.version = 1;
    g_strlcpy(setup.name, "osk-mouse", sizeof(setup.name));

    if (ioctl(m->fd, UI_DEV_SETUP, &setup) < 0 ||
        ioctl(m->fd, UI_DEV_CREATE) < 0) {
        g_set_error(error, OSK_MOUSE_ERROR, errno,
                    "UI_DEV_SETUP/CREATE 失败：%s", g_strerror(errno));
        close(m->fd);
        g_free(m);
        return NULL;
    }

    m->created = TRUE;
    g_print("[OSK] uinput 鼠标就绪：%s\n", device);
    return m;
}

void osk_mouse_move_rel(OskMouse *m, gdouble dx, gdouble dy) {
    if (!m || !m->created) return;

    int ix = (int)(dx + (dx >= 0 ? 0.5 : -0.5));
    int iy = (int)(dy + (dy >= 0 ? 0.5 : -0.5));
    if (ix == 0 && iy == 0) return;

    write_event(m->fd, EV_REL, REL_X, ix);
    write_event(m->fd, EV_REL, REL_Y, iy);
    write_event(m->fd, EV_SYN, SYN_REPORT, 0);
}

void osk_mouse_scroll(OskMouse *m, gint dx, gint dy) {
    if (!m || !m->created) return;
    if (dx == 0 && dy == 0) return;

    if (dy) write_event(m->fd, EV_REL, REL_WHEEL,  dy);
    if (dx) write_event(m->fd, EV_REL, REL_HWHEEL, dx);
    write_event(m->fd, EV_SYN, SYN_REPORT, 0);
}

void osk_mouse_button(OskMouse *m, guint button, gboolean pressed) {
    if (!m || !m->created) return;
    write_event(m->fd, EV_KEY, button, pressed ? 1 : 0);
    write_event(m->fd, EV_SYN, SYN_REPORT, 0);
}

void osk_mouse_free(OskMouse *m) {
    if (!m) return;
    if (m->created)
        ioctl(m->fd, UI_DEV_DESTROY);
    if (m->fd >= 0)
        close(m->fd);
    g_free(m);
}