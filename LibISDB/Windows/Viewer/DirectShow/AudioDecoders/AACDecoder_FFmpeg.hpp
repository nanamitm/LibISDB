/*
  LibISDB
  Copyright(c) 2017-2020 DBCTRADO

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/**
 @file   AACDecoder_FFmpeg.hpp
 @brief  AAC デコーダ (FFmpeg)
 @author DBCTRADO
*/


#ifndef LIBISDB_AAC_DECODER_FFMPEG_H
#define LIBISDB_AAC_DECODER_FFMPEG_H


#ifdef LIBISDB_HAS_FFMPEG_AAC


#include "AACDecoder.hpp"
#include "../../../../MediaParsers/ADTSParser.hpp"


struct AVCodecContext;
struct AVPacket;
struct AVFrame;
struct SwrContext;


namespace LibISDB::DirectShow
{

	/** AAC デコーダクラス (FFmpeg) */
	class AACDecoder_FFmpeg
		: public AACDecoder
	{
	public:
		AACDecoder_FFmpeg() noexcept;
		~AACDecoder_FFmpeg();

	// AudioDecoder
		bool Open() override;
		void Close() override;
		bool IsOpened() const override;

		bool GetChannelMap(int Channels, int *pMap) const override;
		bool GetDownmixInfo(ReturnArg<DownmixInfo> Info) const override;

	// AACDecoder_FFmpeg
		static void GetVersion(std::string *pVersion);

	private:
		bool OpenDecoder() override;
		void CloseDecoder() override;
		bool DecodeFrame(const ADTSFrame *pFrame, ReturnArg<DecodeFrameInfo> Info) override;

		AVCodecContext *m_pCodecContext;
		AVPacket *m_pPacket;
		AVFrame *m_pFrame;
		SwrContext *m_pSwrContext;
		int m_SwrChannels;
		uint8_t m_LastChannelConfig;
		DataBuffer m_PCMBuffer;
	};

} // namespace LibISDB::DirectShow


#endif // LIBISDB_HAS_FFMPEG_AAC


#endif // ifndef LIBISDB_AAC_DECODER_FFMPEG_H
