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
 @file   libisdbtest.cpp
 @brief  ユニットテスト
 @author DBCTRADO
*/


#include "../LibISDB/LibISDB.hpp"

#include <clocale>

#if defined(LIBISDB_WINDOWS) && defined(LIBISDB_WCHAR)
#define LIBISDB_TEST_WMAIN
#endif

#ifndef LIBISDB_TEST_WMAIN
#define CATCH_CONFIG_MAIN
#else
#define CATCH_CONFIG_RUNNER
#endif
#include "../Thirdparty/Catch/catch.hpp"

#include "../LibISDB/Base/DebugDef.hpp"


using namespace LibISDB::Literals;

namespace
{
#ifdef __cpp_char8_t
	typedef char8_t Char8;
#else
	typedef char Char8;
#endif

	const uint8_t * operator"" _b8(const char *str, size_t length) {
		return reinterpret_cast<const uint8_t *>(str);
	}

#ifdef __cpp_char8_t
	const uint8_t * operator"" _b8(const char8_t *str, size_t length) {
		return reinterpret_cast<const uint8_t *>(str);
	}
#endif
}


#include "../LibISDB/Utilities/AlignedAlloc.hpp"

TEST_CASE("AlignedAlloc", "[utility][memory]")
{
	std::uint8_t *buffer = static_cast<std::uint8_t *>(LibISDB::AlignedAlloc(32, 16));
	CHECK((reinterpret_cast<std::uintptr_t>(buffer) & 15) == 0);
	for (size_t i = 0; i < 32; i++)
		buffer[i] = static_cast<std::uint8_t>(i);
	buffer = static_cast<std::uint8_t *>(LibISDB::AlignedRealloc(buffer, 64, 32));
	CHECK((reinterpret_cast<std::uintptr_t>(buffer) & 31) == 0);
	CHECK(
		[buffer]() -> bool {
			for (size_t i = 0; i < 32; i++) {
				if (buffer[i] != i)
					return false;
			}
			return true;
		}());
	buffer = static_cast<std::uint8_t *>(LibISDB::AlignedRealloc(buffer, 16, 32, 5));
	CHECK(((reinterpret_cast<std::uintptr_t>(buffer) + 5) & 31) == 0);
	CHECK(
		[buffer]() -> bool {
			for (size_t i = 0; i < 16; i++) {
				if (buffer[i] != i)
					return false;
			}
			return true;
		}());
	LibISDB::AlignedFree(buffer);

	buffer = static_cast<std::uint8_t *>(LibISDB::AlignedAlloc(16, 16, 15));
	CHECK(((reinterpret_cast<std::uintptr_t>(buffer) + 15) & 15) == 0);
	buffer = static_cast<std::uint8_t *>(LibISDB::AlignedRealloc(buffer, 64, 32, 2));
	CHECK(((reinterpret_cast<std::uintptr_t>(buffer) + 2) & 31) == 0);
	LibISDB::AlignedFree(buffer);
}


#include "../LibISDB/Utilities/CRC.hpp"

