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
 @file   EPGDataSerializer.cpp
 @brief  番組情報のサービス単位のシリアライズ
 @author DBCTRADO
*/


#include "../LibISDBPrivate.hpp"
#include "EPGDataSerializer.hpp"
#include <cstring>
#include <new>
#include "../Base/DebugDef.hpp"


namespace LibISDB
{

namespace
{


/*
	シリアライズされたサービスの構造

	Header (32 バイト固定)
		char[8]  Type = "EPG-SVC1"
		uint32   Version
		uint16   NetworkID
		uint16   TransportStreamID
		uint16   ServiceID
		uint16   Reserved
		uint32   EventCount
		uint64   UpdatedTime

	以降、Tag::End が現れるまでチャンクが続く。

	Chunk
		uint8    Tag
		uint32   Size    (ボディのバイト数)
		uint8[Size] ボディ

	番組は Tag::Event チャンクで始まり、Tag::EventEnd チャンクで終わる。
	その間に番組の内容を表すチャンクが並ぶ。
	未知の Tag は Size 分読み飛ばせるため、前方互換性がある。

	文字列は UTF-16LE で格納される。
		uint16   Length  (UTF-16 のコードユニット数)
		uint16[Length] 文字列
*/


namespace Tag {
	constexpr uint8_t End               = 0x01_u8;
	constexpr uint8_t Event             = 0x04_u8;
	constexpr uint8_t EventEnd          = 0x05_u8;
	constexpr uint8_t EventAudio        = 0x06_u8;
	constexpr uint8_t EventVideo        = 0x07_u8;
	constexpr uint8_t EventGenre        = 0x08_u8;
	constexpr uint8_t EventName         = 0x09_u8;
	constexpr uint8_t EventText         = 0x0A_u8;
	constexpr uint8_t EventExtendedText = 0x0B_u8;
	constexpr uint8_t EventGroup        = 0x0C_u8;
	constexpr uint8_t EventCommon       = 0x0D_u8;
}


namespace EventFlag {
	constexpr uint16_t RunningStatus = 0x0007_u16;
	constexpr uint16_t FreeCAMode    = 0x0008_u16;
	constexpr uint16_t Basic         = 0x0010_u16;
	constexpr uint16_t Extended      = 0x0020_u16;
	constexpr uint16_t Present       = 0x0040_u16;
	constexpr uint16_t Following     = 0x0080_u16;
	constexpr uint16_t CommonEvent   = 0x0100_u16;
}


const char FormatType[8] = {'E', 'P', 'G', '-', 'S', 'V', 'C', '1'};

constexpr size_t CHUNK_HEADER_SIZE = 1 + 4;

// 壊れたデータや悪意のあるデータでの過大な確保を防ぐための上限
constexpr uint32_t MAX_EVENT_COUNT = 100000;
constexpr uint16_t MAX_STRING_LENGTH = 8192;
constexpr uint8_t MAX_EXTENDED_TEXT_COUNT = 64;
constexpr uint8_t MAX_NIBBLE_COUNT = 7;


// UTF-16 への変換

typedef std::vector<uint16_t> UTF16String;


#if defined(LIBISDB_WCHAR) && (WCHAR_MAX > 0xFFFF)

// wchar_t が UTF-32 の環境

void ToUTF16(const String &Src, UTF16String *pDst)
{
	pDst->clear();
	pDst->reserve(Src.length());

	for (const CharType c : Src) {
		const uint32_t Code = static_cast<uint32_t>(c);

		if (Code < 0x10000_u32) {
			pDst->push_back(static_cast<uint16_t>(Code));
		} else if (Code <= 0x10FFFF_u32) {
			const uint32_t v = Code - 0x10000_u32;
			pDst->push_back(static_cast<uint16_t>(0xD800_u32 | (v >> 10)));
			pDst->push_back(static_cast<uint16_t>(0xDC00_u32 | (v & 0x3FF_u32)));
		} else {
			pDst->push_back(0xFFFD_u16);
		}
	}
}

void FromUTF16(const uint16_t *pSrc, size_t Length, String *pDst)
{
	pDst->clear();
	pDst->reserve(Length);

	for (size_t i = 0; i < Length; i++) {
		uint32_t Code = pSrc[i];

		if ((Code >= 0xD800_u32) && (Code < 0xDC00_u32) && (i + 1 < Length)
				&& (pSrc[i + 1] >= 0xDC00) && (pSrc[i + 1] < 0xE000)) {
			Code = 0x10000_u32 + ((Code - 0xD800_u32) << 10) + (pSrc[i + 1] - 0xDC00_u32);
			i++;
		}

		pDst->push_back(static_cast<CharType>(Code));
	}
}

#elif defined(LIBISDB_WCHAR)

// wchar_t が UTF-16 の環境(Windows)

void ToUTF16(const String &Src, UTF16String *pDst)
{
	pDst->assign(
		reinterpret_cast<const uint16_t *>(Src.data()),
		reinterpret_cast<const uint16_t *>(Src.data()) + Src.length());
}

void FromUTF16(const uint16_t *pSrc, size_t Length, String *pDst)
{
	pDst->assign(reinterpret_cast<const CharType *>(pSrc), Length);
}

#else

// CharType が char (UTF-8) の環境

void ToUTF16(const String &Src, UTF16String *pDst)
{
	pDst->clear();
	pDst->reserve(Src.length());

	const uint8_t *p = reinterpret_cast<const uint8_t *>(Src.data());
	const uint8_t *pEnd = p + Src.length();

	while (p < pEnd) {
		uint32_t Code;
		int Following;

		if (*p < 0x80_u8) {
			Code = *p;
			Following = 0;
		} else if ((*p & 0xE0_u8) == 0xC0_u8) {
			Code = *p & 0x1F_u8;
			Following = 1;
		} else if ((*p & 0xF0_u8) == 0xE0_u8) {
			Code = *p & 0x0F_u8;
			Following = 2;
		} else if ((*p & 0xF8_u8) == 0xF0_u8) {
			Code = *p & 0x07_u8;
			Following = 3;
		} else {
			p++;
			pDst->push_back(0xFFFD_u16);
			continue;
		}
		p++;

		bool Valid = true;
		for (int i = 0; i < Following; i++) {
			if ((p >= pEnd) || ((*p & 0xC0_u8) != 0x80_u8)) {
				Valid = false;
				break;
			}
			Code = (Code << 6) | (*p & 0x3F_u8);
			p++;
		}
		if (!Valid || (Code > 0x10FFFF_u32)) {
			pDst->push_back(0xFFFD_u16);
			continue;
		}

		if (Code < 0x10000_u32) {
			pDst->push_back(static_cast<uint16_t>(Code));
		} else {
			const uint32_t v = Code - 0x10000_u32;
			pDst->push_back(static_cast<uint16_t>(0xD800_u32 | (v >> 10)));
			pDst->push_back(static_cast<uint16_t>(0xDC00_u32 | (v & 0x3FF_u32)));
		}
	}
}

void FromUTF16(const uint16_t *pSrc, size_t Length, String *pDst)
{
	pDst->clear();
	pDst->reserve(Length);

	for (size_t i = 0; i < Length; i++) {
		uint32_t Code = pSrc[i];

		if ((Code >= 0xD800_u32) && (Code < 0xDC00_u32) && (i + 1 < Length)
				&& (pSrc[i + 1] >= 0xDC00) && (pSrc[i + 1] < 0xE000)) {
			Code = 0x10000_u32 + ((Code - 0xD800_u32) << 10) + (pSrc[i + 1] - 0xDC00_u32);
			i++;
		}

		if (Code < 0x80_u32) {
			pDst->push_back(static_cast<CharType>(Code));
		} else if (Code < 0x800_u32) {
			pDst->push_back(static_cast<CharType>(0xC0_u32 | (Code >> 6)));
			pDst->push_back(static_cast<CharType>(0x80_u32 | (Code & 0x3F_u32)));
		} else if (Code < 0x10000_u32) {
			pDst->push_back(static_cast<CharType>(0xE0_u32 | (Code >> 12)));
			pDst->push_back(static_cast<CharType>(0x80_u32 | ((Code >> 6) & 0x3F_u32)));
			pDst->push_back(static_cast<CharType>(0x80_u32 | (Code & 0x3F_u32)));
		} else {
			pDst->push_back(static_cast<CharType>(0xF0_u32 | (Code >> 18)));
			pDst->push_back(static_cast<CharType>(0x80_u32 | ((Code >> 12) & 0x3F_u32)));
			pDst->push_back(static_cast<CharType>(0x80_u32 | ((Code >> 6) & 0x3F_u32)));
			pDst->push_back(static_cast<CharType>(0x80_u32 | (Code & 0x3F_u32)));
		}
	}
}

#endif


// 書き出し

class Writer
{
public:
	Writer(Stream &Target) : m_Stream(Target) {}

