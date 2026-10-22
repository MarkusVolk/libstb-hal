/*
 * The remote controls of an LG webOS TV
 *
 * Copyright (C) 2026 Markus Volk
 *
 * The TV's remotes are evdev devices the app may read. Their LG specific
 * codes are turned into the keys neutrino knows, and everything goes the
 * one way the other keys go too: the input FIFO. Keys the compositor
 * would also hand to the window are not taken from there, so no key
 * arrives twice.
 *
 * License: GPL-2.0-or-later
 */
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <map>
#include <poll.h>
#include <string>
#include <sys/ioctl.h>
#include <ctime>
#include <thread>
#include <unistd.h>
#include <vector>

#include "webos_input.h"
#include "webos_request.h"
#include "hal_debug.h"

static std::thread input_thread;
static std::atomic<bool> input_stop(false);
static webos_key_cb input_cb;
static std::atomic<bool> input_standby(false);
static int mapKey(int code);

#define hal_info(args...) _hal_info(HAL_DEBUG_INIT, NULL, args)

/* codes of the LG Magic Remote outside the standard set */
#define LG_KEY_HOME 0x305
#define LG_KEY_MORE 0x3e2
#define LG_KEY_MIC 0x1ac
#define LG_KEY_NETFLIX 0x40d
#define LG_KEY_PRIME 0x40e
#define LG_KEY_DISNEY 0x412
#define LG_KEY_LGCHANNELS 0x413
#define LG_KEY_RAKUTEN 0x414
#define LG_KEY_ALEXA 0x43e

/* the remote has no play, pause or stop keys: held long, OK plays and
 * pauses and back stops; short, they are what they always were */
#define LONG_PRESS_MS 600

struct held_key
{
	bool down;
	bool fired;
	struct timespec since;
};
static std::map<int, held_key> held;

/* not a key: guide held leaves neutrino for webOS, the one key webOS
 * does nothing with; home held belongs to the compositor, which opens
 * its screen zoom on it */
#define TO_WEBOS (-2)

static int longKey(int code)
{
	switch (code)
	{
		case KEY_ENTER:
			return KEY_PLAYPAUSE;
		case KEY_PREVIOUS:
			return KEY_STOP;
		case KEY_PROGRAM: /* guide */
			return TO_WEBOS;
		default:
			return -1;
	}
}

static long elapsedMs(const struct timespec &since)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (now.tv_sec - since.tv_sec) * 1000 + (now.tv_nsec - since.tv_nsec) / 1000000;
}

static void tap(int code)
{
	if (code == TO_WEBOS)
	{
		webos_request("webos-home", "1");
		return;
	}
	input_cb(code, 1);
	input_cb(code, 0);
}

/* a key with a long meaning is only passed on once it is clear which */
static void longPress(int code, int value)
{
	held_key &h = held[code];
	if (value == 1)
	{
		h.down = true;
		h.fired = false;
		clock_gettime(CLOCK_MONOTONIC, &h.since);
		return;
	}
	if (value == 2)
		return;
	if (h.down && !h.fired)
		tap(mapKey(code));
	h.down = false;
}

/* called while waiting: a key held long enough fires its long meaning */
static int checkHeld()
{
	int wait = 500;
	for (auto &k : held)
	{
		if (!k.second.down || k.second.fired)
			continue;
		long ms = elapsedMs(k.second.since);
		if (ms >= LONG_PRESS_MS)
		{
			tap(longKey(k.first));
			k.second.fired = true;
		}
		else if (LONG_PRESS_MS - ms < wait)
			wait = LONG_PRESS_MS - ms;
	}
	return wait;
}


/* -1: not neutrino's, the TV deals with it or it means nothing here; the
 * keys webOS would act on are kept from it by the window's access
 * policies (glfb_webos.cpp) */
