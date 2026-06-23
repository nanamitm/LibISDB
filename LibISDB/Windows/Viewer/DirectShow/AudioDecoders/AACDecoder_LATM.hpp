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
 @file   AACDecoder_LATM.hpp
 @brief  AAC (LATM/LOAS) デコーダ (FFmpeg)
 @author DBCTRADO
*/


#ifndef LIBISDB_AAC_DECODER_LATM_H
#define LIBISDB_AAC_DECODER_LATM_H


#ifdef LIBISDB_HAS_FFMPEG_AAC


#include "AudioDecoder.hpp"
#include "../../../../Base/DataBuffer.hpp"
#include <vector>


struct AVCodecContext;
struct AVPacket;
struct AVFrame;
struct SwrContext;


namespace LibISDB::DirectShow
{

	/**
	 @brief  LATM/LOAS 伝送された AAC (ISO/IEC 14496-3, stream_type 0x11) 用デコーダ

	 22.2ch 等の多チャンネル音声は LATM フレーミングで伝送されるが、コンシューマ
	 向けのオーディオ機器・LibISDB のチャンネルマップには 22.2ch という概念がそもそも
	 存在しない。そのため、デコード結果は元のチャンネル数に関わらず常に
	 libswresample でステレオへダウンミックスしてから返す。
	*/
	class AACDecoder_LATM
		: public AudioDecoder
	{
	public:
		AACDecoder_LATM() noexcept;
		~AACDecoder_LATM();

	// AudioDecoder
		bool Open() override;
		void Close() override;
		bool IsOpened() const override;
		bool Reset() override;
		bool Decode(const uint8_t *pData, size_t *pDataSize, ReturnArg<DecodeFrameInfo> Info) override;

		bool GetChannelMap(int Channels, int *pMap) const override;
		bool GetDownmixInfo(ReturnArg<DownmixInfo> Info) const override;

	// AACDecoder_LATM
		static void GetVersion(std::string *pVersion);

	private:
		bool OpenDecoder();
		void CloseDecoder();
		size_t FindLOASSync() const;
		bool DecodePacket(const uint8_t *pData, size_t Size, ReturnArg<DecodeFrameInfo> Info);

		AVCodecContext *m_pCodecContext;
		AVPacket *m_pPacket;
		AVFrame *m_pFrame;
		SwrContext *m_pSwrContext;
		std::vector<uint8_t> m_LOASBuffer;
		DataBuffer m_PCMBuffer;
	};

} // namespace LibISDB::DirectShow


#endif // LIBISDB_HAS_FFMPEG_AAC


#endif // ifndef LIBISDB_AAC_DECODER_LATM_H
