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
 @file   AACDecoder_LATM.cpp
 @brief  AAC (LATM/LOAS) デコーダ (FFmpeg)
 @author DBCTRADO
*/


#include "../../../../LibISDBPrivate.hpp"


#ifdef LIBISDB_HAS_FFMPEG_AAC


#include "AACDecoder_LATM.hpp"
#include "../../../../Base/DebugDef.hpp"
#include <cstdio>
#include <limits>

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


namespace {

// LOAS (Low Overhead Audio Stream) ヘッダ: 11bit シンクワード (0x2B7) + 13bit
// audioMuxLengthBytes。ADTS と異なり PES ペイロード境界とフレーム境界が一致
// しないことがあるため、ADTSParser と同様にバイト列中からシンクを探して
// 再同期する必要がある。
constexpr size_t LOAS_HEADER_SIZE = 3;
constexpr size_t LOAS_MAX_PAYLOAD_SIZE = 8191;

} // namespace


void AACDecoder_LATM::GetVersion(std::string *pVersion)
{
	char Buf[64];
	const unsigned int Version = ::avcodec_version();
	std::snprintf(
		Buf, sizeof(Buf), "FFmpeg libavcodec %d.%d.%d (LATM)",
		AV_VERSION_MAJOR(Version), AV_VERSION_MINOR(Version), AV_VERSION_MICRO(Version));
	*pVersion = Buf;
}


AACDecoder_LATM::AACDecoder_LATM() noexcept
	: m_pCodecContext(nullptr)
	, m_pPacket(nullptr)
	, m_pFrame(nullptr)
	, m_pSwrContext(nullptr)
{
}


AACDecoder_LATM::~AACDecoder_LATM()
{
	Close();
}


bool AACDecoder_LATM::Open()
{
	if (!OpenDecoder())
		return false;

	ClearAudioInfo();
	m_LOASBuffer.clear();

	constexpr size_t PCM_BUFFER_SIZE = 2 * sizeof(int16_t) * 4096;
	if (m_PCMBuffer.AllocateBuffer(PCM_BUFFER_SIZE) < PCM_BUFFER_SIZE) {
		Close();
		return false;
	}

	return true;
}


void AACDecoder_LATM::Close()
{
	CloseDecoder();
	m_LOASBuffer.clear();
	m_PCMBuffer.FreeBuffer();
}


bool AACDecoder_LATM::IsOpened() const
{
	return m_pCodecContext != nullptr;
}


bool AACDecoder_LATM::Reset()
{
	if (!OpenDecoder())
		return false;

	ClearAudioInfo();
	m_LOASBuffer.clear();

	return true;
}


bool AACDecoder_LATM::GetChannelMap(int Channels, int *pMap) const
{
	// 元のチャンネル数 (22.2ch であれば 24ch) に関わらず、常にステレオへ
	// ダウンミックスしてから返すため、ここで考慮する必要があるのは
	// 2ch のマッピングだけで良い。
	if (Channels != 2)
		return false;

	pMap[CHANNEL_2_L] = 0;
	pMap[CHANNEL_2_R] = 1;

	return true;
}


bool AACDecoder_LATM::GetDownmixInfo(ReturnArg<DownmixInfo> Info) const
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


bool AACDecoder_LATM::OpenDecoder()
{
	CloseDecoder();

	const AVCodec *pCodec = ::avcodec_find_decoder(AV_CODEC_ID_AAC_LATM);
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

	return true;
}


