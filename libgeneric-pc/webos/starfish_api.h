/*
 * LG's StarfishMediaAPIs, the media pipeline of webOS (libplayerAPIs.so.1)
 *
 * The declarations follow the public domain stubs of
 * https://github.com/webosbrew/webos-userland, limited to what the player
 * here uses. The library keeps its own state inside the object; its size
 * is not known, the storage below is what webos-userland reserves.
 *
 * License: GPL-2.0-or-later
 */
#ifndef __STARFISH_API_H__
#define __STARFISH_API_H__

#include <cstdint>
#include <string>

class StarfishMediaAPIs
{
	public:
		StarfishMediaAPIs(const char *uid = nullptr);
		~StarfishMediaAPIs();

		std::string Feed(const char *payload);
		bool flush();
		const char *getMediaID();
		bool Load(const char *payload, void (*callback)(int type, int64_t numValue, const char *strValue, void *data), void *data);
		bool notifyForeground();
		bool Pause();
		bool Play();
		bool pushEOS();
		bool SetPlayRate(const char *payload);
		bool setTimeToDecode(const char *payload);
		bool Unload();

	private:
		alignas(8) char storage[76 + 8 + 4096];
};

enum
{
	SF_EVENT_FRAMEREADY = 0x0,
	SF_EVENT_STR_VIDEO_INFO = 0x4,
	SF_EVENT_STR_AUDIO_INFO = 0x7,
	SF_EVENT_INT_ERROR = 0x12,
	SF_EVENT_STR_ERROR = 0x13,
	SF_EVENT_LOADCOMPLETED = 0x16,
	SF_EVENT_UNLOADCOMPLETED = 0x17,
	SF_EVENT_PLAYING = 0x1a,
	SF_EVENT_PAUSED = 0x1b,
	SF_EVENT_ENDOFSTREAM = 0x1c,
	SF_EVENT_STR_BUFFERFULL = 0x2d,
	SF_EVENT_DROPPED_FRAME = 0x30,
};

#endif
