#include "pch.h"
#include "CompilePipeline.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace
{
	std::string QuotePath(const std::filesystem::path& path)
	{
		return "\"" + path.string() + "\"";
	}

	std::string ReadTextFile(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::in | std::ios::binary);
		if (false == file.is_open())
		{
			return {};
		}

		std::ostringstream stream;
		stream << file.rdbuf();
		return stream.str();
	}

	std::string TailMessage(const std::string& text, std::size_t maxLength)
	{
		if (text.size() <= maxLength)
		{
			return text;
		}
		return text.substr(text.size() - maxLength);
	}

	std::filesystem::path MakeLogPath(const LiveCompileDesc& desc)
	{
		std::error_code errorCode;
		std::filesystem::path basePath;
		if (false == desc.IntermediateDirectory.empty())
		{
			basePath = std::filesystem::path(desc.IntermediateDirectory) / "Logs";
		}
		else if (false == desc.OutputLibraryPath.empty())
		{
			basePath = std::filesystem::path(desc.OutputLibraryPath).parent_path() / "LiveCompileLogs";
		}
		else
		{
			basePath = std::filesystem::current_path(errorCode);
		}

		if (errorCode || basePath.empty())
		{
			basePath = ".";
		}

		std::filesystem::create_directories(basePath, errorCode);
		if (errorCode)
		{
			errorCode.clear();
			basePath = std::filesystem::path(desc.OutputLibraryPath).parent_path();
			std::filesystem::create_directories(basePath, errorCode);
		}

		return basePath /
			("JBroEngine_LiveCompile_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".log");
	}

	// UTF-8 → UTF-16 변환.  사용자명/프로젝트 경로에 한글 같은 non-ASCII 가 있을 때
	// CreateProcessA 가 시스템 ANSI 코드페이지(CP949 등)로 잘못 해석하는 문제를
	// 피하기 위해 CreateProcessW 를 쓰고, 명령줄/경로 모두 wide 문자열로 전달한다.
	std::wstring Utf8ToWide(const std::string& utf8)
	{
		if (utf8.empty()) return {};
		const int requiredSize = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
			static_cast<int>(utf8.size()), nullptr, 0);
		if (requiredSize <= 0) return {};
		std::wstring out(static_cast<std::size_t>(requiredSize), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
			static_cast<int>(utf8.size()), out.data(), requiredSize);
		return out;
	}

	bool RunProcessToLog(const std::string& commandLine, const std::filesystem::path& logPath, std::int32_t& outExitCode, std::string& outError)
	{
		SECURITY_ATTRIBUTES securityAttributes = {};
		securityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
		securityAttributes.bInheritHandle = TRUE;

		HANDLE outputHandle = CreateFileW(
			logPath.wstring().c_str(),
			GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			&securityAttributes,
			CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);

		if (INVALID_HANDLE_VALUE == outputHandle)
		{
			outExitCode = static_cast<std::int32_t>(GetLastError());
			outError = "Failed to create live compile output log.";
			return false;
		}

		STARTUPINFOW startupInfo = {};
		startupInfo.cb = sizeof(STARTUPINFOW);
		startupInfo.dwFlags = STARTF_USESTDHANDLES;
		startupInfo.hStdOutput = outputHandle;
		startupInfo.hStdError = outputHandle;
		startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

		PROCESS_INFORMATION processInfo = {};
		std::wstring wideCommand = Utf8ToWide(commandLine);
		// CreateProcessW 의 lpCommandLine 은 in-place 변경이 가능해야 하므로 buffer 확보.
		std::vector<wchar_t> mutableCommand(wideCommand.begin(), wideCommand.end());
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

		CloseHandle(outputHandle);

		if (FALSE == created)
		{
			outExitCode = static_cast<std::int32_t>(GetLastError());
			outError = "Failed to start live compile process.";
			return false;
		}

		WaitForSingleObject(processInfo.hProcess, INFINITE);

		DWORD processExitCode = 1;
		GetExitCodeProcess(processInfo.hProcess, &processExitCode);
		CloseHandle(processInfo.hThread);
		CloseHandle(processInfo.hProcess);

		outExitCode = static_cast<std::int32_t>(processExitCode);
		return 0 == processExitCode;
	}
}

LiveCompileResult CCompilePipeline::Compile(const LiveCompileDesc& desc)
{
	LiveCompileResult result;
	result.OutputLibraryPath = desc.OutputLibraryPath;
	if (desc.BuildCommand.empty())
	{
		result.Message = "LiveCompile BuildCommand is empty.";
		return result;
	}

	const std::filesystem::path logPath = MakeLogPath(desc);

	std::int32_t exitCode = 1;
	std::string processError;
	const bool processSucceeded = RunProcessToLog(desc.BuildCommand, logPath, exitCode, processError);
	const std::string output = ReadTextFile(logPath);

	result.ExitCode = exitCode;
	result.Succeeded = processSucceeded;
	if (result.Succeeded)
	{
		std::error_code errorCode;
		std::filesystem::remove(logPath, errorCode);
		result.Message = "Compile succeeded.";
	}
	else
	{
		std::ostringstream message;
		message << "Compile failed.";
		message << "\nExitCode: " << exitCode;
		message << "\nBuildCommand: " << desc.BuildCommand;
		message << "\nOutputLog: " << logPath.string();
		if (false == processError.empty())
		{
			message << "\nProcessError: " << processError;
		}
		if (output.empty())
		{
			message << "\nNo compiler output was captured.";
		}
		else
		{
			message << "\n" << TailMessage(output, 4000);
		}
		result.Message = message.str();
	}
	return result;
}

std::future<LiveCompileResult> CCompilePipeline::CompileAsync(LiveCompileDesc desc)
{
	// std::launch::async 명시 — 디퍼드 실행 시 wait_for 가 무한히 timeout 만 반환하는 문제 회피.
	return std::async(std::launch::async, [d = std::move(desc), this]() {
		return this->Compile(d);
	});
}

#else

LiveCompileResult CCompilePipeline::Compile(const LiveCompileDesc& desc)
{
	(void)desc;
	LiveCompileResult result;
	result.Message = "LiveCompile is editor-only.";
	return result;
}

std::future<LiveCompileResult> CCompilePipeline::CompileAsync(LiveCompileDesc desc)
{
	std::promise<LiveCompileResult> promise;
	promise.set_value(Compile(desc));
	return promise.get_future();
}

#endif

