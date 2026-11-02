/*
 * Live TV of the HiSilicon boxes through the vendor AVPLAY
 *
 * The driver of these boxes emulates the DVB decoder devices with a hidden
 * vendor service whose AVPLAY plays slowly after every zap until audio and
 * video are in sync. This plays the live programme with an AVPLAY of our
 * own on a free HiSilicon demux of the tuner's port, and what cDvbPlayer
 * plays - recordings, the timeshift, files and streams - through a RAM
 * port of that demux. The slow start stays hidden behind a frozen window.
 * Tuner, sections and recording stay on the emulated devices.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __HISI_LIVE_H__
#define __HISI_LIVE_H__

#include <stddef.h>
#include <stdint.h>
#include <functional>

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
		/* MPEG audio and AAC, AC3, E-AC3 and DTS where the vendor's FFmpeg
		 * loads; anything else stays on the emulated decoder */
		bool DecodesAudio();
		bool StartAudio();
		void StopAudio();
		/* the window away for the still pictures and players of the
		 * emulated devices; radio keeps its audio */
		void Hide();
		int64_t GetPts();
		/* false while live video is off: the emulated device knows then */
		bool PictureInfo(int &width, int &height, int &rate_mhz);
		/* Neutrino's screen formats: 1 4:3, 2 14:9, 3 16:9 */
		void SetScreenAspect(int aspect);
		/* Neutrino's aspect modes: letterbox, panscan, bestfit, nonlinear */
		void SetAspectMode(int mode);
		/* in the 720x576 plane of /proc/stb/vmpeg, a width of 0 is the whole screen */
		void SetWindowRect(int x, int y, int w, int h);
		/* the picture shown, semiplanar 4:2:0, handed to use while it is mapped */
		struct Frame
		{
			const uint8_t *y;
			int y_stride;
			const uint8_t *c;
			int c_stride;
			int width, height;
			bool nv12;
		};
		bool Capture(const std::function<bool(const Frame &)> &use);
		/* the players' transport stream through a RAM port of the demux:
		 * PIDs 0x100 video, 0x101 sound, no PCR; the decoders as for live
		 * TV, and a held live picture stays until the stream's */
		bool StartPlayback(int video_format, int audio_type, bool sound);
		void StopPlayback();
		bool Playing();
		/* whole TS packets, false while the buffer has no room */
		bool Feed(const uint8_t *ts, size_t len);
		void ClearPlayback();
		void PausePlayback(bool on);
		void TrickPlayback(bool on);
		/* gives AVPLAY, window, track and demux back */
		void Close();

	private:
		cHisiLive() {}
		bool Init();
		bool AttachDemux();
		void SetSync(unsigned ref);
		void Reveal(unsigned start);
		/* the AVPLAY's channels for the source playing: Start and Stop
		 * leave a player's alone and take live TV up after it */
		bool startVideo();
		void stopVideo(bool blank);
		bool startAudio();
		void stopAudio();
		bool OpenTsBuffer();
		struct priv;
		priv *p = nullptr;
};

#endif