	bool IsOK() const noexcept { return m_OK; }

	void Bytes(const void *pData, size_t Size)
	{
		if (m_OK && (Size > 0)) {
			if (m_Stream.Write(pData, Size) != Size)
				m_OK = false;
		}
	}

	void U8(uint8_t Value) { Bytes(&Value, 1); }

	void U16(uint16_t Value)
	{
		const uint8_t Data[2] = {
			static_cast<uint8_t>(Value),
			static_cast<uint8_t>(Value >> 8),
		};
		Bytes(Data, 2);
	}

	void U32(uint32_t Value)
	{
		const uint8_t Data[4] = {
			static_cast<uint8_t>(Value),
			static_cast<uint8_t>(Value >> 8),
			static_cast<uint8_t>(Value >> 16),
			static_cast<uint8_t>(Value >> 24),
		};
		Bytes(Data, 4);
	}

	void U64(uint64_t Value)
	{
		U32(static_cast<uint32_t>(Value));
		U32(static_cast<uint32_t>(Value >> 32));
	}

	void Str(const String &Value)
	{
		UTF16String Buffer;

		ToUTF16(Value, &Buffer);

		if (Buffer.size() > MAX_STRING_LENGTH)
			Buffer.resize(MAX_STRING_LENGTH);

		U16(static_cast<uint16_t>(Buffer.size()));
		for (const uint16_t c : Buffer)
			U16(c);
	}

