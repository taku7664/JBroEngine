#include "Core/Build/BuildManifest.h"

#include <charconv>
#include <iostream>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace
{
	struct ToolOptions
	{
		File::Path OutputPath;
		File::Path ValidatePath;
		std::string StartupSceneGuid;
		std::string StartupScene;
		std::string ProductName;
		std::string TargetPlatform;
		std::string ScriptMode;
		std::string ScriptModule;
		std::string Orientation;
		int Width = 1280;
		int Height = 720;
		float PixelsPerUnit = 100.0f;
		bool HasExpectedWidth = false;
		bool HasExpectedHeight = false;
	};

	std::string NarrowAscii(const wchar_t* text)
	{
		std::string result;
		if (nullptr == text)
		{
			return result;
		}
		while (*text)
		{
			const wchar_t ch = *text++;
			result.push_back(ch >= 0 && ch <= 0x7f ? static_cast<char>(ch) : '?');
		}
		return result;
	}

	std::string WideToUtf8(const wchar_t* text)
	{
		if (nullptr == text || L'\0' == text[0])
		{
			return {};
		}
#if defined(_WIN32)
		const int requiredSize = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
		if (requiredSize <= 1)
		{
			return {};
		}
		std::string result(static_cast<std::size_t>(requiredSize), '\0');
		WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), requiredSize, nullptr, nullptr);
		result.pop_back();
		return result;
#else
		return NarrowAscii(text);