TEST_CASE("CRC", "[utility][hash]")
{
	// CRC-16 (IBM)
	{
		CHECK(LibISDB::CRC16::Calc(nullptr, 0) == LibISDB::CRC16::InitialValue);
		CHECK(LibISDB::CRC16::Calc(u8"The quick brown fox jumps over the lazy dog"_b8, 43) == 0x60AE_u16);

		LibISDB::Hasher<LibISDB::CRC16> crc16;

		CHECK(crc16.Get() == LibISDB::CRC16::InitialValue);
		CHECK(crc16.Calc(u8"The quick brown fox "_b8, 20) == 0xD2A3_u16);
		CHECK(crc16.Calc(u8"jumps over the lazy dog"_b8, 23) == 0x60AE_u32);
		CHECK(crc16.Get() == 0x60AE_u32);
	}

	// CRC-16-CCITT
	{
		CHECK(LibISDB::CRC16CCITT::Calc(nullptr, 0) == LibISDB::CRC16CCITT::InitialValue);
		CHECK(LibISDB::CRC16CCITT::Calc(u8"The quick brown fox jumps over the lazy dog"_b8, 43) == 0xF0C8_u16);

		LibISDB::Hasher<LibISDB::CRC16CCITT> crc16;

		CHECK(crc16.Get() == LibISDB::CRC16::InitialValue);
		CHECK(crc16.Calc(u8"The quick brown fox "_b8, 20) == 0x9D25_u16);
		CHECK(crc16.Calc(u8"jumps over the lazy dog"_b8, 23) == 0xF0C8_u16);
		CHECK(crc16.Get() == 0xF0C8_u16);
	}

	// CRC-32
	{
		CHECK(LibISDB::CRC32::Calc(nullptr, 0) == LibISDB::CRC32::InitialValue);
		CHECK(LibISDB::CRC32::Calc(u8"The quick brown fox jumps over the lazy dog"_b8, 43) == 0x414FA339_u32);

		LibISDB::Hasher<LibISDB::CRC32> crc32;

		CHECK(crc32.Get() == LibISDB::CRC32::InitialValue);
		CHECK(crc32.Calc(u8"The quick brown fox "_b8, 20) == 0x88B075E2_u32);
		CHECK(crc32.Calc(u8"jumps over the lazy dog"_b8, 23) == 0x414FA339_u32);
		CHECK(crc32.Get() == 0x414FA339_u32);
	}

	// CRC-32/MPEG-2
	{
		CHECK(LibISDB::CRC32MPEG2::Calc(nullptr, 0) == LibISDB::CRC32MPEG2::InitialValue);
		CHECK(LibISDB::CRC32MPEG2::Calc(u8"The quick brown fox jumps over the lazy dog"_b8, 43) == 0xBA62119E_u32);

		LibISDB::Hasher<LibISDB::CRC32MPEG2> crc32;

		CHECK(crc32.Get() == LibISDB::CRC32MPEG2::InitialValue);
		CHECK(crc32.Calc(u8"The quick brown fox "_b8, 20) == 0xF7E3F54F_u32);
		CHECK(crc32.Calc(u8"jumps over the lazy dog"_b8, 23) == 0xBA62119E_u32);
		CHECK(crc32.Get() == 0xBA62119E_u32);
	}
}


#include "../LibISDB/Utilities/Sort.hpp"

TEST_CASE("Sort", "[utility][sort]")
{
	int list1[] = {6, 3, 1, 2, 4, 0, 5};
	LibISDB::InsertionSort(list1);
	CHECK(
		[&list1]() -> bool {
			for (int i = 0; i < static_cast<int>(std::size(list1)); i++) {
				if (list1[i] != i)
					return false;
			}
			return true;
		}());
	LibISDB::InsertionSort(list1, list1 + std::size(list1), std::greater<int>());
	CHECK(
		[&list1]() -> bool {
			for (int i = 0; i < static_cast<int>(std::size(list1)); i++) {
				if (list1[i] != static_cast<int>(std::size(list1)) - 1 - i)
					return false;
			}
			return true;
		}());

	struct item {
		int value;
		LibISDB::String text;
		bool operator < (const item &rhs) const { return value < rhs.value; }
	};
	item list2[] = {
		{3, LIBISDB_STR("three-1")},
		{2, LIBISDB_STR("two-1")},
		{3, LIBISDB_STR("three-2")},
		{1, LIBISDB_STR("one")},
		{0, LIBISDB_STR("zero")},
		{2, LIBISDB_STR("two-2")},
	};
	const item list2_sorted[] = {
		{0, LIBISDB_STR("zero")},
		{1, LIBISDB_STR("one")},
		{2, LIBISDB_STR("two-1")},
		{2, LIBISDB_STR("two-2")},
		{3, LIBISDB_STR("three-1")},
		{3, LIBISDB_STR("three-2")},
	};
	LibISDB::InsertionSort(list2);
	CHECK((
		[&list2, &list2_sorted]() -> bool {
			for (int i = 0; i < static_cast<int>(std::size(list2)); i++) {
				if ((list2[i].value != list2_sorted[i].value)
						|| (list2[i].text != list2_sorted[i].text))
					return false;
			}
			return true;
		}()));
}