static int mapKey(int code)
{
	switch (code)
	{
		case KEY_ENTER:
			return KEY_OK;
		case KEY_PREVIOUS: /* back */
			return KEY_EXIT;
		case LG_KEY_HOME:
			return KEY_MENU;
		case KEY_PROGRAM: /* guide */
			return KEY_EPG;
		case LG_KEY_MORE:
			return KEY_INFO;
		case KEY_VIDEO_NEXT: /* input */
			return KEY_PVR;
		case KEY_MENU: /* settings */
			return KEY_AUDIO;
		case LG_KEY_MIC: /* the remote has no record key */
			return KEY_RECORD;
		/* the app keys, where the remote has no transport keys */
		case LG_KEY_NETFLIX:
			return KEY_PLAYPAUSE;
		case LG_KEY_PRIME:
			return KEY_STOP;
		case LG_KEY_DISNEY:
			return KEY_REWIND;
		case LG_KEY_RAKUTEN:
			return KEY_FORWARD;
		case LG_KEY_LGCHANNELS:
			return KEY_TIME; /* timeshift */
		case LG_KEY_ALEXA:
			return KEY_MODE; /* tv / radio */
		case KEY_POWER:
		case KEY_VOLUMEUP:
		case KEY_VOLUMEDOWN:
		case KEY_MUTE:
			return -1;
		default:
			break;
	}
	/* mouse and pointer buttons of the Magic Remote */
	if (code >= BTN_MISC && code < KEY_OK)
		return -1;
	return code < 0x200 ? code : -1;
}

static bool hasKeys(int fd)
{
	unsigned long evbit = 0;
	if (ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), &evbit) < 0)
		return false;
	return evbit & (1 << EV_KEY);
}

/* devices come and go when a remote pairs or wakes up */
static void scan(std::map<std::string, int> &devs)
{
	DIR *dir = opendir("/dev/input");
	if (!dir)
		return;
	struct dirent *e;
	while ((e = readdir(dir)))
	{
		if (strncmp(e->d_name, "event", 5))
			continue;
		std::string path = std::string("/dev/input/") + e->d_name;
		if (devs.count(path))
			continue;
		int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;
		if (!hasKeys(fd))
		{
			close(fd);
			devs[path] = -1;
			continue;
		}
		char name[64] = "";
		ioctl(fd, EVIOCGNAME(sizeof(name)), name);
		hal_info("webOS input: %s (%s)\n", path.c_str(), name);
		devs[path] = fd;
	}
	closedir(dir);
}

static void loop()
{
	std::map<std::string, int> devs;
	time_t rescan = 0;
	while (!input_stop)
	{
		time_t now = time(NULL);
		if (now >= rescan)
		{
			scan(devs);
			rescan = now + 3;
		}
		std::vector<struct pollfd> pfds;
		std::vector<std::string> paths;
		for (auto &d : devs)
		{
			if (d.second < 0)
				continue;
			pfds.push_back({ d.second, POLLIN, 0 });
			paths.push_back(d.first);
		}
		if (poll(pfds.data(), pfds.size(), checkHeld()) <= 0)
			continue;
		for (size_t i = 0; i < pfds.size(); i++)
		{
			if (pfds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
			{
				close(pfds[i].fd);
				devs.erase(paths[i]);
				continue;
			}
			if (!(pfds[i].revents & POLLIN))
				continue;
			struct input_event ev[16];
			ssize_t n = read(pfds[i].fd, ev, sizeof(ev));
			if (n < 0 && errno == ENODEV)
			{
				close(pfds[i].fd);
				devs.erase(paths[i]);
				continue;
			}
			for (ssize_t k = 0; k < n / (ssize_t)sizeof(ev[0]); k++)
			{
				if (ev[k].type != EV_KEY)
					continue;
				if (input_standby)
				{
					int c = ev[k].code;
					if (ev[k].value == 1 && c != KEY_POWER && c != KEY_VOLUMEUP && c != KEY_VOLUMEDOWN && c != KEY_MUTE &&
					    (mapKey(c) >= 0 || longKey(c) != -1))
					{
						input_standby = false;
						held.clear();
						tap(KEY_POWER);
					}
					continue;
				}
				if (longKey(ev[k].code) != -1)
				{
					longPress(ev[k].code, ev[k].value);
					continue;
				}
				int code = mapKey(ev[k].code);
				if (code >= 0)
					input_cb(code, ev[k].value);
			}
		}
	}
	for (auto &d : devs)
		if (d.second >= 0)
			close(d.second);
}

void webos_input_start(webos_key_cb cb)
{
	if (input_thread.joinable())
		return;
	input_cb = cb;
	input_stop = false;
	input_thread = std::thread(loop);
}

void webos_input_standby(bool on)
{
	input_standby = on;
}

void webos_input_stop()
{
	input_stop = true;
	if (input_thread.joinable())
		input_thread.join();
}
