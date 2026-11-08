/*
 * cenc://<n>: a DASH stream with Widevine, opened by ffmpeg in the process,
 * its samples decrypted by the CDM and handed to mpv as Matroska, so that
 * mpv decodes them like any other file, with the hardware decoder.
 *
 * Copyright (C) 2026 Markus Volk
 *
 * License: GPL-2.0-or-later
 */
#ifndef __CENC_STREAM_H__
#define __CENC_STREAM_H__

#include <cstdint>
#include <string>
#include <vector>

#include <mpv/client.h>

struct cCencParams
{
	std::string manifest;
	std::vector<std::string> headers;	/* "Name: value" for the media */
	std::string user_agent;
	std::string license_url;
	std::string license_headers;	/* "Name: value" lines for the license request */
	std::string certificate_url;
	int64_t start_ms = 0;
	int max_height = 2160;
};

namespace cenc
{
	void registerProtocol(mpv_handle *h);
	/* the address mpv plays the stream under */
	std::string add(const cCencParams &p);
	bool params(const std::string &url, cCencParams &p);
	/* known once the manifest is read, 0 for a live stream */
	int64_t durationMs(const std::string &url);
}

#endif