	void Time(const DateTime &Value)
	{
		U16(static_cast<uint16_t>(Value.Year));
		U8(static_cast<uint8_t>(Value.Month));
		U8(static_cast<uint8_t>(Value.Day));
		U8(static_cast<uint8_t>(Value.DayOfWeek));
		U8(static_cast<uint8_t>(Value.Hour));
		U8(static_cast<uint8_t>(Value.Minute));
		U8(static_cast<uint8_t>(Value.Second));
	}

	/** チャンクを開始する(サイズは後で埋める) */
	Stream::OffsetType BeginChunk(uint8_t ChunkTag)
	{
		U8(ChunkTag);
		const Stream::OffsetType SizePos = m_Stream.GetPos();
		U32(0);
		return SizePos;
	}

	/** チャンクを終了し、サイズを埋める */
	void EndChunk(Stream::OffsetType SizePos)
	{
		if (!m_OK)
			return;

		const Stream::OffsetType CurPos = m_Stream.GetPos();
		const Stream::OffsetType Size = CurPos - (SizePos + 4);

		if ((Size < 0) || (Size > 0xFFFFFFFFLL)
				|| !m_Stream.SetPos(SizePos, Stream::SetPosType::Begin)) {
			m_OK = false;
			return;
		}

		U32(static_cast<uint32_t>(Size));

		if (!m_Stream.SetPos(CurPos, Stream::SetPosType::Begin))
			m_OK = false;
	}

	/** 中身のないチャンクを書き出す */
	void EmptyChunk(uint8_t ChunkTag)
	{
		U8(ChunkTag);
		U32(0);
	}

private:
	Stream &m_Stream;
	bool m_OK = true;
};


// 読み込み

class Reader
{
public:
	Reader(Stream &Source) : m_Stream(Source) {}

