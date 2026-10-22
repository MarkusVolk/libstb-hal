/*
 * What sets the hosts of libgeneric-pc apart
 *
 * Copyright (C) 2026 Markus Volk
 *
 * A PC or a set-top box needs nothing of its own here (platform.cpp);
 * an LG webOS TV has its own window, remote, standby and decoder
 * (webos/platform_webos.cpp). The build picks one of the two.
 *
 * License: GPL-2.0-or-later
 */
#ifndef __GENERIC_PLATFORM_H__
#define __GENERIC_PLATFORM_H__

#include <hardware_caps.h>

#ifdef __cplusplus
extern "C" {
#endif
/* what the device is and can do, over the generic defaults */
void platform_caps(hw_caps_t *caps);
#ifdef __cplusplus
}

#include <functional>
#include <SDL3/SDL.h>

class cHwPlayer;

/* the window neutrino draws into, full screen; NULL: the usual SDL window */
SDL_Window *platform_create_window(int width, int height);
void platform_release_window();
/* the remote, where the platform reads it itself, keys as linux input
 * codes; SDL's keys and gamepads are left alone then. false: SDL has them */
bool platform_input_start(std::function<void(int code, int value)> cb);
void platform_input_stop();
/* standby the platform's way; false: the usual way */
bool platform_standby(bool on);
/* the platform's own decoder, tried before mpv; NULL without one */
cHwPlayer *platform_player();
#endif

#endif
