/*
 * Live TV of the HiSilicon boxes through the vendor AVPLAY
 *
 * The driver of these boxes emulates the DVB decoder devices with a hidden
 * vendor service whose AVPLAY plays slowly after every zap until audio and
 * video are in sync. This plays the live programme with an AVPLAY of our
 * own on a free HiSilicon demux of the tuner's port, synchronised to the
 * audio without slow playing. Everything else - tuner, sections,
 * recording, the players of files and the timeshift - stays on the
 * emulated devices.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __HISI_LIVE_H__
#define __HISI_LIVE_H__

#include <stdint.h>

class cHisiLive
{
	public:
		enum { PID_VIDEO, PID_AUDIO, PID_PCR, PID_COUNT };

		/* NULL where the box lacks the vendor libraries or HAL_HISI=0 */
		static cHisiLive *getInstance();
		/* the decoder unit it stands for: the live one, not PiP */
		static cHisiLive *forUnit(int unit) { return unit == 0 ? getInstance() : nullptr; }

		void SetPid(int which, unsigned short pid);
		void SetVideoType(int video_format);
		void SetAudioType(int zapit_type);
		bool StartVideo();
		void StopVideo(bool blank);
		/* MPEG audio and AAC; AC3 and the like stay on the emulated decoder */
		bool DecodesAudio();
		bool StartAudio();
		void StopAudio();
		/* the window away for the still pictures and players of the
		 * emulated devices; radio keeps its audio */
		void Hide();
		int64_t GetPts();
		/* false while live video is off: the emulated device knows then */
		bool PictureInfo(int &width, int &height, int &rate_mhz);
		/* gives AVPLAY, window, track and demux back */
		void Close();

	private:
		cHisiLive() {}
		bool Init();
		bool AttachDemux();
		void SetSync(unsigned ref);
		struct priv;
		priv *p = nullptr;
};

#endif
