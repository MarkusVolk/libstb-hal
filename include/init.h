#ifndef __INIT_H__
#define __INIT_H__

#include <stdint.h>

void hal_api_init();
void hal_api_exit();

/* the display and the input devices go to another program and come back;
 * nothing happens where the HAL owns no window */
void hal_display_suspend();
void hal_display_resume();

/* a key for a terminal: a KEY_* code from linux/input.h for a key that does
 * not type a character, or the character that was typed */
struct hal_term_key
{
	uint32_t code;
	uint32_t unicode;
	uint32_t mods;
};
#define HAL_MOD_SHIFT	1
#define HAL_MOD_CTRL	2
#define HAL_MOD_ALT	4
/* while fd is not -1, the keyboard of the window writes hal_term_key to it
 * instead of sending remote control keys; nothing happens without a window */
void hal_set_terminal_fd(int fd);

#endif // __INIT_H__