	bool IsOK() const noexcept { return m_OK; }
	void SetError() noexcept { m_OK = false; }

	/** チャンクのボディの範囲を設定する */
	void BeginChunk(uint32_t Size) noexcept { m_Remain = Size; }

	/** チャンクのボディの残りを読み飛ばす */
	void EndChunk()
	{
		if (m_OK && (m_Remain > 0)) {
			if (!m_Stream.SetPos(static_cast<Stream::OffsetType>(m_Remain), Stream::SetPosType::Current))
				m_OK = false;
			m_Remain = 0;
		}
	}

	bool Bytes(void *pData, size_t Size)
	{
		if (!m_OK)
			return false;
		if (Size > m_Remain) {
			m_OK = false;
			return false;
		}
		if (m_Stream.Read(pData, Size) != Size) {
			m_OK = false;
			return false;
		}
		m_Remain -= static_cast<uint32_t>(Size);
		return true;
	}

	uint8_t U8()
	{
		uint8_t Data = 0;
		Bytes(&Data, 1);
		return Data;
	}

	uint16_t U16()
	{
		uint8_t Data[2] = {};
		Bytes(Data, 2);
		return static_cast<uint16_t>(Data[0] | (Data[1] << 8));
	}

	uint32_t U32()
	{
		uint8_t Data[4] = {};
		Bytes(Data, 4);
		return static_cast<uint32_t>(Data[0])
			| (static_cast<uint32_t>(Data[1]) << 8)
			| (static_cast<uint32_t>(Data[2]) << 16)
			| (static_cast<uint32_t>(Data[3]) << 24);
	}

	uint64_t U64()
	{
		const uint32_t Low = U32();
		const uint32_t High = U32();
		return static_cast<uint64_t>(Low) | (static_cast<uint64_t>(High) << 32);
	}

	void Str(String *pValue)
	{
		const uint16_t Length = U16();

		if (!m_OK)
			return;
		if (Length > MAX_STRING_LENGTH) {
			m_OK = false;
			return;
		}

		if (Length == 0) {
			pValue->clear();
			return;
		}

		try {
			UTF16String Buffer(Length);

			for (uint16_t i = 0; i < Length; i++)
				Buffer[i] = U16();
			if (!m_OK)
				return;

			FromUTF16(Buffer.data(), Buffer.size(), pValue);
		} catch (const std::bad_alloc &) {
			m_OK = false;
		}
	}

	void Time(DateTime *pValue)
	{
		pValue->Year      = U16();
		pValue->Month     = U8();
		pValue->Day       = U8();
		pValue->DayOfWeek = U8();
		pValue->Hour      = U8();
		pValue->Minute    = U8();
		pValue->Second    = U8();
		pValue->Millisecond = 0;
	}