#include "../LibISDB/Utilities/MD5.hpp"

TEST_CASE("MD5", "[utility][hash]")
{
	LibISDB::MD5Value md5;

	static const LibISDB::MD5Value zero_md5 = {
		0xD4, 0x1D, 0x8C, 0xD9, 0x8F, 0x00, 0xB2, 0x04, 0xE9, 0x80, 0x09, 0x98, 0xEC, 0xF8, 0x42, 0x7E
	};
	md5 = LibISDB::CalcMD5(nullptr, 0);
	CHECK(md5 == zero_md5);

	static const LibISDB::MD5Value fox_md5 = {
		0x9E, 0x10, 0x7D, 0x9D, 0x37, 0x2B, 0xB6, 0x82, 0x6B, 0xD8, 0x1D, 0x35, 0x42, 0xA4, 0x19, 0xD6
	};
	md5 = LibISDB::CalcMD5(u8"The quick brown fox jumps over the lazy dog"_b8, 43);
	CHECK(md5 == fox_md5);
}


#include "../LibISDB/Utilities/Utilities.hpp"

TEST_CASE("Load", "[utility][load]")
{
	const Char8 *Data = u8"ABCDE";

	CHECK(LibISDB::Load16(Data) == 0x4142_u16);
	CHECK(LibISDB::Load16(Data + 1) == 0x4243_u16);
	CHECK(LibISDB::Load24(Data) == 0x414243_u32);
	CHECK(LibISDB::Load24(Data + 1) == 0x424344_u32);
	CHECK(LibISDB::Load32(Data) == 0x41424344_u32);
	CHECK(LibISDB::Load32(Data + 1) == 0x42434445_u32);
}


#include "../LibISDB/Base/ARIBString.hpp"

TEST_CASE("ARIBString", "[base][string]")
{
	LibISDB::ARIBStringDecoder decoder;
	LibISDB::String str;

	// "テレビショッピング"
	decoder.Decode("\x1b\x7c\xc6\xec\xd3\xb7\xe7\xc3\xd4\xf3\xb0"_b8, 11, &str);
	CHECK(str.compare(LIBISDB_STR("\u30c6\u30ec\u30d3\u30b7\u30e7\u30c3\u30d4\u30f3\u30b0")) == 0);

	// "番組内容②"
	decoder.Decode("\x48\x56\x41\x48\x46\x62\x4d\x46\x1b\x24\x2a\x3b\x1b\x7d\xfe\xe2"_b8, 16, &str);
	CHECK(str.compare(LIBISDB_STR("\u756a\u7d44\u5185\u5bb9\u2461")) == 0);
}


#include "../LibISDB/Base/DateTime.hpp"

