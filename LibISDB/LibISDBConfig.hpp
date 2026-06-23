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
 @file   LibISDBConfig.hpp
 @brief  LibISDB 設定ヘッダ
 @author DBCTRADO
*/


#ifndef LIBISDB_CONFIG_H
#define LIBISDB_CONFIG_H


//#define LIBISDB_MSB_FIRST

//#define LIBISDB_CHAR_IS_UTF8

//#define LIBISDB_TS_PACKET_PAYLOAD_ALIGN 16

// CRC calculation algorithm
#define LIBISDB_CRC_SLICING_BY_4
//#define LIBISDB_CRC_SLICING_BY_8

//#define LIBISDB_H264_STRICT_1SEG


#if __has_include("../Thirdparty/fdk-aac/libAACdec/include/aacdecoder_lib.h")
#define LIBISDB_HAS_FDK_AAC
#endif

// 22.2ch (LATM) 対応の足がかりとして、2ch/6ch AAC と LATM/LOAS AAC を
// FFmpeg (libavcodec/libswresample) でデコードする。fdk-aac 等と同様、
// 必要なヘッダ・import ライブラリ・実行時 DLL は Thirdparty/ffmpeg に
// バンドルしてある (vcpkg ビルド出力からの抜粋、LGPL 構成)。x64 用の
// バイナリしか用意していないため x64 ビルドに限定する。
#if defined(_M_X64) && __has_include("../Thirdparty/ffmpeg/include/libavcodec/avcodec.h")
#define LIBISDB_HAS_FFMPEG_AAC
#endif


#endif // ifndef LIBISDB_CONFIG_H
