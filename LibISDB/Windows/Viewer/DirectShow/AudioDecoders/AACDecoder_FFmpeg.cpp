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
 @file   AACDecoder_FFmpeg.cpp
 @brief  AAC デコーダ (FFmpeg)
 @author DBCTRADO
*/


#include "../../../../LibISDBPrivate.hpp"


#ifdef LIBISDB_HAS_FFMPEG_AAC


#include "AACDecoder_FFmpeg.hpp"
#include "../../../../Base/DebugDef.hpp"
#include <cstdio>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}


#pragma comment(lib, "avcodec.lib")
#pragma comment(lib, "avutil.lib")
#pragma comment(lib, "swresample.lib")


namespace LibISDB::DirectShow
{


void AACDecoder_FFmpeg::GetVersion(std::string *pVersion)
{
	char Buf[64];
	const unsigned int Version = ::avcodec_version();
	std::snprintf(
		Buf, sizeof(Buf), "FFmpeg libavcodec %d.%d.%d",
		AV_VERSION_MAJOR(Version), AV_VERSION_MINOR(Version), AV_VERSION_MICRO(Version));
	*pVersion = Buf;
}


AACDecoder_FFmpeg::AACDecoder_FFmpeg() noexcept
	: m_pCodecContext(nullptr)
	, m_pPacket(nullptr)
	, m_pFrame(nullptr)
	, m_pSwrContext(nullptr)
	, m_SwrChannels(0)
	, m_LastChannelConfig(0xFF)
{
}


AACDecoder_FFmpeg::~AACDecoder_FFmpeg()
{
	Close();
}


bool AACDecoder_FFmpeg::Open()
{
	if (!AACDecoder::Open())
		return false;

	constexpr size_t PCM_BUFFER_SIZE = 6 * sizeof(int16_t) * 4096;
	if (m_PCMBuffer.AllocateBuffer(PCM_BUFFER_SIZE) < PCM_BUFFER_SIZE) {
		Close();
		return false;
	}

	return true;
}


void AACDecoder_FFmpeg::Close()
{
	AACDecoder::Close();

	m_PCMBuffer.FreeBuffer();
}


bool AACDecoder_FFmpeg::IsOpened() const
{
	return m_pCodecContext != nullptr;
}


bool AACDecoder_FFmpeg::GetChannelMap(int Channels, int *pMap) const
{
	// 段階導入のため、まずは既存の FAAD2 / FDK AAC と同じ 2ch / 6ch のみを
	// サポートする。22.2ch (24ch) 対応はこのデコーダで FFmpeg 連携が
	// 問題なく動作することを確認した後の次段階とする。
	switch (Channels) {
	case 2:
		pMap[CHANNEL_2_L] = 0;
		pMap[CHANNEL_2_R] = 1;
		break;

	case 6:
		pMap[CHANNEL_6_FL]  = 0;
		pMap[CHANNEL_6_FR]  = 1;
		pMap[CHANNEL_6_FC]  = 2;
		pMap[CHANNEL_6_LFE] = 3;
		pMap[CHANNEL_6_BL]  = 4;
		pMap[CHANNEL_6_BR]  = 5;
		break;

	default:
		return false;
	}

	return true;
}


bool AACDecoder_FFmpeg::GetDownmixInfo(ReturnArg<DownmixInfo> Info) const
{
	if (!Info)
		return false;

	constexpr double PSQR = 1.0 / 1.4142135623730950488016887242097;

	Info->Center = PSQR;
	Info->Front  = 1.0;
	Info->Rear   = PSQR;
	Info->LFE    = 0.0;

	return true;
}


bool AACDecoder_FFmpeg::OpenDecoder()
{
	CloseDecoder();

	const AVCodec *pCodec = ::avcodec_find_decoder(AV_CODEC_ID_AAC);
	if (pCodec == nullptr)
		return false;

	m_pCodecContext = ::avcodec_alloc_context3(pCodec);
	if (m_pCodecContext == nullptr)
		return false;

	if (::avcodec_open2(m_pCodecContext, pCodec, nullptr) < 0) {
		CloseDecoder();
		return false;
	}

	m_pPacket = ::av_packet_alloc();
	m_pFrame = ::av_frame_alloc();
	if ((m_pPacket == nullptr) || (m_pFrame == nullptr)) {
		CloseDecoder();
		return false;
	}

	m_LastChannelConfig = 0xFF;
	m_DecodeError = false;

	return true;
}


void AACDecoder_FFmpeg::CloseDecoder()
{
	if (m_pSwrContext != nullptr) {
		::swr_free(&m_pSwrContext);
		m_pSwrContext = nullptr;
	}
	m_SwrChannels = 0;

	if (m_pFrame != nullptr) {
		::av_frame_free(&m_pFrame);
		m_pFrame = nullptr;
	}

	if (m_pPacket != nullptr) {
		::av_packet_free(&m_pPacket);
		m_pPacket = nullptr;
	}

	if (m_pCodecContext != nullptr) {
		::avcodec_free_context(&m_pCodecContext);
		m_pCodecContext = nullptr;
	}
}


bool AACDecoder_FFmpeg::DecodeFrame(const ADTSFrame *pFrame, ReturnArg<DecodeFrameInfo> Info)
{
	if (m_pCodecContext == nullptr)
		return false;

	if (pFrame->GetChannelConfig() != m_LastChannelConfig) {
		// チャンネル設定が変化した、デコーダリセット
		LIBISDB_TRACE(
			LIBISDB_STR("AACDecoder_FFmpeg::DecodeFrame() Channel config changed {} -> {}\n"),
			m_LastChannelConfig,
			pFrame->GetChannelConfig());
		if (!ResetDecoder())
			return false;

		m_LastChannelConfig = pFrame->GetChannelConfig();
	}

	::av_packet_unref(m_pPacket);
	m_pPacket->data = const_cast<uint8_t *>(pFrame->GetData());
	m_pPacket->size = static_cast<int>(pFrame->GetSize());

	int Ret = ::avcodec_send_packet(m_pCodecContext, m_pPacket);
	if (Ret < 0) {
		LIBISDB_TRACE(LIBISDB_STR("avcodec_send_packet() error {}\n"), Ret);
		return false;
	}

	Ret = ::avcodec_receive_frame(m_pCodecContext, m_pFrame);
	if (Ret < 0) {
		// AVERROR(EAGAIN): デコーダがまだ出力を持っていない (初回フレーム等)。
		// 致命的ではなく、次の Decode() 呼び出しで揃うことがある。
		return false;
	}

	const int Channels = m_pFrame->ch_layout.nb_channels;
	const int SampleRate = m_pFrame->sample_rate;

	if ((Channels <= 0) || (SampleRate <= 0))
		return false;

	// FFmpeg の AAC デコーダはプレーナ float (AV_SAMPLE_FMT_FLTP) で出力する。
	// FAAD2 / FDK AAC と同じ、インタリーブされた 16bit PCM に変換して
	// 下流に渡す。
	if ((m_pSwrContext == nullptr) || (m_SwrChannels != Channels)) {
		if (m_pSwrContext != nullptr) {
			::swr_free(&m_pSwrContext);
			m_pSwrContext = nullptr;
		}

		AVChannelLayout OutLayout;
		::av_channel_layout_default(&OutLayout, Channels);

		SwrContext *pSwr = nullptr;
		Ret = ::swr_alloc_set_opts2(
			&pSwr,
			&OutLayout, AV_SAMPLE_FMT_S16, SampleRate,
			&m_pFrame->ch_layout, static_cast<AVSampleFormat>(m_pFrame->format), SampleRate,
			0, nullptr);
		::av_channel_layout_uninit(&OutLayout);

		if ((Ret < 0) || (pSwr == nullptr) || (::swr_init(pSwr) < 0)) {
			if (pSwr != nullptr)
				::swr_free(&pSwr);
			return false;
		}

		m_pSwrContext = pSwr;
		m_SwrChannels = Channels;
	}

	const size_t RequiredSize =
		static_cast<size_t>(Channels) * sizeof(int16_t) * static_cast<size_t>(m_pFrame->nb_samples);
	if (m_PCMBuffer.GetBufferSize() < RequiredSize) {
		if (m_PCMBuffer.AllocateBuffer(RequiredSize) < RequiredSize)
			return false;
	}

	uint8_t *pOut = m_PCMBuffer.GetBuffer();
	const int Converted = ::swr_convert(
		m_pSwrContext,
		&pOut, m_pFrame->nb_samples,
		const_cast<const uint8_t **>(m_pFrame->extended_data), m_pFrame->nb_samples);
	if (Converted < 0)
		return false;

	m_AudioInfo.Frequency = SampleRate;
	m_AudioInfo.ChannelCount = Channels;
	m_AudioInfo.OriginalChannelCount = Channels;
	m_AudioInfo.DualMono = (Channels == 2) && (m_LastChannelConfig == 0);

	Info->pData = m_PCMBuffer.GetBuffer();
	Info->SampleCount = static_cast<size_t>(Converted);
	Info->Info = m_AudioInfo;
	Info->Discontinuity = m_DecodeError;

	return true;
}


#endif // LIBISDB_HAS_FFMPEG_AAC


} // namespace LibISDB::DirectShow
