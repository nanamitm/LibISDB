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
#include <memory>


struct AVCodecContext;
struct AVPacket;
struct AVFrame;
struct SwrContext;


namespace LibISDB::DirectShow
{

	namespace LatmDownmix
	{
		struct Matrix;
	}


	/**
	 @brief  LATM/LOAS 伝送された AAC (ISO/IEC 14496-3, stream_type 0x11) 用デコーダ

	 22.2ch 等の多チャンネル音声は LATM フレーミングで伝送される。デコード結果は
	 放送由来のチャンネル順序を保ったインターリーブ PCM として返し、チャンネルの
	 解釈は下流のフィルタに委ねる。

	 ただし 24ch をそのまま扱えるのは、それを解釈できる出力先が繋がっている場合に
	 限られる。通常の音声レンダラへ出力する場合は、SetDownmixChannels() で
	 ダウンミックス先のチャンネル数を指定する。
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
		bool SetDownmixChannels(int Channels) override;

	// AACDecoder_LATM
		/**
		 @brief  ダウンミックスに使うプリセットを指定する

		 @param[in] pFileName   プリセットを定義した ini ファイルのパス。
		                        nullptr や空文字列なら組み込み定義のみを使う
		 @param[in] pPresetName 使用するプリセット名。nullptr や空文字列なら
		                        ダウンミックス先のチャンネル数から決まる既定の
		                        名前 ("7.1ch" / "5.1ch" / "2ch") を使う
		*/
		bool SetDownmixPreset(const CharType *pFileName, const CharType *pPresetName);

		static void GetVersion(std::string *pVersion);

	private:
		bool OpenDecoder();
		void CloseDecoder();
		size_t FindLOASSync() const;
		bool DecodePacket(const uint8_t *pData, size_t Size, ReturnArg<DecodeFrameInfo> Info);
		void ResolveMatrix();
		bool SetSwrMatrix(SwrContext *pSwr) const;

		AVCodecContext *m_pCodecContext;
		AVPacket *m_pPacket;
		AVFrame *m_pFrame;
		AVFrame *m_pSwrInputFormat;
		SwrContext *m_pSwrContext;
		/** ダウンミックス先のチャンネル数。0 ならダウンミックスしない */
		int m_DownmixChannels;
		/** m_pSwrContext を構築した時点の m_DownmixChannels */
		int m_SwrDownmixChannels;
		/** ダウンミックスに使うチャンネルマトリクス。未解決なら空 */
		std::unique_ptr<LatmDownmix::Matrix> m_Matrix;
		String m_PresetFileName;
		String m_PresetName;
		std::vector<uint8_t> m_LOASBuffer;
		DataBuffer m_PCMBuffer;
	};

} // namespace LibISDB::DirectShow


#endif // LIBISDB_HAS_FFMPEG_AAC


#endif // ifndef LIBISDB_AAC_DECODER_LATM_H
