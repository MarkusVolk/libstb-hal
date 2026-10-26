/*
 * An LG webOS TV, see platform.h
 *
 * Copyright (C) 2026 Markus Volk
 *
 * License: GPL-2.0-or-later
 */
#include <cstring>

#include "platform.h"
#include "glfb_webos.h"
#include "starfish_player.h"
#include "webos_input.h"
#include "webos_request.h"
#include "hal_debug.h"

#define hal_info(args...) _hal_info(HAL_DEBUG_INIT, NULL, args)

void platform_caps(hw_caps_t *caps)
{
	/* the TV is switched off by whoever started neutrino, after it ends
	 * with the poweroff action */
	caps->can_shutdown = 1;
	/* recordings go to the USB drive the TV mounts, not into the app home */
	caps->media_in_home = 0;
	strcpy(caps->boxmodel, "webos");
	strcpy(caps->boxvendor, "LG");
	strcpy(caps->boxname, "webOS TV");
}

/* the compositor of the TV shows a surface only with its own shell roles,
 * so the window is created without one and gets them here */
SDL_Window *platform_create_window(int width, int height)
{
	/* the compositor stretches the surface over the screen; at the size of
	 * the display mode the OSD gets a pixel of the screen for each of its own */
	const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
	if (mode && mode->w > 0 && mode->h > 0)
	{
		width = mode->w;
		height = mode->h;
	}
	SDL_PropertiesID props = SDL_CreateProperties();
	SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Neutrino");
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
	SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
	SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_OPENGL_BOOLEAN, true);
	SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_WAYLAND_SURFACE_ROLE_CUSTOM_BOOLEAN, true);
	SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_TRANSPARENT_BOOLEAN, true);
	SDL_Window *window = SDL_CreateWindowWithProperties(props);
	SDL_DestroyProperties(props);
	if (!window)
	{
		hal_info("GLFB: SDL_CreateWindowWithProperties failed: %s\n", SDL_GetError());
		return NULL;
	}
	if (!glfb_webos_attach(window, "neutrino"))
	{
		SDL_DestroyWindow(window);
		return NULL;
	}
	return window;
}

void platform_release_window()
{
	glfb_webos_release();
}

/* the Magic Remote also shows up as a gamepad and its keys come through
 * the compositor as well; read from evdev, each key arrives once */
bool platform_input_start(std::function<void(int, int)> cb)
{
	webos_input_start(cb);
	return true;
}

void platform_input_stop()
{
	webos_input_stop();
}

/* the panel goes off, neutrino stays; the next key wakes it */
bool platform_standby(bool on)
{
	webos_input_standby(on);
	webos_request("screen", on ? "off" : "on");
	return true;
}

cHwPlayer *platform_player()
{
	return new cStarfishPlayer();
}