#endif
	}

	bool ParseInt(const wchar_t* text, int& outValue)
	{
		const std::string value = NarrowAscii(text);
		const char* begin = value.data();
		const char* end = begin + value.size();
		const auto result = std::from_chars(begin, end, outValue);
		return result.ec == std::errc() && result.ptr == end;
	}

	bool ParseFloat(const wchar_t* text, float& outValue)
	{
		try
		{
			outValue = std::stof(std::wstring(text ? text : L""));
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	void PrintUsage()
	{
		std::wcerr << L"Usage: BuildManifestTool"
			<< L" --out <path>"
			<< L" --startup-scene-guid <guid>"
			<< L" [--product-name <name>]"
			<< L" [--startup-scene <path>]"
			<< L" [--width <int>]"
			<< L" [--height <int>]"
			<< L" [--pixels-per-unit <float>]"
			<< L" [--target-platform <name>]"
			<< L" [--script-mode <mode>]"
			<< L" [--script-module <path>]"
			<< L" [--orientation <Landscape|Portrait|Auto>]"
			<< std::endl
			<< L"   or: BuildManifestTool --validate <path>"
			<< L" [--startup-scene-guid <guid>]"
			<< L" [--product-name <name>]"
			<< L" [--width <int>]"
			<< L" [--height <int>]"
			<< L" [--target-platform <name>]"
			<< L" [--script-mode <mode>]"
			<< L" [--script-module <path>]"
			<< std::endl;
	}

	bool RequireValue(int argc, wchar_t** argv, int& index)
	{
		return index + 1 < argc && nullptr != argv[index + 1] && L'-' != argv[index + 1][0];
	}

	bool ParseArgs(int argc, wchar_t** argv, ToolOptions& outOptions)
	{
		for (int i = 1; i < argc; ++i)
		{
			const std::wstring_view arg(argv[i] ? argv[i] : L"");
			if (arg == L"--out")
			{
				if (false == RequireValue(argc, argv, i)) return false;
				outOptions.OutputPath = File::Path(argv[++i]);
			}
			else if (arg == L"--validate")
			{
				if (false == RequireValue(argc, argv, i)) return false;
				outOptions.ValidatePath = File::Path(argv[++i]);
			}
			else if (arg == L"--startup-scene-guid")
			{
				if (false == RequireValue(argc, argv, i)) return false;
				outOptions.StartupSceneGuid = NarrowAscii(argv[++i]);
			}
			else if (arg == L"--startup-scene")
			{
				if (false == RequireValue(argc, argv, i)) return false;
				outOptions.StartupScene = std::filesystem::path(argv[++i]).generic_string();
			}
			else if (arg == L"--product-name")
			{
				if (false == RequireValue(argc, argv, i)) return false;
				outOptions.ProductName = WideToUtf8(argv[++i]);
			}
			else if (arg == L"--width")
			{
				if (false == RequireValue(argc, argv, i) || false == ParseInt(argv[++i], outOptions.Width)) return false;
				outOptions.HasExpectedWidth = true;
			}
			else if (arg == L"--height")
			{
				if (false == RequireValue(argc, argv, i) || false == ParseInt(argv[++i], outOptions.Height)) return false;
				outOptions.HasExpectedHeight = true;
			}
			else if (arg == L"--pixels-per-unit")
			{
				if (false == RequireValue(argc, argv, i) || false == ParseFloat(argv[++i], outOptions.PixelsPerUnit)) return false;
			}
			else if (arg == L"--target-platform")
			{
				if (false == RequireValue(argc, argv, i)) return false;
				outOptions.TargetPlatform = NarrowAscii(argv[++i]);
			}
			else if (arg == L"--script-mode")
			{
				if (false == RequireValue(argc, argv, i)) return false;
				outOptions.ScriptMode = NarrowAscii(argv[++i]);
			}
			else if (arg == L"--script-module")
			{
				if (false == RequireValue(argc, argv, i)) return false;
				outOptions.ScriptModule = std::filesystem::path(argv[++i]).generic_string();
			}
			else if (arg == L"--orientation")
			{
				if (false == RequireValue(argc, argv, i)) return false;
				outOptions.Orientation = NarrowAscii(argv[++i]);
			}
			else
			{
				return false;
			}
		}

		if (false == outOptions.ValidatePath.empty())
		{
			return outOptions.OutputPath.empty();
		}

		return false == outOptions.OutputPath.empty()
			&& false == outOptions.StartupSceneGuid.empty();
	}

	bool ValidateManifest(const ToolOptions& options)
	{
		BuildManifest manifest;
		std::string error;
		if (false == CBuildManifestLoader::LoadFromFile(options.ValidatePath, manifest, &error))
		{
			std::cerr << (error.empty() ? "Failed to validate build manifest." : error) << std::endl;
			return false;
		}

		if (false == options.StartupSceneGuid.empty() && manifest.StartupSceneGuid != options.StartupSceneGuid)
		{
			std::cerr << "Build manifest startup scene GUID mismatch." << std::endl;
			return false;
		}
		if (options.HasExpectedWidth && manifest.ResolutionWidth != options.Width)
		{
			std::cerr << "Build manifest width mismatch." << std::endl;
			return false;
		}
		if (options.HasExpectedHeight && manifest.ResolutionHeight != options.Height)
		{
			std::cerr << "Build manifest height mismatch." << std::endl;
			return false;
		}
		if (false == options.TargetPlatform.empty() && manifest.TargetPlatform != options.TargetPlatform)
		{
			std::cerr << "Build manifest target platform mismatch." << std::endl;
			return false;
		}
		if (false == options.ProductName.empty() && manifest.ProductName != options.ProductName)
		{
			std::cerr << "Build manifest product name mismatch." << std::endl;
			return false;
		}
		if (false == options.ScriptMode.empty() && manifest.ScriptMode != options.ScriptMode)
		{
			std::cerr << "Build manifest script mode mismatch." << std::endl;
			return false;
		}
		if (false == options.ScriptModule.empty() && manifest.ScriptModule != options.ScriptModule)
		{
			std::cerr << "Build manifest script module mismatch." << std::endl;
			return false;
		}
		return true;
	}
}

int wmain(int argc, wchar_t** argv)
{
	ToolOptions options;
	if (false == ParseArgs(argc, argv, options))
	{
		PrintUsage();
		return 2;
	}

	if (false == options.ValidatePath.empty())
	{
		return ValidateManifest(options) ? 0 : 1;
	}

	BuildManifest manifest;
	manifest.Version = 1;
	manifest.ProductName = options.ProductName;
	manifest.ResolutionWidth = options.Width;
	manifest.ResolutionHeight = options.Height;
	manifest.StartupSceneGuid = options.StartupSceneGuid;
	manifest.StartupScene = options.StartupScene;
	manifest.PixelsPerUnit = options.PixelsPerUnit;
	manifest.TargetPlatform = options.TargetPlatform;
	manifest.ScriptMode = options.ScriptMode;
	manifest.ScriptModule = options.ScriptModule;
	manifest.Orientation = options.Orientation;

	std::string error;
	if (false == CBuildManifestLoader::WriteBinaryFile(options.OutputPath, manifest, &error))
	{
		std::cerr << (error.empty() ? "Failed to write build manifest." : error) << std::endl;
		return 1;
	}

	BuildManifest loadedManifest;
	if (false == CBuildManifestLoader::LoadFromFile(options.OutputPath, loadedManifest, &error))
	{
		std::cerr << (error.empty() ? "Failed to read generated build manifest." : error) << std::endl;
		return 1;
	}
	if (loadedManifest.StartupSceneGuid != manifest.StartupSceneGuid
		|| loadedManifest.ProductName != manifest.ProductName
		|| loadedManifest.ResolutionWidth != (manifest.ResolutionWidth > 0 ? manifest.ResolutionWidth : 1280)
		|| loadedManifest.ResolutionHeight != (manifest.ResolutionHeight > 0 ? manifest.ResolutionHeight : 720))
	{
		std::cerr << "Generated build manifest round-trip validation failed." << std::endl;
		return 1;
	}

	return 0;
}
