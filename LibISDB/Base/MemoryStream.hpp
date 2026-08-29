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
 @file   MemoryStream.hpp
 @brief  メモリストリーム
 @author DBCTRADO
*/


#ifndef LIBISDB_MEMORY_STREAM_H
#define LIBISDB_MEMORY_STREAM_H


#include "Stream.hpp"
#include <vector>


namespace LibISDB
{

	/** メモリストリームクラス

	メモリ上のバッファを Stream として扱う。
	書き出し時はバッファが自動的に拡張される。
	*/
	class MemoryStream
		: public Stream
	{
	public:
		typedef std::vector<uint8_t> BufferType;

		MemoryStream() = default;
		MemoryStream(const void *pData, size_t Size);
		explicit MemoryStream(BufferType &&Buffer);

	// Stream
		bool Close() override;
		bool IsOpen() const override;

		size_t Read(void *pBuff, size_t Size) override;
		size_t Write(const void *pBuff, size_t Size) override;
		bool Flush() override;

		SizeType GetSize() override;
		OffsetType GetPos() override;
		bool SetPos(OffsetType Pos, SetPosType Type = SetPosType::Begin) override;

		bool IsEnd() const override;

	// MemoryStream
		/** バッファの内容を設定する(位置は先頭に戻る) */
		bool SetData(const void *pData, size_t Size);
		/** バッファを設定する(位置は先頭に戻る) */
		void SetBuffer(BufferType &&Buffer);
		/** バッファを空にする */
		void Clear();
		/** バッファを取得する */
		BufferType & GetBuffer() noexcept { return m_Buffer; }
		const BufferType & GetBuffer() const noexcept { return m_Buffer; }
		/** バッファを取り出す(元のバッファは空になる) */
		BufferType DetachBuffer();
		/** データの先頭を取得する */
		const uint8_t * GetData() const noexcept { return m_Buffer.data(); }
		/** データのサイズを取得する */
		size_t GetDataSize() const noexcept { return m_Buffer.size(); }
		/** バッファを予約する */
		bool Reserve(size_t Size);
		/** バッファの最大サイズを設定する

		書き出しでバッファがこのサイズを超える場合、書き出しが失敗する。
		信頼できない入力を扱う際のメモリ枯渇対策に使用する。
		0 を指定すると無制限(既定)。
		*/
		void SetMaxSize(size_t Size) noexcept { m_MaxSize = Size; }
		size_t GetMaxSize() const noexcept { return m_MaxSize; }

	private:
		BufferType m_Buffer;
		size_t m_Pos = 0;
		size_t m_MaxSize = 0;
	};

} // namespace LibISDB


#endif // ifndef LIBISDB_MEMORY_STREAM_H
