/* LibISDB: GPL-2.0-or-later */
#ifndef LIBISDB_MPC_SPATIAL_AUDIO_H
#define LIBISDB_MPC_SPATIAL_AUDIO_H

#include "../DirectShowBase.hpp"

namespace LibISDB::DirectShow::MpcSpatialAudio {
// MPC Audio Renderer version 1: canonical FFmpeg 22.2 order, PCM16, 48 kHz.
// FL FR FC LFE BL BR FLC FRC BC SL SR TC TFL TFC TFR TBL TBC TBR
// LFE2 TSL TSR BFC BFL BFR. Never label unspecified channel order as this type.
inline constexpr GUID MediaSubtype =
{0x3740b29c,0x02a5,0x4f75,{0xa8,0xc9,0x90,0x8d,0x9b,0xac,0xb8,0x57}};
inline constexpr GUID RendererCapability =
{0x540ea4f8,0x4198,0x4a1d,{0xb9,0x6d,0x47,0xf6,0x1e,0xfd,0x81,0x1a}};
inline bool IsSupported(IBaseFilter* Filter) {
 if (!Filter) return false;
 IUnknown* Capability = nullptr;
 if (FAILED(Filter->QueryInterface(RendererCapability, reinterpret_cast<void**>(&Capability)))) return false;
 Capability->Release();
 return true;
}
inline bool IsMediaType(const CMediaType& Type) {
 if (*Type.Type() != MEDIATYPE_Audio || *Type.Subtype() != MediaSubtype
  || *Type.FormatType() != FORMAT_WaveFormatEx || Type.FormatLength() != sizeof(WAVEFORMATEXTENSIBLE)) return false;
 const auto* W = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(Type.Format());
 return W && W->Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE && W->Format.cbSize == 22
  && W->Format.nChannels == 24 && W->Format.nSamplesPerSec == 48000
  && W->Format.wBitsPerSample == 16 && W->Samples.wValidBitsPerSample == 16
  && W->Format.nBlockAlign == 48 && W->Format.nAvgBytesPerSec == 48000 * 48
  && W->dwChannelMask == 0 && W->SubFormat == MEDIASUBTYPE_PCM;
}
}
#endif
