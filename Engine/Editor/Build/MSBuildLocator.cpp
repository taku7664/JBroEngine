#include "pch.h"
#include "Editor/Build/MSBuildLocator.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include <cstdlib>
#include <string>
#include <system_error>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace
{
	std::filesystem::path EnvPath(const wchar_t* name)
	{
		wchar_t* value = nullptr;
		std::size_t length = 0;
		if (0 != _wdupenv_s(&value, &length, name) || nullptr == value)
		{
			return {};
		}
		std::filesystem::path result(value);
		std::free(value);
		return result;
	}

	// 개발자 명령 프롬프트(VsDevCmd) 안에서 실행된 경우 VSINSTALLDIR 로 즉시 해석.
	std::filesystem::path FromVsInstallDir()
	{
		const std::filesystem::path vsInstallDir = EnvPath(L"VSINSTALLDIR");
		if (vsInstallDir.empty())
		{
			return {};
		}

		const std::filesystem::path candidate =
			vsInstallDir / L"MSBuild" / L"Current" / L"Bin" / L"MSBuild.exe";
		std::error_code errorCode;
		if (std::filesystem::exists(candidate, errorCode))
		{
			return candidate;
		}
		return {};
	}

	// vswhere 는 VS 설치 여부와 무관하게 항상 이 고정 위치에 배포된다.
	std::filesystem::path VswhereExecutablePath()
	{
		const std::filesystem::path programFilesX86 = EnvPath(L"ProgramFiles(x86)");
		if (programFilesX86.empty())
		{
			return {};
		}

		const std::filesystem::path vswhere =
			programFilesX86 / L"Microsoft Visual Studio" / L"Installer" / L"vswhere.exe";
		std::error_code errorCode;
		if (std::filesystem::exists(vswhere, errorCode))
		{
			return vswhere;
		}
		return {};
	}

	// 자식 프로세스의 stdout 을 바이트 그대로 모은다(vswhere 는 -utf8 로 UTF-8 출력).
	bool CaptureProcessStdout(const std::wstring& commandLine, std::string& outText)
	{
		SECURITY_ATTRIBUTES securityAttributes = {};
		securityAttributes.nLength        = sizeof(SECURITY_ATTRIBUTES);
		securityAttributes.bInheritHandle = TRUE;

		HANDLE readPipe  = nullptr;
		HANDLE writePipe = nullptr;
		if (FALSE == CreatePipe(&readPipe, &writePipe, &securityAttributes, 0))
		{
			return false;
		}
		// 부모가 쥔 read 핸들은 자식에게 상속되면 안 된다(핸들 누수/데드락 방지).
		SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

		STARTUPINFOW startupInfo = {};
		startupInfo.cb         = sizeof(STARTUPINFOW);
		startupInfo.dwFlags    = STARTF_USESTDHANDLES;
		startupInfo.hStdOutput = writePipe;
		startupInfo.hStdError  = writePipe;
		startupInfo.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);

		PROCESS_INFORMATION processInfo = {};
		std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
		mutableCommand.push_back(L'\0');

		const BOOL created = CreateProcessW(
			nullptr,
			mutableCommand.data(),
			nullptr,
			nullptr,
			TRUE,
			CREATE_NO_WINDOW,
			nullptr,
			nullptr,
			&startupInfo,
			&processInfo);

		// 자식이 write 끝을 물려받았으므로 부모 쪽 write 핸들은 닫아야 ReadFile 이 EOF 를 본다.
		CloseHandle(writePipe);

		if (FALSE == created)
		{
			CloseHandle(readPipe);
			return false;
		}

		std::string text;
		char  buffer[512];
		DWORD bytesRead = 0;
		while (TRUE == ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0)
		{
			text.append(buffer, bytesRead);
		}

		CloseHandle(readPipe);
		WaitForSingleObject(processInfo.hProcess, INFINITE);
		CloseHandle(processInfo.hThread);
		CloseHandle(processInfo.hProcess);

		outText = std::move(text);
		return true;
	}

	std::filesystem::path FromVswhere()
	{
		const std::filesystem::path vswhere = VswhereExecutablePath();
		if (vswhere.empty())
		{
			return {};
		}

		// -prerelease : Preview/Insiders 에디션도 포함(에디션/버전 하드코딩 제거의 핵심).
		// -utf8       : 경로에 non-ASCII 가 있어도 안전하게 UTF-8 로 출력.
		// -find       : 설치본 안의 MSBuild.exe 절대경로를 직접 돌려준다.
		const std::wstring command =
			L"\"" + vswhere.wstring() + L"\""
			L" -latest -prerelease -products *"
			L" -requires Microsoft.Component.MSBuild"
			L" -find MSBuild\\**\\Bin\\MSBuild.exe -utf8";

		std::string output;
		if (false == CaptureProcessStdout(command, output))
		{
			return {};
		}

		// 여러 줄이 나올 수 있다(x86/amd64 등). 존재하는 첫 줄을 사용한다.
		std::size_t start = 0;
		while (start < output.size())
		{
			std::size_t end = output.find_first_of("\r\n", start);
			if (std::string::npos == end)
			{
				end = output.size();
			}
			const std::string line = output.substr(start, end - start);
			start = end + 1;
			if (line.empty())
			{
				continue;
			}

			const int wideLen = MultiByteToWideChar(CP_UTF8, 0, line.data(),
				static_cast<int>(line.size()), nullptr, 0);
			if (wideLen <= 0)
			{
				continue;
			}
			std::wstring wide(static_cast<std::size_t>(wideLen), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, line.data(),
				static_cast<int>(line.size()), wide.data(), wideLen);

			std::filesystem::path candidate(wide);
			std::error_code errorCode;
			if (std::filesystem::exists(candidate, errorCode))
			{
				return candidate;
			}
		}
		return {};
	}
}

namespace Build
{
	std::filesystem::path LocateMSBuild()
	{
		const std::filesystem::path fromDevPrompt = FromVsInstallDir();
		if (false == fromDevPrompt.empty())
		{
			return fromDevPrompt;
		}
		return FromVswhere();
	}
}

#else

namespace Build
{
	std::filesystem::path LocateMSBuild()
	{
		return {};
	}
}

#endif
