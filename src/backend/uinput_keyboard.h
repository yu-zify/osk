#ifndef OSK_UINPUT_KEYBOARD_H
#define OSK_UINPUT_KEYBOARD_H

#include <glib.h>

typedef struct OskKeyboard OskKeyboard;

OskKeyboard *osk_keyboard_new(const char *device, GError **error);

void osk_keyboard_chord(OskKeyboard *kbd, unsigned short code,
                        const unsigned short *mods, gsize n);

void osk_keyboard_free(OskKeyboard *kbd);

#endif