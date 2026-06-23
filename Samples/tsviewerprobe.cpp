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
 @file   tsviewerprobe.cpp
 @brief  TS ファイルを BonDriver を介さず直接 ViewerFilter (DirectShow) に渡して再生するプローブ

 tsviewerprobe <filename> [--renderer <name>] [--video-decoder <name>]
               [--audio-clock on|off] [--duration-ms <ms>]

*/


#include "../LibISDB/LibISDB.hpp"
#include "../LibISDB/Windows/Viewer/ViewerEngine.hpp"
#include "../LibISDB/Filters/StreamSourceFilter.hpp"
#include "../LibISDB/Filters/AsyncStreamingFilter.hpp"
#include "../LibISDB/Filters/TSPacketParserFilter.hpp"
#include "../LibISDB/Filters/AnalyzerFilter.hpp"
#include <Windows.h>
#include <dshow.h>
#include <cstdio>
#include <cstdarg>
#include <iostream>
#include <string>
#include <cstdlib>


namespace
{


class ProbeEngine : public LibISDB::ViewerEngine
{
};


LibISDB::DirectShow::VideoRenderer::RendererType ParseRendererType(const std::wstring &Name)
{
	using RendererType = LibISDB::DirectShow::VideoRenderer::RendererType;

	if (Name == L"MPC Video Renderer")     return RendererType::MPCVideoRenderer;
	if (Name == L"madVR")                  return RendererType::madVR;
	if (Name == L"EVR")                    return RendererType::EVR;
	if (Name == L"EVR Custom Presenter")   return RendererType::EVRCustomPresenter;
	if (Name == L"VMR7")                   return RendererType::VMR7;
	if (Name == L"VMR9")                   return RendererType::VMR9;
	if (Name == L"VMR7 Renderless")        return RendererType::VMR7Renderless;
	if (Name == L"VMR9 Renderless")        return RendererType::VMR9Renderless;
	if (Name == L"Overlay Mixer")          return RendererType::OverlayMixer;

	return RendererType::Default;
}


void AudioRendererStatsLog(const wchar_t *pFormat, ...)
{
	FILE *fp = nullptr;
	if (_wfopen_s(&fp, L"audio_renderer_stats_debug.log", L"a, ccs=UTF-8") != 0 || fp == nullptr)
		return;
	SYSTEMTIME st;
	::GetLocalTime(&st);
	fwprintf(fp, L"%02d:%02d:%02d.%03d ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	va_list args;
	va_start(args, pFormat);
	vfwprintf(fp, pFormat, args);
	va_end(args);
	fwprintf(fp, L"\n");
	fclose(fp);
}


LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_DESTROY) {
		::PostQuitMessage(0);
		return 0;
	}
	return ::DefWindowProcW(hwnd, uMsg, wParam, lParam);
}


}


