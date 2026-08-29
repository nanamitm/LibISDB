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
 @file   EPGDataSerializer.hpp
 @brief  番組情報のサービス単位のシリアライズ
 @author DBCTRADO
*/


#ifndef LIBISDB_EPG_DATA_SERIALIZER_H
#define LIBISDB_EPG_DATA_SERIALIZER_H


#include "EPGDatabase.hpp"
#include "../Base/Stream.hpp"


namespace LibISDB
{

	/** 番組情報シリアライズクラス

	EPGDatabase のサービス1個分の番組情報を、環境に依存しないバイト列に相互変換する。

	EPGDataFile と異なり、以下の点で環境非依存になっている。

	- 文字列を CharType ではなく UTF-16LE 固定で格納する
	  (EPGDataFile は sizeof(CharType) 分をそのまま書き出すため、
	   Windows(wchar_t = 2 バイト)と他の環境とで互換性がない)
	- 整数をリトルエンディアン固定で格納する
	- サービス単位で完結しており、差分の送受信に使用できる

	そのため、異なる OS 間で番組情報をやり取りする用途に使用できる。
	*/
	class EPGDataSerializer
	{
	public:
		/** フォーマットのバージョン */
		static constexpr uint32_t FormatVersion = 1;

		/** ヘッダのサイズ */
		static constexpr size_t HeaderSize = 32;

		/** サービスのヘッダ情報 */
		struct ServiceHeader {
			uint32_t Version = FormatVersion;      /**< フォーマットのバージョン */
			uint16_t NetworkID = NETWORK_ID_INVALID;
			uint16_t TransportStreamID = TRANSPORT_STREAM_ID_INVALID;
			uint16_t ServiceID = SERVICE_ID_INVALID;
			uint32_t EventCount = 0;               /**< 番組の数 */
			uint64_t UpdatedTime = 0;              /**< 番組の UpdatedTime の最大値(TOT 基準) */

			EPGDatabase::ServiceInfo GetServiceInfo() const noexcept
			{
				return EPGDatabase::ServiceInfo(NetworkID, TransportStreamID, ServiceID);
			}
		};

		/** サービス1個分の番組情報を書き出す

		@param[in]  Database          読み出し元のデータベース
		@param[in]  NetworkID         ネットワーク ID
		@param[in]  TransportStreamID トランスポートストリーム ID
		@param[in]  ServiceID         サービス ID
		@param[out] DataStream        書き出し先(現在位置から書き出される)
		@param[out] pHeader           書き出したヘッダ情報(nullptr 可)

		@return 書き出せた場合 true
		*/
		static bool SerializeService(
			const EPGDatabase &Database,
			uint16_t NetworkID, uint16_t TransportStreamID, uint16_t ServiceID,
			Stream &DataStream, ServiceHeader *pHeader = nullptr);

		/** サービス1個分の番組情報を読み込む

		Database の同一サービスの情報は置き換えられる。
		マージする場合は一時的な EPGDatabase に読み込んでから
		EPGDatabase::MergeService() を使用する。

		@param[in]  DataStream 読み込み元(現在位置から読み込まれる)
		@param[out] Database 格納先のデータベース
		@param[out] pHeader  読み込んだヘッダ情報(nullptr 可)

		@return 読み込めた場合 true
		*/
		static bool DeserializeService(
			Stream &DataStream, EPGDatabase &Database, ServiceHeader *pHeader = nullptr);

		/** ヘッダのみを取得する

		バージョンの比較のためにデータ全体を解析せずに済ませる用途に使用する。

		@param[in]  pData   データの先頭
		@param[in]  Size    データのサイズ
		@param[out] pHeader ヘッダ情報

		@return 取得できた場合 true
		*/
		static bool PeekHeader(const void *pData, size_t Size, ServiceHeader *pHeader);
	};

} // namespace LibISDB


#endif // ifndef LIBISDB_EPG_DATA_SERIALIZER_H
