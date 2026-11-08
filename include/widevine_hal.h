/*
 * Widevine in the process: a CDM put onto the box by the user (Google does
 * not allow it in an image) takes the licenses and decrypts the CENC samples,
 * which the player then decodes like clear ones.
 *
 * Copyright (C) 2026 Markus Volk
 *
 * License: GPL-2.0-or-later
 */
#ifndef __WIDEVINE_HAL_H__
#define __WIDEVINE_HAL_H__

#include <cstdint>
#include <string>
#include <vector>

struct AVPacket;

class cWidevine
{
	public:
		/* where libwidevinecdm.so is looked for: NEUTRINO_WIDEVINE_DIR from
		 * the environment, else the directory below Neutrino's configuration */
		static std::string directory();
		/* loads the CDM and creates an instance of it; false with the
		 * reason in error */
		static bool probe(std::string &version, std::string &error);

		cWidevine();
		~cWidevine();

		/* a session for the init data (one or more pssh boxes): the CDM's
		 * challenge goes in a POST to license_url, headers as
		 * "Name: value" lines, and the answer is the license. The service
		 * certificate is optional. True once a key is usable. */
		bool open(const std::vector<uint8_t> &init_data, const std::string &license_url,
			  const std::string &headers, const std::vector<uint8_t> &certificate,
			  std::string &error);

		enum Result { DECRYPTED, CLEAR, NO_KEY, FAILED };
		/* decrypts a packet with AV_PKT_DATA_ENCRYPTION_INFO in place and
		 * removes that side data; a packet without it stays as it is */
		Result decrypt(AVPacket *pkt);

	private:
		struct Session;
		Session *s;
};

#endif // __WIDEVINE_HAL_H__
