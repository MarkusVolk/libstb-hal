/*
 * The remote controls of an LG webOS TV
 *
 * Copyright (C) 2026 Markus Volk
 *
 * License: GPL-2.0-or-later
 */
#ifndef __WEBOS_INPUT_H__
#define __WEBOS_INPUT_H__

#include <functional>

/* code is a linux input key neutrino knows, value 0 up, 1 down, 2 held */
typedef std::function<void(int code, int value)> webos_key_cb;

void webos_input_start(webos_key_cb cb);
void webos_input_stop();
/* in standby the panel is off: the next key wakes neutrino, as the
 * power key it would want belongs to the TV */
void webos_input_standby(bool on);

#endif
