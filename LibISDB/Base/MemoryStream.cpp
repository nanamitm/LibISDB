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
 @file   MemoryStream.cpp
 @brief  メモリストリーム
 @author DBCTRADO
*/


#include "../LibISDBPrivate.hpp"
#include "MemoryStream.hpp"
#include <cstring>
#include <new>
#include "DebugDef.hpp"


namespace LibISDB
{


MemoryStream::MemoryStream(const void *pData, size_t Size)
{
	SetData(pData, Size);
}


MemoryStream::MemoryStream(BufferType &&Buffer)
	: m_Buffer(std::move(Buffer))
{
}


bool MemoryStream::Close()
{
	Clear();

	return true;
}


bool MemoryStream::IsOpen() const
{
	return true;
}


size_t MemoryStream::Read(void *pBuff, size_t Size)
{
	if ((pBuff == nullptr) || (Size == 0))
		return 0;

	if (m_Pos >= m_Buffer.size())
		return 0;

	const size_t Remain = m_Buffer.size() - m_Pos;
	const size_t ReadSize = std::min(Size, Remain);

	std::memcpy(pBuff, m_Buffer.data() + m_Pos, ReadSize);
	m_Pos += ReadSize;

	return ReadSize;
}


size_t MemoryStream::Write(const void *pBuff, size_t Size)
{
	if ((pBuff == nullptr) || (Size == 0))
		return 0;

	const size_t End = m_Pos + Size;
	if (End < m_Pos)	// オーバーフロー
		return 0;
	if ((m_MaxSize != 0) && (End > m_MaxSize))
		return 0;

	if (End > m_Buffer.size()) {
		try {
			m_Buffer.resize(End);
		} catch (const std::bad_alloc &) {
			return 0;
		}
	}

	std::memcpy(m_Buffer.data() + m_Pos, pBuff, Size);
	m_Pos = End;

	return Size;
}


bool MemoryStream::Flush()
{
	return true;
}


MemoryStream::SizeType MemoryStream::GetSize()
{
	return static_cast<SizeType>(m_Buffer.size());
}


MemoryStream::OffsetType MemoryStream::GetPos()
{
	return static_cast<OffsetType>(m_Pos);
}


bool MemoryStream::SetPos(OffsetType Pos, SetPosType Type)
{
	OffsetType NewPos;

	switch (Type) {
	case SetPosType::Begin:
		NewPos = Pos;
		break;
	case SetPosType::Current:
		NewPos = static_cast<OffsetType>(m_Pos) + Pos;
		break;
	case SetPosType::End:
		NewPos = static_cast<OffsetType>(m_Buffer.size()) + Pos;
		break;
	default:
		return false;
	}

	// バッファの終端を超えるシークは失敗させる
	// (壊れたデータでチャンクをスキップした際に検出できるようにするため)
	if ((NewPos < 0) || (static_cast<SizeType>(NewPos) > m_Buffer.size()))
		return false;

	m_Pos = static_cast<size_t>(NewPos);

	return true;
}


bool MemoryStream::IsEnd() const
{
	return m_Pos >= m_Buffer.size();
}


bool MemoryStream::SetData(const void *pData, size_t Size)
{
	if ((pData == nullptr) && (Size != 0))
		return false;
	if ((m_MaxSize != 0) && (Size > m_MaxSize))
		return false;

	try {
		m_Buffer.assign(
			static_cast<const uint8_t *>(pData),
			static_cast<const uint8_t *>(pData) + Size);
	} catch (const std::bad_alloc &) {
		return false;
	}

	m_Pos = 0;

	return true;
}


void MemoryStream::SetBuffer(BufferType &&Buffer)
{
	m_Buffer = std::move(Buffer);
	m_Pos = 0;
}


void MemoryStream::Clear()
{
	m_Buffer.clear();
	m_Buffer.shrink_to_fit();
	m_Pos = 0;
}


MemoryStream::BufferType MemoryStream::DetachBuffer()
{
	BufferType Buffer(std::move(m_Buffer));

	m_Buffer.clear();
	m_Pos = 0;

	return Buffer;
}


bool MemoryStream::Reserve(size_t Size)
{
	if ((m_MaxSize != 0) && (Size > m_MaxSize))
		return false;

	try {
		m_Buffer.reserve(Size);
	} catch (const std::bad_alloc &) {
		return false;
	}

	return true;
}


}	// namespace LibISDB
