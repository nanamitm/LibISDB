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
#include "LatmDownmixMatrix.hpp"
#include "../../../../Base/DebugDef.hpp"
#include <vector>
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
// FFmpeg is linked statically; libavutil's random seed uses BCryptGenRandom
#pragma comment(lib, "bcrypt.lib")


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
	, m_pSwrInputFormat(nullptr)
	, m_pSwrContext(nullptr)
	, m_DownmixChannels(0)
	, m_SwrDownmixChannels(0)
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

	constexpr size_t PCM_BUFFER_SIZE = 24 * sizeof(int16_t) * 4096;
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
	// ダウンミックスした場合の並びは LatmDownmix の出力スロットの順序で、
	// 下流のチャンネルマップと同じ並びになっているため並べ替えは不要。
	// 22.2ch (24ch) をそのまま出す場合も、放送由来の順序を保つために
	// 並べ替えは行わない。
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


bool AACDecoder_LATM::SetDownmixChannels(int Channels)
{
	if ((Channels != 0) && (Channels != 2) && (Channels != 6) && (Channels != 8))
		return false;

	if (m_DownmixChannels != Channels) {
		m_DownmixChannels = Channels;
		ResolveMatrix();
	}

	return true;
}


bool AACDecoder_LATM::SetDownmixPreset(const CharType *pFileName, const CharType *pPresetName)
{
	m_PresetFileName = (pFileName != nullptr) ? pFileName : LIBISDB_STR("");
	m_PresetName = (pPresetName != nullptr) ? pPresetName : LIBISDB_STR("");

	ResolveMatrix();

	return true;
}


void AACDecoder_LATM::ResolveMatrix()
{
	m_Matrix.reset();

	if (m_DownmixChannels == 0)
		return;

	// プリセット名の指定が無い場合、ダウンミックス先のチャンネル数から決める
	const CharType *pDefaultName =
		(m_DownmixChannels == 8) ? LIBISDB_STR("7.1ch") :
		(m_DownmixChannels == 6) ? LIBISDB_STR("5.1ch") : LIBISDB_STR("2ch");
	const CharType *pPresetName =
		!m_PresetName.empty() ? m_PresetName.c_str() : pDefaultName;

	LatmDownmix::Matrix Matrix;
	bool Resolved = LatmDownmix::ResolvePreset(
		!m_PresetFileName.empty() ? m_PresetFileName.c_str() : nullptr,
		pPresetName, &Matrix);

	// プリセットの出力チャンネル数が要求と食い違う場合は、要求に合う
	// 組み込み定義へ戻す
	if (Resolved && (Matrix.OutputChannels.size() != static_cast<size_t>(m_DownmixChannels))) {
		LIBISDB_TRACE(
			LIBISDB_STR("AACDecoder_LATM: プリセット '{}' は {} ch 用ではありません\n"),
			pPresetName, m_DownmixChannels);
		Resolved = (pPresetName != pDefaultName)
			&& LatmDownmix::GetBuiltinPreset(pDefaultName, &Matrix)
			&& (Matrix.OutputChannels.size() == static_cast<size_t>(m_DownmixChannels));
	}

	if (!Resolved) {
		// マトリクスが無い場合は libswresample の既定のダウンミックスに任せる
		LIBISDB_TRACE(
			LIBISDB_STR("AACDecoder_LATM: ダウンミックスのマトリクスを解決できません\n"));
		return;
	}

	m_Matrix = std::make_unique<LatmDownmix::Matrix>(std::move(Matrix));
}


bool AACDecoder_LATM::SetSwrMatrix(SwrContext *pSwr) const
{
	if (!m_Matrix)
		return false;

	const int InChannels = m_pFrame->ch_layout.nb_channels;
	const size_t OutChannels = m_Matrix->OutputChannels.size();
	if ((InChannels <= 0) || (OutChannels == 0))
		return false;

	// 出力ch×入力chの係数行列を組み立てる。入力に存在しないチャンネルの
	// 指定は無視する (5.1ch の素材に 22.2ch 用のプリセットを使った場合など)
	std::vector<double> Coefficients(OutChannels * static_cast<size_t>(InChannels), 0.0);

	for (size_t i = 0; i < OutChannels; i++) {
		for (const LatmDownmix::Route &Route : m_Matrix->OutputChannels[i].Routes) {
			const int Index = ::av_channel_layout_index_from_channel(
				&m_pFrame->ch_layout, Route.InputChannel);
			if (Index >= 0) {
				Coefficients[i * static_cast<size_t>(InChannels) + static_cast<size_t>(Index)] +=
					static_cast<double>(Route.Gain) * static_cast<double>(m_Matrix->OutputGain);
			}
		}
	}

	return ::swr_set_matrix(pSwr, Coefficients.data(), InChannels) >= 0;
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
	m_pSwrInputFormat = ::av_frame_alloc();
	if ((m_pPacket == nullptr) || (m_pFrame == nullptr) || (m_pSwrInputFormat == nullptr)) {
		CloseDecoder();
		return false;
	}

	return true;
}