TEST_CASE("DateTime", "[base][time]")
{
	LibISDB::DateTime Time;

	CHECK_FALSE(Time.IsValid());

	Time.Year        = 2000;
	Time.Month       = 12;
	Time.Day         = 31;
	Time.Hour        = 23;
	Time.Minute      = 59;
	Time.Second      = 59;
	Time.Millisecond = 0;

	Time.SetDayOfWeek();
	CHECK(Time.DayOfWeek == 0);

	CHECK(Time.IsValid());

	Time.OffsetSeconds(1);

	CHECK(Time.Year == 2001);
	CHECK(Time.Month == 1);
	CHECK(Time.Day == 1);
	CHECK(Time.DayOfWeek == 1);
	CHECK(Time.Hour == 0);
	CHECK(Time.Minute == 0);
	CHECK(Time.Second == 0);
	CHECK(Time.Millisecond == 0);

	LibISDB::DateTime Time2(Time);

	CHECK(Time2.IsValid());
	CHECK(Time2 == Time);
	CHECK_FALSE(Time2 < Time);
	CHECK(Time2 <= Time);
	CHECK_FALSE(Time2 > Time);
	CHECK(Time2 >= Time);
	CHECK(Time2.Compare(Time) == 0);
	CHECK(Time2.DiffMilliseconds(Time) == 0LL);

	Time2.OffsetMinutes(-30);

	CHECK(Time2.Year == 2000);
	CHECK(Time2.Month == 12);
	CHECK(Time2.Day == 31);
	CHECK(Time2.DayOfWeek == 0);
	CHECK(Time2.Hour == 23);
	CHECK(Time2.Minute == 30);
	CHECK(Time2.Second == 0);
	CHECK(Time2.Millisecond == 0);

	CHECK(Time2 != Time);
	CHECK(Time2 < Time);
	CHECK(Time2 <= Time);
	CHECK(Time > Time2);
	CHECK(Time >= Time2);
	CHECK(Time2.Compare(Time) < 0);
	CHECK(Time.Compare(Time2) > 0);
	CHECK(Time2.Diff(Time) == std::chrono::milliseconds(-30LL * 60LL * 1000LL));
	CHECK(Time2.DiffMilliseconds(Time) == -30LL * 60LL * 1000LL);
	CHECK(Time2.DiffSeconds(Time) == -30LL * 60LL);

	unsigned long long LinearSeconds = Time.GetLinearSeconds();
	Time2.FromLinearSeconds(LinearSeconds);
	CHECK(Time == Time2);

	LinearSeconds += 60;
	Time.FromLinearSeconds(LinearSeconds);
	Time2.OffsetSeconds(60);
	CHECK(Time == Time2);

	Time.Millisecond = 500;
	unsigned long long LinearMilliseconds = Time.GetLinearMilliseconds();
	CHECK(LinearMilliseconds == LinearSeconds * 1000ULL + 500ULL);
	Time2.FromLinearMilliseconds(LinearMilliseconds);
	CHECK(Time == Time2);
}


#include "../LibISDB/Base/MemoryStream.hpp"

TEST_CASE("MemoryStream", "[base][stream]")
{
	LibISDB::MemoryStream Stream;

	CHECK(Stream.IsOpen());
	CHECK(Stream.IsEnd());
	CHECK(Stream.GetSize() == 0);
	CHECK(Stream.GetPos() == 0);

	static const std::uint8_t Data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

	CHECK(Stream.Write(Data, 4) == 4);
	CHECK(Stream.Write(Data + 4, 4) == 4);
	CHECK(Stream.GetSize() == 8);
	CHECK(Stream.GetPos() == 8);
	CHECK(Stream.IsEnd());
	CHECK(std::memcmp(Stream.GetData(), Data, 8) == 0);

	// 読み込み
	CHECK(Stream.SetPos(0));
	CHECK_FALSE(Stream.IsEnd());

	std::uint8_t Buffer[8] = {};

	CHECK(Stream.Read(Buffer, 3) == 3);
	CHECK(std::memcmp(Buffer, Data, 3) == 0);
	CHECK(Stream.GetPos() == 3);

	// 終端を超える読み込みは読める分だけ読まれる
	CHECK(Stream.Read(Buffer, 8) == 5);
	CHECK(std::memcmp(Buffer, Data + 3, 5) == 0);
	CHECK(Stream.IsEnd());
	CHECK(Stream.Read(Buffer, 1) == 0);

	// シーク
	CHECK(Stream.SetPos(-2, LibISDB::Stream::SetPosType::End));
	CHECK(Stream.GetPos() == 6);
	CHECK(Stream.SetPos(-4, LibISDB::Stream::SetPosType::Current));
	CHECK(Stream.GetPos() == 2);
	CHECK_FALSE(Stream.SetPos(-1));
	CHECK_FALSE(Stream.SetPos(9));
	CHECK(Stream.GetPos() == 2);

	// 途中への上書き
	CHECK(Stream.Write(Data, 2) == 2);
	CHECK(Stream.GetSize() == 8);
	CHECK(Stream.GetData()[2] == 0x01);
	CHECK(Stream.GetData()[3] == 0x02);

	// 最大サイズ
	CHECK(Stream.SetPos(0, LibISDB::Stream::SetPosType::End));
	Stream.SetMaxSize(10);
	CHECK(Stream.Write(Data, 2) == 2);
	CHECK(Stream.Write(Data, 1) == 0);
	CHECK(Stream.GetSize() == 10);

	Stream.SetMaxSize(0);
	Stream.Clear();
	CHECK(Stream.GetSize() == 0);
	CHECK(Stream.GetPos() == 0);

	// バッファの受け渡し
	CHECK(Stream.SetData(Data, sizeof(Data)));
	CHECK(Stream.GetSize() == sizeof(Data));
	CHECK(Stream.GetPos() == 0);

	LibISDB::MemoryStream::BufferType DetachedBuffer = Stream.DetachBuffer();

	CHECK(DetachedBuffer.size() == sizeof(Data));
	CHECK(Stream.GetSize() == 0);
}


