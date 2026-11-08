/*
 * widevine-check: whether the Widevine CDM in Neutrino's directory loads.
 * Prints "version <v>" and exits 0, or the reason on stderr and exits 1.
 * With --dir it prints the directory the CDM is looked for in.
 *
 * Copyright (C) 2026 Markus Volk
 *
 * License: GPL-2.0-or-later
 */

#include <stdio.h>
#include <string.h>

#include <string>

#include "widevine_hal.h"

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "--dir"))
	{
		printf("%s\n", cWidevine::directory().c_str());
		return 0;
	}
	std::string version, error;
	if (!cWidevine::probe(version, error))
	{
		fprintf(stderr, "%s\n", error.c_str());
		return 1;
	}
	printf("version %s\n", version.c_str());
	return 0;
}
