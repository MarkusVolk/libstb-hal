/*
 * Requests to the script that started neutrino on an LG webOS TV
 *
 * Copyright (C) 2026 Markus Volk
 *
 * License: GPL-2.0-or-later
 */
#ifndef __WEBOS_REQUEST_H__
#define __WEBOS_REQUEST_H__

/* the app may not call the TV's power and launcher services itself; the
 * script that started it runs as root and does, on a file in /tmp named
 * neutrino.<what> */
void webos_request(const char *what, const char *value);

#endif