void AACDecoder_LATM::CloseDecoder()
{
	if (m_pSwrContext != nullptr) {
		::swr_free(&m_pSwrContext);
		m_pSwrContext = nullptr;
	}

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


size_t AACDecoder_LATM::FindLOASSync() const
{
	for (size_t i = 0; i + 1 < m_LOASBuffer.size(); i++) {
		if ((m_LOASBuffer[i] == 0x56) && ((m_LOASBuffer[i + 1] & 0xE0) == 0xE0))
			return i;
	}

	return std::numeric_limits<size_t>::max();
}


bool AACDecoder_LATM::Decode(const uint8_t *pData, size_t *pDataSize, ReturnArg<DecodeFrameInfo> Info)
{
	if (!IsOpened() || (pData == nullptr) || (pDataSize == nullptr))
		return false;

	const size_t InputSize = *pDataSize;

	m_LOASBuffer.insert(m_LOASBuffer.end(), pData, pData + InputSize);

	// PES ペイロード境界と LOAS フレーム境界は一致しないことがあるため、渡された
	// 分は (内部バッファへ取り込んだ時点で) 常に消費済みとして扱う。複数フレーム分
	// が一度に届いた場合、残りは次回以降の呼び出しで順次デコードされる。
	*pDataSize = InputSize;

	for (;;) {
		const size_t Sync = FindLOASSync();
		if (Sync == std::numeric_limits<size_t>::max()) {
			if (m_LOASBuffer.size() > 1) {
				const uint8_t Last = m_LOASBuffer.back();
				m_LOASBuffer.clear();
				m_LOASBuffer.push_back(Last);
			}
			return false;
		}

		if (Sync > 0)
			m_LOASBuffer.erase(m_LOASBuffer.begin(), m_LOASBuffer.begin() + Sync);

		if (m_LOASBuffer.size() < LOAS_HEADER_SIZE)
			return false;

		const size_t PayloadSize =
			(static_cast<size_t>(m_LOASBuffer[1] & 0x1F) << 8) | m_LOASBuffer[2];

		if ((PayloadSize == 0) || (PayloadSize > LOAS_MAX_PAYLOAD_SIZE)) {
			// シンクワードと一致したが長さが不正、誤検出として 1 バイト進めて再同期
			m_LOASBuffer.erase(m_LOASBuffer.begin());
			continue;
		}

		const size_t FrameSize = PayloadSize + LOAS_HEADER_SIZE;
		if (m_LOASBuffer.size() < FrameSize)
			return false;

		const bool OK = DecodePacket(m_LOASBuffer.data(), FrameSize, Info);
		m_LOASBuffer.erase(m_LOASBuffer.begin(), m_LOASBuffer.begin() + FrameSize);

		return OK;
	}
}


bool AACDecoder_LATM::DecodePacket(const uint8_t *pData, size_t Size, ReturnArg<DecodeFrameInfo> Info)
{
	::av_packet_unref(m_pPacket);
	m_pPacket->data = const_cast<uint8_t *>(pData);
	m_pPacket->size = static_cast<int>(Size);

	int Ret = ::avcodec_send_packet(m_pCodecContext, m_pPacket);
	if (Ret < 0) {
		LIBISDB_TRACE(LIBISDB_STR("AACDecoder_LATM: avcodec_send_packet() error {}\n"), Ret);
		return false;
	}

	Ret = ::avcodec_receive_frame(m_pCodecContext, m_pFrame);
	if (Ret < 0) {
		// AVERROR(EAGAIN): まだ出力がない (初回フレーム等)。次回の呼び出しで揃う。
		return false;
	}

	const int SrcChannels = m_pFrame->ch_layout.nb_channels;
	const int SampleRate = m_pFrame->sample_rate;

	if ((SrcChannels <= 0) || (SampleRate <= 0))
		return false;

	if (m_pSwrContext == nullptr) {
		AVChannelLayout OutLayout = AV_CHANNEL_LAYOUT_STEREO;

		SwrContext *pSwr = nullptr;
		Ret = ::swr_alloc_set_opts2(
			&pSwr,
			&OutLayout, AV_SAMPLE_FMT_S16, SampleRate,
			&m_pFrame->ch_layout, static_cast<AVSampleFormat>(m_pFrame->format), SampleRate,
			0, nullptr);

		if ((Ret < 0) || (pSwr == nullptr) || (::swr_init(pSwr) < 0)) {
			if (pSwr != nullptr)
				::swr_free(&pSwr);
			return false;
		}

		m_pSwrContext = pSwr;
	}

	constexpr int OUT_CHANNELS = 2;
	const size_t RequiredSize =
		static_cast<size_t>(OUT_CHANNELS) * sizeof(int16_t) * static_cast<size_t>(m_pFrame->nb_samples);
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
	m_AudioInfo.ChannelCount = OUT_CHANNELS;
	m_AudioInfo.OriginalChannelCount = SrcChannels;
	m_AudioInfo.DualMono = false;

	Info->pData = m_PCMBuffer.GetBuffer();
	Info->SampleCount = static_cast<size_t>(Converted);
	Info->Info = m_AudioInfo;
	Info->Discontinuity = false;

	return true;
}


#endif // LIBISDB_HAS_FFMPEG_AAC


} // namespace LibISDB::DirectShow