int wmain(int argc, wchar_t **argv)
{
	if (argc < 2) {
		std::wcerr <<
			L"tsviewerprobe <filename> [--renderer <name>] [--video-decoder <name>]\n"
			L"              [--audio-clock on|off] [--duration-ms <ms>]" << std::endl;
		return 1;
	}

	std::wstring FileName = argv[1];
	std::wstring RendererName = L"MPC Video Renderer";
	std::wstring VideoDecoderName = L"MPC Video Decoder (nanamitm)";
	bool UseAudioRendererClock = true;
	int DurationMs = 30000;
	LibISDB::ViewerFilter::AACDecoderType AACDecoder = LibISDB::ViewerFilter::AACDecoderType::FAAD2;
	bool PTSSync = true;
	size_t BufferSize = 0;
	bool NoVideo = false;

	for (int i = 2; i < argc; i++) {
		const std::wstring Arg = argv[i];

		if (Arg == L"--renderer" && i + 1 < argc) {
			RendererName = argv[++i];
		} else if (Arg == L"--video-decoder" && i + 1 < argc) {
			VideoDecoderName = argv[++i];
		} else if (Arg == L"--audio-clock" && i + 1 < argc) {
			const std::wstring Value = argv[++i];
			UseAudioRendererClock = (Value == L"on");
		} else if (Arg == L"--duration-ms" && i + 1 < argc) {
			DurationMs = std::wcstol(argv[++i], nullptr, 10);
		} else if (Arg == L"--aac-decoder" && i + 1 < argc) {
			const std::wstring Value = argv[++i];
			AACDecoder = (Value == L"fdk")
				? LibISDB::ViewerFilter::AACDecoderType::FDK_AAC
				: LibISDB::ViewerFilter::AACDecoderType::FAAD2;
		} else if (Arg == L"--pts-sync" && i + 1 < argc) {
			const std::wstring Value = argv[++i];
			PTSSync = (Value == L"on");
		} else if (Arg == L"--buffer-size" && i + 1 < argc) {
			BufferSize = static_cast<size_t>(std::wcstoull(argv[++i], nullptr, 10));
		} else if (Arg == L"--no-video") {
			NoVideo = true;
		}
	}

	::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

	WNDCLASSW wc{};
	wc.lpfnWndProc = &WndProc;
	wc.hInstance = ::GetModuleHandleW(nullptr);
	wc.lpszClassName = L"TsViewerProbeWindow";
	::RegisterClassW(&wc);

	const HWND hwnd = ::CreateWindowExW(
		0, wc.lpszClassName, L"tsviewerprobe", WS_OVERLAPPED,
		0, 0, 640, 360, nullptr, nullptr, wc.hInstance, nullptr);

	int ExitCode = 0;

	{
		ProbeEngine Engine;

		LibISDB::StreamSourceFilter *pSource = new LibISDB::StreamSourceFilter;
		pSource->SetSourceMode(LibISDB::SourceFilter::SourceMode::Pull);
		LibISDB::AsyncStreamingFilter *pAsyncStreaming = new LibISDB::AsyncStreamingFilter;
		pAsyncStreaming->SetSourceFilter(pSource);
		pAsyncStreaming->CreateBuffer(pAsyncStreaming->GetOutputBufferSize(), 3, 3);
		LibISDB::TSPacketParserFilter *pParser = new LibISDB::TSPacketParserFilter;
		LibISDB::AnalyzerFilter *pAnalyzer = new LibISDB::AnalyzerFilter;
		LibISDB::ViewerFilter *pViewer = new LibISDB::ViewerFilter;

		pViewer->SetUseAudioRendererClock(UseAudioRendererClock);
		pViewer->SetAACDecoderType(AACDecoder);
		pViewer->EnablePTSSync(PTSSync);
		if (BufferSize != 0)
			pViewer->SetBufferSize(BufferSize);

		Engine.SetStreamTypePlayable(LibISDB::STREAM_TYPE_MPEG2_VIDEO, true);
		Engine.SetStreamTypePlayable(LibISDB::STREAM_TYPE_H264, true);
		Engine.SetStreamTypePlayable(LibISDB::STREAM_TYPE_H265, true);

		if (!Engine.BuildEngine({pSource, pAsyncStreaming, pParser, pAnalyzer, pViewer})) {
			std::wcerr << L"BuildEngine failed" << std::endl;
			ExitCode = 1;
			goto Cleanup;
		}

		Engine.SetStartStreamingOnSourceOpen(true);

		if (!Engine.OpenSource(FileName)) {
			std::wcerr << L"Failed to open file: " << FileName << std::endl;
			ExitCode = 1;
			goto Cleanup;
		}

		std::uint8_t VideoStreamType = LibISDB::STREAM_TYPE_UNINITIALIZED;
		for (int i = 0; i < 100; i++) {
			VideoStreamType = Engine.GetVideoStreamType();
			if (VideoStreamType != LibISDB::STREAM_TYPE_UNINITIALIZED)
				break;
			if ((i % 10) == 0) {
				LibISDB::TSPacketParserFilter::PacketCountInfo Count = pParser->GetTotalPacketCount();
				std::wcout
					<< L"[DEBUG] waiting... isSourceOpen=" << Engine.IsSourceOpen()
					<< L" inputBytes=" << pParser->GetTotalInputBytes()
					<< L" inputPackets=" << Count.Input
					<< L" serviceCount=" << Engine.GetSelectableServiceCount()
					<< std::endl;
			}
			::Sleep(50);
		}

		if (VideoStreamType == LibISDB::STREAM_TYPE_UNINITIALIZED) {
			std::wcerr << L"Video stream type not detected" << std::endl;
		} else {
			LibISDB::ViewerFilter::OpenSettings Settings;

			Settings.hwndRender = hwnd;
			Settings.hwndMessageDrain = hwnd;
			Settings.VideoRenderer = ParseRendererType(RendererName);
			Settings.VideoStreamType = NoVideo ? LibISDB::STREAM_TYPE_INVALID : VideoStreamType;
			Settings.pszVideoDecoder = VideoDecoderName.c_str();

			std::wcout
				<< L"[INFO] Opening viewer (decoder=" << VideoDecoderName
				<< L" renderer=" << RendererName
				<< L" audioClock=" << (UseAudioRendererClock ? L"on" : L"off")
				<< L" aacDecoder=" << (AACDecoder == LibISDB::ViewerFilter::AACDecoderType::FDK_AAC ? L"fdk" : L"faad2")
				<< L" ptsSync=" << (PTSSync ? L"on" : L"off")
				<< L" bufferSize=" << (BufferSize != 0 ? BufferSize : 4096)
				<< L")" << std::endl;

			if (!Engine.BuildViewer(Settings)) {
				std::wcerr << L"BuildViewer failed" << std::endl;
			} else if (!Engine.EnableViewer(true)) {
				std::wcerr << L"EnableViewer failed" << std::endl;
			} else {
				std::wcout << L"[INFO] Viewer opened. Playing for " << DurationMs << L" ms..." << std::endl;
			}
		}

		LibISDB::COMPointer<IAMAudioRendererStats> pAudioStats;
		{
			const LibISDB::COMPointer<IBaseFilter> pAudioRenderer = pViewer->GetAudioRendererFilter();
			if (pAudioRenderer) {
				pAudioRenderer.QueryInterface(&pAudioStats);
				std::wcout << L"[INFO] IAMAudioRendererStats " << (pAudioStats ? L"available" : L"NOT available") << std::endl;
			}
		}

		const DWORD StartTick = ::GetTickCount();
		DWORD LastStatsTick = StartTick;
		MSG msg;
		while (static_cast<int>(::GetTickCount() - StartTick) < DurationMs) {
			if (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
				::TranslateMessage(&msg);
				::DispatchMessageW(&msg);
			} else {
				::Sleep(10);
			}

			const DWORD Now = ::GetTickCount();
			if (pAudioStats && (Now - LastStatsTick >= 1000)) {
				LastStatsTick = Now;

				DWORD BreakCount = 0, Unused = 0;
				DWORD SilenceDur = 0;
				DWORD LastBufferDur = 0;
				DWORD Jitter = 0;

				pAudioStats->GetStatParam(AM_AUDREND_STAT_PARAM_BREAK_COUNT, &BreakCount, &Unused);
				pAudioStats->GetStatParam(AM_AUDREND_STAT_PARAM_SILENCE_DUR, &SilenceDur, &Unused);
				pAudioStats->GetStatParam(AM_AUDREND_STAT_PARAM_LAST_BUFFER_DUR, &LastBufferDur, &Unused);
				pAudioStats->GetStatParam(AM_AUDREND_STAT_PARAM_JITTER, &Jitter, &Unused);

				LibISDB::TSPacketParserFilter::PacketCountInfo Count = pParser->GetTotalPacketCount();
				AudioRendererStatsLog(
					L"[SRC] isSourceOpen=%d inputBytes=%llu inputPackets=%llu",
					Engine.IsSourceOpen(),
					static_cast<unsigned long long>(pParser->GetTotalInputBytes()),
					static_cast<unsigned long long>(Count.Input));

				AudioRendererStatsLog(
					L"breakCount=%lu silenceDur=%lu lastBufferDur=%lu jitter=%lu",
					BreakCount, SilenceDur, LastBufferDur, Jitter);
			}
		}

		std::wcout << L"[INFO] Done. Closing." << std::endl;

		Engine.CloseViewer();
		Engine.CloseSource();
	}

Cleanup:
	if (hwnd != nullptr)
		::DestroyWindow(hwnd);
	::CoUninitialize();

	return ExitCode;
}