#include "../LibISDB/EPG/EPGDataSerializer.hpp"

namespace
{

LibISDB::EventInfo MakeTestEvent(std::uint16_t EventID, std::uint64_t UpdatedTime)
{
	LibISDB::EventInfo Event;

	Event.NetworkID = 0x0004;
	Event.TransportStreamID = 0x4010;
	Event.ServiceID = 0x00E4;
	Event.EventID = EventID;
	Event.StartTime.Year = 2026;
	Event.StartTime.Month = 8;
	Event.StartTime.Day = 29;
	Event.StartTime.Hour = 21;
	Event.StartTime.Minute = 30;
	Event.StartTime.Second = 0;
	Event.StartTime.Millisecond = 0;
	Event.StartTime.SetDayOfWeek();
	Event.Duration = 3600;
	Event.RunningStatus = 4;
	Event.FreeCAMode = true;
	Event.EventName = LIBISDB_STR("テスト番組『表題』");
	Event.EventText = LIBISDB_STR("概要のテキスト\r\n2行目");
	Event.Type = LibISDB::EventInfo::TypeFlag::Basic | LibISDB::EventInfo::TypeFlag::Extended;
	Event.UpdatedTime = UpdatedTime;

	Event.ExtendedText.resize(2);
	Event.ExtendedText[0].Description = LIBISDB_STR("出演者");
	Event.ExtendedText[0].Text = LIBISDB_STR("山田太郎、鈴木花子");
	Event.ExtendedText[1].Description = LIBISDB_STR("番組内容");
	Event.ExtendedText[1].Text = LIBISDB_STR("サロゲートペアを含む \U00020BB7 のテキスト");

	Event.VideoList.resize(1);
	Event.VideoList[0].StreamContent = 0x01;
	Event.VideoList[0].ComponentType = 0xB1;
	Event.VideoList[0].ComponentTag = 0x00;
	Event.VideoList[0].LanguageCode = 0x6A706E;
	Event.VideoList[0].Text = LIBISDB_STR("映像");

	Event.AudioList.resize(2);
	Event.AudioList[0].StreamContent = 0x02;
	Event.AudioList[0].ComponentType = 0x03;
	Event.AudioList[0].ComponentTag = 0x10;
	Event.AudioList[0].SimulcastGroupTag = 0xFF;
	Event.AudioList[0].ESMultiLingualFlag = false;
	Event.AudioList[0].MainComponentFlag = true;
	Event.AudioList[0].QualityIndicator = 1;
	Event.AudioList[0].SamplingRate = 7;
	Event.AudioList[0].LanguageCode = 0x6A706E;
	Event.AudioList[0].LanguageCode2 = 0;
	Event.AudioList[0].Text = LIBISDB_STR("主音声");
	Event.AudioList[1] = Event.AudioList[0];
	Event.AudioList[1].ComponentTag = 0x11;
	Event.AudioList[1].ESMultiLingualFlag = true;
	Event.AudioList[1].MainComponentFlag = false;
	Event.AudioList[1].LanguageCode2 = 0x656E67;
	Event.AudioList[1].Text = LIBISDB_STR("副音声");

	Event.ContentNibble.NibbleCount = 2;
	Event.ContentNibble.NibbleList[0].ContentNibbleLevel1 = 0x7;
	Event.ContentNibble.NibbleList[0].ContentNibbleLevel2 = 0x3;
	Event.ContentNibble.NibbleList[0].UserNibble1 = 0xF;
	Event.ContentNibble.NibbleList[0].UserNibble2 = 0xF;
	Event.ContentNibble.NibbleList[1].ContentNibbleLevel1 = 0x1;
	Event.ContentNibble.NibbleList[1].ContentNibbleLevel2 = 0x2;
	Event.ContentNibble.NibbleList[1].UserNibble1 = 0x0;
	Event.ContentNibble.NibbleList[1].UserNibble2 = 0x1;

	Event.EventGroupList.resize(1);
	Event.EventGroupList[0].GroupType = LibISDB::EventGroupDescriptor::GROUP_TYPE_COMMON;
	Event.EventGroupList[0].EventList.resize(2);
	Event.EventGroupList[0].EventList[0] = {0x00E4, 0x1234, 0x0004, 0x4010};
	Event.EventGroupList[0].EventList[1] = {0x00E5, 0x1235, 0x0004, 0x4011};

	return Event;
}

}	// namespace

