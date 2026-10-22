/*
 * A PC or a set-top box: nothing of its own, see platform.h
 *
 * Copyright (C) 2026 Markus Volk
 *
 * License: GPL-2.0-or-later
 */
#include "platform.h"

void platform_caps(hw_caps_t *)
{
}

SDL_Window *platform_create_window(int, int)
{
	return NULL;
}

void platform_release_window()
{
}

bool platform_input_start(std::function<void(int, int)>)
{
	return false;
}

void platform_input_stop()
{
}

bool platform_standby(bool)
{
	return false;
}

cHwPlayer *platform_player()
{
	return NULL;
}
