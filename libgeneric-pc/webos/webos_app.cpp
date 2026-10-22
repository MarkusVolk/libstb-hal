/*
 * Neutrino as an app on an LG webOS TV: what it needs before main()
 *
 * Copyright (C) 2026 Markus Volk
 *
 * The TV lets an app use its media pipeline only from the program its
 * appinfo names as main, so neutrino is started directly, without a shell
 * script in front of it. What such a script would do happens here, when
 * the library is loaded.
 *
 * License: GPL-2.0-or-later
 */
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <unistd.h>

namespace fs = std::filesystem;

/* the only place the app's jail can write to and keeps across starts */
static const char *DATA_BASE = "/mnt/lg/flash/data/";

static void __attribute__((constructor)) webos_app_init()
{
	const char *appid = getenv("APPID");
	std::string data = std::string(DATA_BASE) + (appid ? appid : "org.tuxbox.neutrino");
	std::error_code ec;

	/* <app>/usr/bin/neutrino: the defaults the build installed for the
	 * data directory lie in <app>/data-defaults */
	char exe[PATH_MAX];
	ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n > 0)
	{
		exe[n] = '\0';
		fs::path app = fs::path(exe).parent_path().parent_path().parent_path();
		fs::path defaults = app / "data-defaults";
		if (!fs::exists(fs::path(data) / "etc", ec) && fs::is_directory(defaults, ec))
			fs::copy(defaults, data, fs::copy_options::recursive | fs::copy_options::skip_existing, ec);

		/* the programs, Lua modules and Python of the app lie below its
		 * directory, not below / where they were built for */
		std::string usr = (app / "usr").string();
		const char *path = getenv("PATH");
		setenv("PATH", (usr + "/bin" + (path ? std::string(":") + path : "")).c_str(), 1);
		setenv("LUA_PATH", (usr + "/share/lua/5.1/?.lua;" + usr + "/share/lua/5.1/?/init.lua;;").c_str(), 0);
		setenv("LUA_CPATH", (usr + "/lib/lua/5.1/?.so;;").c_str(), 0);
		setenv("PYTHONHOME", usr.c_str(), 0);
	}
	for (const char *d : { "/home", "/var/tuxbox", "/etc/neutrino/config" })
		fs::create_directories(data + d, ec);

	setenv("HOME", (data + "/home").c_str(), 1);
	setenv("XDG_RUNTIME_DIR", "/tmp/xdg", 0);
	setenv("WAYLAND_DISPLAY", "wayland-0", 0);
	setenv("SDL_VIDEO_DRIVER", "wayland", 0);
	/* there is no tuner; zapit loads channels only with a frontend */
	setenv("SIMULATE_FE", "1", 0);

	/* started by the TV, nothing reads stdout; keep it for debugging,
	 * the run before as neutrino.log.1 */
	if (!isatty(STDOUT_FILENO))
	{
		std::string log = data + "/neutrino.log";
		rename(log.c_str(), (log + ".1").c_str());
		if (freopen(log.c_str(), "w", stdout))
		{
			dup2(fileno(stdout), STDERR_FILENO);
			setvbuf(stdout, NULL, _IOLBF, 0);
		}
	}
}
