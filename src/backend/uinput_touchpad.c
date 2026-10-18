#include "uinput_touchpad.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/uinput.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define OSK_TP_ERROR (osk_touchpad_error_quark())
#define MAX_SLOTS 10

struct OskTouchpad {
    int fd;
    gboolean created;
    int width, height;
    /* slot 管理：id → slot 索引。0 表示未占用，实际 id 从 1 开始。 */
    guint slots[MAX_SLOTS];
    int   num_slots;
    int   next_slot;   /* 循环分配 */
};

static GQuark osk_touchpad_error_quark(void) {
    return g_quark_from_static_string("osk-touchpad-error");
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

static gboolean abs_setup(int fd, unsigned short code,
                           int min, int max, int resolution) {
    struct uinput_abs_setup s = {0};
    s.code = code;
    s.absinfo.minimum = min;
    s.absinfo.maximum = max;
    s.absinfo.resolution = resolution; // 设置分辨率
    return ioctl(fd, UI_ABS_SETUP, &s) == 0;
}

OskTouchpad *osk_touchpad_new(const char *device,
                               int width, int height,
                               GError **error) {
    OskTouchpad *tp = g_new0(OskTouchpad, 1);
    tp->fd = -1;
    tp->width = width;
    tp->height = height;

    tp->fd = open(device, O_WRONLY | O_CLOEXEC);
    if (tp->fd < 0) {
        g_set_error(error, OSK_TP_ERROR, errno,
                    "无法打开 %s：%s", device, g_strerror(errno));
        g_free(tp);
        return NULL;
    }

// 在 osk_touchpad_new 中，创建 fd 之后，UI_DEV_SETUP 之前

// 事件类型
ioctl(tp->fd, UI_SET_EVBIT, EV_KEY);
ioctl(tp->fd, UI_SET_EVBIT, EV_ABS);
ioctl(tp->fd, UI_SET_EVBIT, EV_SYN); // 别忘了 SYN

// 按键 - 健全性检查要求的
ioctl(tp->fd, UI_SET_KEYBIT, BTN_TOUCH);
ioctl(tp->fd, UI_SET_KEYBIT, BTN_TOOL_FINGER);
// 其他辅助按键
ioctl(tp->fd, UI_SET_KEYBIT, BTN_LEFT);
ioctl(tp->fd, UI_SET_KEYBIT, BTN_TOOL_DOUBLETAP);
ioctl(tp->fd, UI_SET_KEYBIT, BTN_TOOL_TRIPLETAP);

// 绝对坐标轴 - 健全性检查要求的
ioctl(tp->fd, UI_SET_ABSBIT, ABS_X);
ioctl(tp->fd, UI_SET_ABSBIT, ABS_Y);
// 其他多指协议轴
ioctl(tp->fd, UI_SET_ABSBIT, ABS_MT_SLOT);
ioctl(tp->fd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
ioctl(tp->fd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
ioctl(tp->fd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
ioctl(tp->fd, UI_SET_ABSBIT, ABS_MT_PRESSURE);

// 设备属性
ioctl(tp->fd, UI_SET_PROPBIT, INPUT_PROP_POINTER);
ioctl(tp->fd, UI_SET_PROPBIT, INPUT_PROP_BUTTONPAD);
    // 在主坐标轴上设置分辨率
abs_setup(tp->fd, ABS_X, 0, width, 100);
abs_setup(tp->fd, ABS_Y, 0, height, 100);

// 在多指坐标轴上设置分辨率
abs_setup(tp->fd, ABS_MT_POSITION_X, 0, width, 100);
abs_setup(tp->fd, ABS_MT_POSITION_Y, 0, height, 100);

// 其他轴（如 ABS_MT_SLOT, ABS_MT_TRACKING_ID）的分辨率通常不关键，可以传 0 或省略
abs_setup(tp->fd, ABS_MT_SLOT, 0, MAX_SLOTS - 1, 0);
abs_setup(tp->fd, ABS_MT_TRACKING_ID, 0, 65535, 0);
abs_setup(tp->fd, ABS_MT_PRESSURE, 0, 255, 0);

    /* 关键属性：让 libinput 把设备识别为触控板 */
  //  ioctl(tp->fd, UI_SET_PROPBIT, INPUT_PROP_POINTER);
if (ioctl(tp->fd, UI_SET_PROPBIT, INPUT_PROP_POINTER) < 0) {
    g_warning("[tp] UI_SET_PROPBIT INPUT_PROP_POINTER 失败: %s",
              g_strerror(errno));
} else {
    g_print("[tp] INPUT_PROP_POINTER 已设置\n");
}
    struct uinput_setup setup = {0};
    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor  = 0x1234;
    setup.id.product = 0x5679;   /* 和键盘区分 */
    setup.id.version = 1;
    g_strlcpy(setup.name, "osk-touchpad", sizeof(setup.name));

    if (ioctl(tp->fd, UI_DEV_SETUP, &setup) < 0 ||
        ioctl(tp->fd, UI_DEV_CREATE) < 0) {
        g_set_error(error, OSK_TP_ERROR, errno,
                    "UI_DEV_SETUP/CREATE 失败：%s", g_strerror(errno));
        goto fail;
    }

    tp->created = TRUE;
    g_print("[OSK] uinput 触控板就绪：%s (%dx%d)\n",
            device, width, height);
    return tp;

fail:
    if (tp->fd >= 0) close(tp->fd);
    g_free(tp);
    return NULL;
}

/* 查找 id 对应的 slot；未找到返回 -1 */
static int find_slot(OskTouchpad *tp, guint id) {
    for (int i = 0; i < MAX_SLOTS; ++i)
        if (tp->slots[i] == id)
            return i;
    return -1;
}

/* 分配一个空闲 slot；失败返回 -1 */
static int alloc_slot(OskTouchpad *tp, guint id) {
    for (int i = 0; i < MAX_SLOTS; ++i) {
        if (tp->slots[i] == 0) {
            tp->slots[i] = id;
            if (i + 1 > tp->num_slots)
                tp->num_slots = i + 1;
            return i;
        }
    }
    return -1;
}

static void send_tool_state(OskTouchpad *tp) {
    int n = 0;
    for (int i = 0; i < MAX_SLOTS; ++i)
        if (tp->slots[i]) n++;

    // BTN_TOUCH 表示有物理接触
    write_event(tp->fd, EV_KEY, BTN_TOUCH, n > 0 ? 1 : 0);
    // BTN_TOOL_FINGER 表示单指
    write_event(tp->fd, EV_KEY, BTN_TOOL_FINGER, n == 1 ? 1 : 0);
    write_event(tp->fd, EV_KEY, BTN_TOOL_DOUBLETAP, n == 2 ? 1 : 0);
    write_event(tp->fd, EV_KEY, BTN_TOOL_TRIPLETAP, n == 3 ? 1 : 0);
}

void osk_touchpad_down(OskTouchpad *tp, guint id, int x, int y) {
    g_print("[tp-backend] down id=%u x=%d y=%d\n", id, x, y);
    if (!tp || !tp->created) return;
    if (find_slot(tp, id) >= 0) return;

    int slot = alloc_slot(tp, id);
    if (slot < 0) return;

    write_event(tp->fd, EV_ABS, ABS_MT_SLOT, slot);
    write_event(tp->fd, EV_ABS, ABS_MT_TRACKING_ID, (int)id);
    write_event(tp->fd, EV_ABS, ABS_MT_POSITION_X, x);
    write_event(tp->fd, EV_ABS, ABS_MT_POSITION_Y, y);

    /* 首指时同步更新 ABS_X / ABS_Y（libinput 用于确定设备状态） */
    if (slot == 0) {
        write_event(tp->fd, EV_ABS, ABS_X, x);
        write_event(tp->fd, EV_ABS, ABS_Y, y);
        write_event(tp->fd, EV_KEY, BTN_LEFT, 1);
         write_event(tp->fd, EV_KEY, BTN_LEFT, 0);
    }

    send_tool_state(tp);
    write_event(tp->fd, EV_SYN, SYN_REPORT, 0);
}

void osk_touchpad_move(OskTouchpad *tp, guint id, int x, int y) {
    g_print("[tp-backend] move id=%u x=%d y=%d\n", id, x, y);
    if (!tp || !tp->created) return;
    int slot = find_slot(tp, id);
    if (slot < 0) return;

    write_event(tp->fd, EV_ABS, ABS_MT_SLOT, slot);
    write_event(tp->fd, EV_ABS, ABS_MT_POSITION_X, x);
    write_event(tp->fd, EV_ABS, ABS_MT_POSITION_Y, y);

    if (slot == 0) {
        write_event(tp->fd, EV_ABS, ABS_X, x);
        write_event(tp->fd, EV_ABS, ABS_Y, y);
    }

    write_event(tp->fd, EV_SYN, SYN_REPORT, 0);
}

void osk_touchpad_up(OskTouchpad *tp, guint id) {
    g_print("[tp-backend] up id=%u\n", id);
    if (!tp || !tp->created) return;
    int slot = find_slot(tp, id);
    if (slot < 0) return;

    write_event(tp->fd, EV_ABS, ABS_MT_SLOT, slot);
    write_event(tp->fd, EV_ABS, ABS_MT_TRACKING_ID, -1);

    tp->slots[slot] = 0;

    send_tool_state(tp);
    write_event(tp->fd, EV_SYN, SYN_REPORT, 0);
}

void osk_touchpad_free(OskTouchpad *tp) {
    if (!tp) return;
    if (tp->created)
        ioctl(tp->fd, UI_DEV_DESTROY);
    if (tp->fd >= 0)
        close(tp->fd);
    g_free(tp);
}