	/** チャンクヘッダを読む(チャンクの範囲外を読むため Remain を使わない) */
	bool ReadChunkHeader(uint8_t *pTag, uint32_t *pSize)
	{
		if (!m_OK)
			return false;

		uint8_t Data[CHUNK_HEADER_SIZE];

		if (m_Stream.Read(Data, sizeof(Data)) != sizeof(Data)) {
			m_OK = false;
			return false;
		}

		*pTag = Data[0];
		*pSize = static_cast<uint32_t>(Data[1])
			| (static_cast<uint32_t>(Data[2]) << 8)
			| (static_cast<uint32_t>(Data[3]) << 16)
			| (static_cast<uint32_t>(Data[4]) << 24);

		m_Remain = *pSize;

		return true;
	}

private:
	Stream &m_Stream;
	bool m_OK = true;
	uint32_t m_Remain = 0;
};


void SerializeEvent(Writer &W, const EventInfo &Event)
{
	uint16_t Flags = static_cast<uint16_t>(Event.RunningStatus) & EventFlag::RunningStatus;

	if (Event.FreeCAMode)
		Flags |= EventFlag::FreeCAMode;
	if (!!(Event.Type & EventInfo::TypeFlag::Basic))
		Flags |= EventFlag::Basic;
	if (!!(Event.Type & EventInfo::TypeFlag::Extended))
		Flags |= EventFlag::Extended;
	if (!!(Event.Type & EventInfo::TypeFlag::Present))
		Flags |= EventFlag::Present;
	if (!!(Event.Type & EventInfo::TypeFlag::Following))
		Flags |= EventFlag::Following;
	if (Event.IsCommonEvent)
		Flags |= EventFlag::CommonEvent;

	const Stream::OffsetType EventSizePos = W.BeginChunk(Tag::Event);
	W.U16(Event.EventID);
	W.U16(Flags);
	W.Time(Event.StartTime);
	W.U32(Event.Duration);
	W.U64(Event.UpdatedTime);
	W.EndChunk(EventSizePos);

	if (!Event.EventName.empty()) {
		const Stream::OffsetType Pos = W.BeginChunk(Tag::EventName);
		W.Str(Event.EventName);
		W.EndChunk(Pos);
	}

	if (!Event.EventText.empty()) {
		const Stream::OffsetType Pos = W.BeginChunk(Tag::EventText);
		W.Str(Event.EventText);
		W.EndChunk(Pos);
	}

	if (!Event.ExtendedText.empty()) {
		const size_t Count = std::min<size_t>(Event.ExtendedText.size(), MAX_EXTENDED_TEXT_COUNT);
		const Stream::OffsetType Pos = W.BeginChunk(Tag::EventExtendedText);
		W.U8(static_cast<uint8_t>(Count));
		for (size_t i = 0; i < Count; i++) {
			W.Str(Event.ExtendedText[i].Description);
			W.Str(Event.ExtendedText[i].Text);
		}
		W.EndChunk(Pos);
	}

	if (!Event.VideoList.empty()) {
		const size_t Count = std::min<size_t>(Event.VideoList.size(), 255);
		const Stream::OffsetType Pos = W.BeginChunk(Tag::EventVideo);
		W.U8(static_cast<uint8_t>(Count));
		for (size_t i = 0; i < Count; i++) {
			const EventInfo::VideoInfo &Video = Event.VideoList[i];
			W.U8(Video.StreamContent);
			W.U8(Video.ComponentType);
			W.U8(Video.ComponentTag);
			W.U8(0);
			W.U32(Video.LanguageCode);
			W.Str(Video.Text);
		}
		W.EndChunk(Pos);
	}

	if (!Event.AudioList.empty()) {
		const size_t Count = std::min<size_t>(Event.AudioList.size(), 255);
		const Stream::OffsetType Pos = W.BeginChunk(Tag::EventAudio);
		W.U8(static_cast<uint8_t>(Count));
		for (size_t i = 0; i < Count; i++) {
			const EventInfo::AudioInfo &Audio = Event.AudioList[i];
			uint8_t AudioFlags = 0;
			if (Audio.ESMultiLingualFlag)
				AudioFlags |= 0x01_u8;
			if (Audio.MainComponentFlag)
				AudioFlags |= 0x02_u8;
			W.U8(AudioFlags);
			W.U8(Audio.StreamContent);
			W.U8(Audio.ComponentType);
			W.U8(Audio.ComponentTag);
			W.U8(Audio.SimulcastGroupTag);
			W.U8(Audio.QualityIndicator);
			W.U8(Audio.SamplingRate);
			W.U8(0);
			W.U32(Audio.LanguageCode);
			W.U32(Audio.LanguageCode2);
			W.Str(Audio.Text);
		}
		W.EndChunk(Pos);
	}

	if (Event.ContentNibble.NibbleCount > 0) {
		const int Count = std::min<int>(Event.ContentNibble.NibbleCount, MAX_NIBBLE_COUNT);
		const Stream::OffsetType Pos = W.BeginChunk(Tag::EventGenre);
		W.U8(static_cast<uint8_t>(Count));
		for (int i = 0; i < Count; i++) {
			const ContentDescriptor::NibbleInfo &Nibble = Event.ContentNibble.NibbleList[i];
			W.U8(static_cast<uint8_t>((Nibble.ContentNibbleLevel1 << 4) | (Nibble.ContentNibbleLevel2 & 0x0F)));
			W.U8(static_cast<uint8_t>((Nibble.UserNibble1 << 4) | (Nibble.UserNibble2 & 0x0F)));
		}
		W.EndChunk(Pos);
	}

	if (!Event.EventGroupList.empty()) {
		const size_t GroupCount = std::min<size_t>(Event.EventGroupList.size(), 255);
		const Stream::OffsetType Pos = W.BeginChunk(Tag::EventGroup);
		W.U8(static_cast<uint8_t>(GroupCount));
		for (size_t i = 0; i < GroupCount; i++) {
			const EventInfo::EventGroupInfo &Group = Event.EventGroupList[i];
			const size_t EventCount = std::min<size_t>(Group.EventList.size(), 255);
			W.U8(Group.GroupType);
			W.U8(static_cast<uint8_t>(EventCount));
			for (size_t j = 0; j < EventCount; j++) {
				const EventGroupDescriptor::EventInfo &GroupEvent = Group.EventList[j];
				W.U16(GroupEvent.ServiceID);
				W.U16(GroupEvent.EventID);
				W.U16(GroupEvent.NetworkID);
				W.U16(GroupEvent.TransportStreamID);
			}
		}
		W.EndChunk(Pos);
	}

	if (Event.IsCommonEvent) {
		const Stream::OffsetType Pos = W.BeginChunk(Tag::EventCommon);
		W.U16(Event.CommonEvent.ServiceID);
		W.U16(Event.CommonEvent.EventID);
		W.EndChunk(Pos);
	}

	W.EmptyChunk(Tag::EventEnd);
}


/** 番組を1個読み込む(Tag::Event チャンクのボディの読み込みから開始する) */
bool DeserializeEvent(Reader &R, const EPGDatabase::ServiceInfo &Service, EventInfo *pEvent)
{
	const uint16_t EventID = R.U16();
	const uint16_t Flags = R.U16();

	pEvent->NetworkID = Service.NetworkID;
	pEvent->TransportStreamID = Service.TransportStreamID;
	pEvent->ServiceID = Service.ServiceID;
	pEvent->EventID = EventID;
	R.Time(&pEvent->StartTime);
	pEvent->Duration = R.U32();
	pEvent->UpdatedTime = R.U64();
	pEvent->RunningStatus = static_cast<uint8_t>(Flags & EventFlag::RunningStatus);
	pEvent->FreeCAMode = (Flags & EventFlag::FreeCAMode) != 0;
	pEvent->IsCommonEvent = (Flags & EventFlag::CommonEvent) != 0;

	pEvent->Type = EventInfo::TypeFlag::None;
	if (Flags & EventFlag::Basic)
		pEvent->Type |= EventInfo::TypeFlag::Basic;
	if (Flags & EventFlag::Extended)
		pEvent->Type |= EventInfo::TypeFlag::Extended;
	if (Flags & EventFlag::Present)
		pEvent->Type |= EventInfo::TypeFlag::Present;
	if (Flags & EventFlag::Following)
		pEvent->Type |= EventInfo::TypeFlag::Following;

	R.EndChunk();

	if (!R.IsOK())
		return false;

	for (;;) {
		uint8_t ChunkTag;
		uint32_t Size;

		if (!R.ReadChunkHeader(&ChunkTag, &Size))
			return false;

		if (ChunkTag == Tag::EventEnd) {
			R.EndChunk();
			break;
		}

		switch (ChunkTag) {
		case Tag::EventName:
			R.Str(&pEvent->EventName);
			break;

		case Tag::EventText:
			R.Str(&pEvent->EventText);
			break;

		case Tag::EventExtendedText:
			{
				const uint8_t Count = R.U8();
				if (Count > MAX_EXTENDED_TEXT_COUNT) {
					R.SetError();
					break;
				}
				pEvent->ExtendedText.resize(Count);
				for (uint8_t i = 0; i < Count; i++) {
					R.Str(&pEvent->ExtendedText[i].Description);
					R.Str(&pEvent->ExtendedText[i].Text);
				}
			}
			break;

		case Tag::EventVideo:
			{
				const uint8_t Count = R.U8();
				pEvent->VideoList.resize(Count);
				for (uint8_t i = 0; i < Count; i++) {
					EventInfo::VideoInfo &Video = pEvent->VideoList[i];
					Video.StreamContent = R.U8();
					Video.ComponentType = R.U8();
					Video.ComponentTag = R.U8();
					R.U8();
					Video.LanguageCode = R.U32();
					R.Str(&Video.Text);
				}
			}
			break;

		case Tag::EventAudio:
			{
				const uint8_t Count = R.U8();
				pEvent->AudioList.resize(Count);
				for (uint8_t i = 0; i < Count; i++) {
					EventInfo::AudioInfo &Audio = pEvent->AudioList[i];
					const uint8_t AudioFlags = R.U8();
					Audio.ESMultiLingualFlag = (AudioFlags & 0x01_u8) != 0;
					Audio.MainComponentFlag = (AudioFlags & 0x02_u8) != 0;
					Audio.StreamContent = R.U8();
					Audio.ComponentType = R.U8();
					Audio.ComponentTag = R.U8();
					Audio.SimulcastGroupTag = R.U8();
					Audio.QualityIndicator = R.U8();
					Audio.SamplingRate = R.U8();
					R.U8();
					Audio.LanguageCode = R.U32();
					Audio.LanguageCode2 = R.U32();
					R.Str(&Audio.Text);
				}
			}
			break;

		case Tag::EventGenre:
			{
				const uint8_t Count = R.U8();
				if (Count > MAX_NIBBLE_COUNT) {
					R.SetError();
					break;
				}
				pEvent->ContentNibble.NibbleCount = Count;
				for (uint8_t i = 0; i < Count; i++) {
					const uint8_t ContentNibble = R.U8();
					const uint8_t UserNibble = R.U8();
					ContentDescriptor::NibbleInfo &Nibble = pEvent->ContentNibble.NibbleList[i];
					Nibble.ContentNibbleLevel1 = ContentNibble >> 4;
					Nibble.ContentNibbleLevel2 = ContentNibble & 0x0F_u8;
					Nibble.UserNibble1 = UserNibble >> 4;
					Nibble.UserNibble2 = UserNibble & 0x0F_u8;
				}
			}
			break;

		case Tag::EventGroup:
			{
				const uint8_t GroupCount = R.U8();
				pEvent->EventGroupList.resize(GroupCount);
				for (uint8_t i = 0; i < GroupCount; i++) {
					EventInfo::EventGroupInfo &Group = pEvent->EventGroupList[i];
					Group.GroupType = R.U8();
					const uint8_t EventCount = R.U8();
					Group.EventList.resize(EventCount);
					for (uint8_t j = 0; j < EventCount; j++) {
						EventGroupDescriptor::EventInfo &GroupEvent = Group.EventList[j];
						GroupEvent.ServiceID = R.U16();
						GroupEvent.EventID = R.U16();
						GroupEvent.NetworkID = R.U16();
						GroupEvent.TransportStreamID = R.U16();
					}
				}
			}
			break;

		case Tag::EventCommon:
			pEvent->CommonEvent.ServiceID = R.U16();
			pEvent->CommonEvent.EventID = R.U16();
			break;

		default:
			// 未知のチャンクは読み飛ばす
			break;
		}

		R.EndChunk();

		if (!R.IsOK())
			return false;
	}

	return R.IsOK();
}


}	// namespace




bool EPGDataSerializer::SerializeService(
	const EPGDatabase &Database,
	uint16_t NetworkID, uint16_t TransportStreamID, uint16_t ServiceID,
	Stream &DataStream, ServiceHeader *pHeader)
{
	EPGDatabase::EventList EventList;

	if (!Database.GetEventListSortedByTime(NetworkID, TransportStreamID, ServiceID, &EventList))
		return false;

	ServiceHeader Header;

	Header.Version = FormatVersion;
	Header.NetworkID = NetworkID;
	Header.TransportStreamID = TransportStreamID;
	Header.ServiceID = ServiceID;
	Header.EventCount = static_cast<uint32_t>(std::min<size_t>(EventList.size(), MAX_EVENT_COUNT));
	Header.UpdatedTime = 0;

	for (size_t i = 0; i < Header.EventCount; i++) {
		if (EventList[i].UpdatedTime > Header.UpdatedTime)
			Header.UpdatedTime = EventList[i].UpdatedTime;
	}

	Writer W(DataStream);

	W.Bytes(FormatType, sizeof(FormatType));
	W.U32(Header.Version);
	W.U16(Header.NetworkID);
	W.U16(Header.TransportStreamID);
	W.U16(Header.ServiceID);
	W.U16(0);
	W.U32(Header.EventCount);
	W.U64(Header.UpdatedTime);

	for (size_t i = 0; i < Header.EventCount; i++)
		SerializeEvent(W, EventList[i]);

	W.EmptyChunk(Tag::End);

	if (!W.IsOK())
		return false;

	if (pHeader != nullptr)
		*pHeader = Header;

	return true;
}


bool EPGDataSerializer::DeserializeService(
	Stream &DataStream, EPGDatabase &Database, ServiceHeader *pHeader)
{
	uint8_t HeaderData[HeaderSize];

	if (DataStream.Read(HeaderData, sizeof(HeaderData)) != sizeof(HeaderData))
		return false;

	ServiceHeader Header;

	if (!PeekHeader(HeaderData, sizeof(HeaderData), &Header))
		return false;

	const EPGDatabase::ServiceInfo ServiceInfo = Header.GetServiceInfo();

	EPGDatabase::EventList EventList;

	try {
		EventList.reserve(Header.EventCount);
	} catch (const std::bad_alloc &) {
		return false;
	}

	Reader R(DataStream);

	for (;;) {
		uint8_t ChunkTag;
		uint32_t Size;

		if (!R.ReadChunkHeader(&ChunkTag, &Size))
			return false;

		if (ChunkTag == Tag::End)
			break;

		if (ChunkTag == Tag::Event) {
			EventInfo Event;

			if (!DeserializeEvent(R, ServiceInfo, &Event))
				return false;

			if (EventList.size() >= MAX_EVENT_COUNT)
				return false;

			EventList.push_back(std::move(Event));
		} else {
			// 未知のチャンクは読み飛ばす
			R.EndChunk();
			if (!R.IsOK())
				return false;
		}
	}

	Database.SetServiceEventList(ServiceInfo, std::move(EventList));

	if (pHeader != nullptr)
		*pHeader = Header;

	return true;
}


bool EPGDataSerializer::PeekHeader(const void *pData, size_t Size, ServiceHeader *pHeader)
{
	if ((pData == nullptr) || (Size < HeaderSize) || (pHeader == nullptr))
		return false;

	const uint8_t *p = static_cast<const uint8_t *>(pData);

	if (std::memcmp(p, FormatType, sizeof(FormatType)) != 0)
		return false;

	const auto GetU16 = [](const uint8_t *q) -> uint16_t {
		return static_cast<uint16_t>(q[0] | (q[1] << 8));
	};
	const auto GetU32 = [](const uint8_t *q) -> uint32_t {
		return static_cast<uint32_t>(q[0])
			| (static_cast<uint32_t>(q[1]) << 8)
			| (static_cast<uint32_t>(q[2]) << 16)
			| (static_cast<uint32_t>(q[3]) << 24);
	};

	pHeader->Version = GetU32(p + 8);
	if (pHeader->Version > FormatVersion)
		return false;

	pHeader->NetworkID = GetU16(p + 12);
	pHeader->TransportStreamID = GetU16(p + 14);
	pHeader->ServiceID = GetU16(p + 16);
	pHeader->EventCount = GetU32(p + 20);
	if (pHeader->EventCount > MAX_EVENT_COUNT)
		return false;
	pHeader->UpdatedTime =
		static_cast<uint64_t>(GetU32(p + 24)) | (static_cast<uint64_t>(GetU32(p + 28)) << 32);

	return true;
}


}	// namespace LibISDB