TEST_CASE("EPGDataSerializer", "[epg][serialize]")
{
	const LibISDB::EPGDatabase::ServiceInfo Service(0x0004, 0x4010, 0x00E4);

	LibISDB::EPGDatabase SrcDatabase;

	{
		LibISDB::EPGDatabase::EventList EventList;

		EventList.push_back(MakeTestEvent(0x1000, 1000));
		EventList.push_back(MakeTestEvent(0x1001, 2000));
		EventList[1].StartTime.OffsetHours(1);
		EventList[1].EventName = LIBISDB_STR("2番目の番組");
		EventList[1].ExtendedText.clear();
		EventList[1].EventGroupList.clear();
		EventList[1].IsCommonEvent = true;
		EventList[1].CommonEvent.ServiceID = 0x00E5;
		EventList[1].CommonEvent.EventID = 0x2001;

		CHECK(SrcDatabase.SetServiceEventList(Service, std::move(EventList)));
	}

	LibISDB::MemoryStream Stream;
	LibISDB::EPGDataSerializer::ServiceHeader Header;

	REQUIRE(LibISDB::EPGDataSerializer::SerializeService(
		SrcDatabase, Service.NetworkID, Service.TransportStreamID, Service.ServiceID,
		Stream, &Header));

	CHECK(Header.NetworkID == Service.NetworkID);
	CHECK(Header.TransportStreamID == Service.TransportStreamID);
	CHECK(Header.ServiceID == Service.ServiceID);
	CHECK(Header.EventCount == 2);
	CHECK(Header.UpdatedTime == 2000);

	// ヘッダのみの取得
	{
		LibISDB::EPGDataSerializer::ServiceHeader PeekedHeader;

		CHECK(LibISDB::EPGDataSerializer::PeekHeader(
			Stream.GetData(), Stream.GetDataSize(), &PeekedHeader));
		CHECK(PeekedHeader.ServiceID == Header.ServiceID);
		CHECK(PeekedHeader.EventCount == Header.EventCount);
		CHECK(PeekedHeader.UpdatedTime == Header.UpdatedTime);

		// 短すぎるデータ / 不正なデータ
		CHECK_FALSE(LibISDB::EPGDataSerializer::PeekHeader(
			Stream.GetData(), LibISDB::EPGDataSerializer::HeaderSize - 1, &PeekedHeader));

		LibISDB::MemoryStream::BufferType Broken(
			Stream.GetData(), Stream.GetData() + LibISDB::EPGDataSerializer::HeaderSize);
		Broken[0] = 'X';
		CHECK_FALSE(LibISDB::EPGDataSerializer::PeekHeader(
			Broken.data(), Broken.size(), &PeekedHeader));
	}

	// 往復
	LibISDB::EPGDatabase DstDatabase;
	LibISDB::EPGDataSerializer::ServiceHeader ReadHeader;

	REQUIRE(Stream.SetPos(0));
	REQUIRE(LibISDB::EPGDataSerializer::DeserializeService(Stream, DstDatabase, &ReadHeader));

	CHECK(ReadHeader.NetworkID == Header.NetworkID);
	CHECK(ReadHeader.TransportStreamID == Header.TransportStreamID);
	CHECK(ReadHeader.ServiceID == Header.ServiceID);
	CHECK(ReadHeader.EventCount == Header.EventCount);
	CHECK(ReadHeader.UpdatedTime == Header.UpdatedTime);

	LibISDB::EPGDatabase::EventList SrcList, DstList;

	REQUIRE(SrcDatabase.GetEventListSortedByTime(
		Service.NetworkID, Service.TransportStreamID, Service.ServiceID, &SrcList));
	REQUIRE(DstDatabase.GetEventListSortedByTime(
		Service.NetworkID, Service.TransportStreamID, Service.ServiceID, &DstList));

	REQUIRE(SrcList.size() == 2);
	REQUIRE(DstList.size() == SrcList.size());

	for (std::size_t i = 0; i < SrcList.size(); i++) {
		// SourceID は転送されないため IsEqual() で比較する
		CHECK(SrcList[i].IsEqual(DstList[i]));
		CHECK(SrcList[i].Type == DstList[i].Type);
		CHECK(SrcList[i].UpdatedTime == DstList[i].UpdatedTime);
	}

	// マージ元として使用できること
	LibISDB::EPGDatabase MergeDatabase;

	CHECK(MergeDatabase.MergeService(
		&DstDatabase, Service.NetworkID, Service.TransportStreamID, Service.ServiceID,
		LibISDB::EPGDatabase::MergeFlag::Database
			| LibISDB::EPGDatabase::MergeFlag::MergeBasicExtended));

	LibISDB::EPGDatabase::EventList MergedList;

	REQUIRE(MergeDatabase.GetEventListSortedByTime(
		Service.NetworkID, Service.TransportStreamID, Service.ServiceID, &MergedList));
	CHECK(MergedList.size() == SrcList.size());

	// SetServiceUpdated を指定していないので更新済みにはならない
	CHECK_FALSE(MergeDatabase.IsServiceUpdated(
		Service.NetworkID, Service.TransportStreamID, Service.ServiceID));

	// 途中で切れたデータを読み込んでも失敗するだけで済むこと
	{
		LibISDB::MemoryStream Truncated;
		LibISDB::EPGDatabase BrokenDatabase;

		CHECK(Truncated.SetData(Stream.GetData(), Stream.GetDataSize() / 2));
		CHECK_FALSE(LibISDB::EPGDataSerializer::DeserializeService(Truncated, BrokenDatabase));
	}
}




#ifdef LIBISDB_TEST_WMAIN

static char * ConvertArg(const wchar_t *arg)
{
	size_t max_length = std::wcslen(arg) * MB_CUR_MAX + 1;
	char *mbs = new char[max_length];

#ifdef _MSC_VER
	size_t dest_length;
	::wcstombs_s(&dest_length, mbs, max_length, arg, max_length);
#else
	std::wcstombs(mbs, arg, max_length);
#endif

	return mbs;
}

static char ** ConvertArgs(int argc, wchar_t **argv)
{
	char **args = new char *[argc + 1];

	for (int i = 0; i < argc; i++) {
		args[i] = ConvertArg(argv[i]);
	}

	args[argc] = nullptr;

	return args;
}

static void FreeArgs(int argc, char **argv)
{
	for (int i = 0; i < argc; i++)
		delete [] argv[i];
	delete [] argv;
}

int wmain(int argc, wchar_t **argv)
{
	std::setlocale(LC_ALL, "");
	char **args = ConvertArgs(argc, argv);
	int result = Catch::Session().run(argc, args);
	FreeArgs(argc, args);

	return result < 0xff ? result : 0xff;
}

#endif