void AACDecoder_LATM::CloseDecoder()
{
	::av_frame_free(&m_pSwrInputFormat);
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
	*pDataSize = 0;
	size_t Pos = 0;

	// Consume only through one frame so the caller can decode the rest of
	// this input immediately. Keep only an incomplete frame between calls.
	while (Pos < InputSize) {
		if (m_LOASBuffer.size() < LOAS_HEADER_SIZE)
			m_LOASBuffer.push_back(pData[Pos++]);

		const size_t Sync = FindLOASSync();
		if (Sync == std::numeric_limits<size_t>::max()) {
			if (m_LOASBuffer.size() > 1) {
				const uint8_t Last = m_LOASBuffer.back();
				m_LOASBuffer.clear();
				m_LOASBuffer.push_back(Last);
			}
			continue;
		}

		if (Sync > 0)
			m_LOASBuffer.erase(m_LOASBuffer.begin(), m_LOASBuffer.begin() + Sync);

		if (m_LOASBuffer.size() < LOAS_HEADER_SIZE)
			continue;

		const size_t PayloadSize =
			(static_cast<size_t>(m_LOASBuffer[1] & 0x1F) << 8) | m_LOASBuffer[2];

		if ((PayloadSize == 0) || (PayloadSize > LOAS_MAX_PAYLOAD_SIZE)) {
			// シンクワードと一致したが長さが不正、誤検出として 1 バイト進めて再同期
			m_LOASBuffer.erase(m_LOASBuffer.begin());
			continue;
		}

		const size_t FrameSize = PayloadSize + LOAS_HEADER_SIZE;
		const size_t CopySize = std::min(FrameSize - m_LOASBuffer.size(), InputSize - Pos);
		m_LOASBuffer.insert(m_LOASBuffer.end(), pData + Pos, pData + Pos + CopySize);
		Pos += CopySize;
		if (m_LOASBuffer.size() < FrameSize)
			break;

		// FFmpeg bitstream readers require zero padding after the packet.
		m_LOASBuffer.resize(FrameSize + AV_INPUT_BUFFER_PADDING_SIZE, 0);
		const bool OK = DecodePacket(m_LOASBuffer.data(), FrameSize, Info);
		m_LOASBuffer.clear();
		*pDataSize = Pos;

		return OK;
	}

	*pDataSize = Pos;
	return false;
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

	if ((m_pSwrContext == nullptr)
			|| (m_SwrDownmixChannels != m_DownmixChannels)
			|| (m_pSwrInputFormat->sample_rate != SampleRate)
			|| (m_pSwrInputFormat->format != m_pFrame->format)
			|| (::av_channel_layout_compare(&m_pSwrInputFormat->ch_layout, &m_pFrame->ch_layout) != 0)) {
		::swr_free(&m_pSwrContext);

		// ダウンミックスしない場合、出力のチャンネル順序はデコーダから
		// 渡された順序をそのまま保持する。ダウンミックスする場合の並びは
		// LatmDownmix のマトリクスの出力スロットと同じ順序になる
		AVChannelLayout OutLayout{};
		switch (m_DownmixChannels) {
		case 2:
			{
				const AVChannelLayout Layout = AV_CHANNEL_LAYOUT_STEREO;
				Ret = ::av_channel_layout_copy(&OutLayout, &Layout);
			}
			break;
		case 6:
			{
				const AVChannelLayout Layout = AV_CHANNEL_LAYOUT_5POINT1_BACK;
				Ret = ::av_channel_layout_copy(&OutLayout, &Layout);
			}
			break;
		case 8:
			{
				const AVChannelLayout Layout = AV_CHANNEL_LAYOUT_7POINT1;
				Ret = ::av_channel_layout_copy(&OutLayout, &Layout);
			}
			break;
		default:
			Ret = ::av_channel_layout_copy(&OutLayout, &m_pFrame->ch_layout);
			break;
		}
		if (Ret < 0)
			return false;

		SwrContext *pSwr = nullptr;
		Ret = ::swr_alloc_set_opts2(
			&pSwr,
			&OutLayout, AV_SAMPLE_FMT_S16, SampleRate,
			&m_pFrame->ch_layout, static_cast<AVSampleFormat>(m_pFrame->format), SampleRate,
			0, nullptr);

		// 係数行列は swr_init() より前に設定する必要がある。マトリクスが
		// 無い場合は libswresample の既定のダウンミックスがそのまま使われる
		if ((Ret >= 0) && (pSwr != nullptr) && (m_DownmixChannels != 0))
			SetSwrMatrix(pSwr);

		if ((Ret < 0) || (pSwr == nullptr) || (::swr_init(pSwr) < 0)) {
			if (pSwr != nullptr)
				::swr_free(&pSwr);
			return false;
		}

		::av_channel_layout_uninit(&m_pSwrInputFormat->ch_layout);
		if (::av_channel_layout_copy(&m_pSwrInputFormat->ch_layout, &m_pFrame->ch_layout) < 0) {
			::swr_free(&pSwr);
			return false;
		}
		m_pSwrInputFormat->sample_rate = SampleRate;
		m_pSwrInputFormat->format = m_pFrame->format;
		m_pSwrContext = pSwr;
		m_SwrDownmixChannels = m_DownmixChannels;
	}

	const int OutChannels = (m_DownmixChannels != 0) ? m_DownmixChannels : SrcChannels;
	const size_t RequiredSize =
		static_cast<size_t>(OutChannels) * sizeof(int16_t) * static_cast<size_t>(m_pFrame->nb_samples);
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
	m_AudioInfo.ChannelCount = OutChannels;
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
