/*
 * webOS shell role for the SDL window
 *
 * Copyright (C) 2026 Markus Volk
 *
 * License: GPL-2.0-or-later
 */
#ifndef __GLFB_WEBOS_H__
#define __GLFB_WEBOS_H__

#include <SDL3/SDL.h>

/* gives the window's wl_surface the wl_shell and wl_webos_shell roles the
 * compositor of a webOS TV wants before it shows anything; the window must
 * have been created with the custom Wayland surface role */
bool glfb_webos_attach(SDL_Window *window, const char *appid);
void glfb_webos_release();

#endif
