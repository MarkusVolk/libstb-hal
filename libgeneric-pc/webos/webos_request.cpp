/*
 * Requests to the script that started neutrino on an LG webOS TV
 *
 * Copyright (C) 2026 Markus Volk
 *
 * License: GPL-2.0-or-later
 */
#include <cstdio>
#include <string>

#include "webos_request.h"

void webos_request(const char *what, const char *value)
{
	std::string path = std::string("/tmp/neutrino.") + what;
	std::string tmp = path + ".new";
	FILE *f = fopen(tmp.c_str(), "w");
	if (!f)
		return;
	fputs(value, f);
	fclose(f);
	rename(tmp.c_str(), path.c_str());
}
