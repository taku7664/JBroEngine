#include "Core/Build/BuildManifest.h"
#include "ThirdParty/magic_enum/magic_enum.hpp"
#include "yaml-cpp/yaml.h"

#include <charconv>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace
{
	struct ToolOptions
	{
		File::Path OutputPath;
		File::Path ValidatePath;
		File::Path InputMapPath;
		std::string StartupCanvasGuid;
		std::string StartupCanvas;
		std::string ProductName;
		std::string TargetPlatform;
		std::string ScriptMode;
		std::string ScriptModule;
		std::string Orientation;
		std::vector<std::string> BuildCanvases;      // 런타임 선로드 씬 이름(경로) — 인덱스 1:1
		std::vector<std::string> BuildCanvasGuids;  // 각 씬의 에셋 GUID
		int Width = 1280;
		int Height = 720;
		float PixelsPerUnit = 100.0f;
		bool HasExpectedWidth = false;
		bool HasExpectedHeight = false;
	};

	int BindingNameToCode(EInputBindingSource source, const std::string& name)
	{
		switch (source)
		{
		case EInputBindingSource::Key:
		{
			const auto value = magic_enum::enum_cast<EKeyCode>(name);
			return value.has_value() ? static_cast<int>(*value) : 0;
		}
		case EInputBindingSource::MouseButton:
		{
			const auto value = magic_enum::enum_cast<EMouseButton>(name);
			return value.has_value() ? static_cast<int>(*value) : 0;
		}
		case EInputBindingSource::GamepadButton:
		{
			const auto value = magic_enum::enum_cast<EGamepadButton>(name);
			return value.has_value() ? static_cast<int>(*value) : 0;
		}
		case EInputBindingSource::GamepadAxis:
		{
			const auto value = magic_enum::enum_cast<EGamepadAxis>(name);
			return value.has_value() ? static_cast<int>(*value) : 0;
		}
		case EInputBindingSource::GamepadStick:
			return name == "Right" ? 1 : 0;
		default:
			return 0;
		}
	}

	bool LoadInputMap(const File::Path& projectPath, std::vector<InputActionDef>& outActions)
	{
		if (projectPath.empty())
		{
			return true;
		}

		YAML::Node root;
		try
		{
			root = YAML::LoadFile(projectPath.string());
		}
		catch (const YAML::Exception&)
		{
			return false;
		}

		const YAML::Node actions = root["InputActions"];
		if (!actions || false == actions.IsSequence())
		{
			return true;
		}
		for (const YAML::Node& actionNode : actions)
		{
			InputActionDef action;
			action.Name = actionNode["Name"].as<std::string>("");
			if (action.Name.empty())
			{
				continue;
			}
			const auto type = magic_enum::enum_cast<EInputActionValueType>(
				actionNode["Type"].as<std::string>("Bool"));
			action.Type = type.has_value() ? *type : EInputActionValueType::Bool;

			const YAML::Node bindings = actionNode["Bindings"];
			if (bindings && bindings.IsSequence())
			{
				for (const YAML::Node& bindingNode : bindings)
				{
					InputBinding binding;
					const auto source = magic_enum::enum_cast<EInputBindingSource>(
						bindingNode["Source"].as<std::string>("Key"));
					binding.Source = source.has_value() ? *source : EInputBindingSource::Key;
					binding.Code = BindingNameToCode(binding.Source,
						bindingNode["Code"].as<std::string>(""));
					binding.GamepadIndex = bindingNode["GamepadIndex"].as<int>(-1);
					const auto composite = magic_enum::enum_cast<EInputComposite>(
						bindingNode["Composite"].as<std::string>("None"));
					binding.Composite = composite.has_value() ? *composite : EInputComposite::None;
					action.Bindings.push_back(binding);
				}
			}
			outActions.push_back(std::move(action));
		}
		return true;
	}

	bool LoadFontSettings(const File::Path& projectPath, std::string& outDefaultFamily,
		std::vector<std::string>& outFallbackFamilies)
	{
		outDefaultFamily.clear(); outFallbackFamilies.clear();
		if (projectPath.empty()) return true;
		try
		{
			const YAML::Node root = YAML::LoadFile(projectPath.string());
			outDefaultFamily = root["DefaultFontFamilyGuid"].as<std::string>("");
			if (const YAML::Node fallbacks = root["FallbackFontFamilies"]; fallbacks && fallbacks.IsSequence())
			{
				for (const YAML::Node& node : fallbacks)
				{
					const std::string guid = node.as<std::string>("");
					if (false == guid.empty()) outFallbackFamilies.push_back(guid);
				}
			}
			return true;
		}
		catch (const YAML::Exception&)
		{
			return false;
		}
	}

	// 오디오 믹싱 버스 이름 목록. 없으면 빈 목록 — 런타임이 Master 만 갖는다(예전 동작).
	// 매니페스트에 싣지 않으면 패키지 게임에서만 카테고리 볼륨이 죽으므로 반드시 옮긴다.
	bool LoadAudioBuses(const File::Path& projectPath, std::vector<AudioBusDef>& outBuses)
	{
		outBuses.clear();
		if (projectPath.empty()) return true;
		try
		{
			const YAML::Node root = YAML::LoadFile(projectPath.string());
			if (const YAML::Node buses = root["AudioBuses"]; buses && buses.IsSequence())
			{
				for (const YAML::Node& node : buses)
				{
					AudioBusDef bus;
					// 스칼라(이름만)도 받는다 — 음량이 붙기 전 포맷.
					if (node.IsMap())
					{
						bus.Name   = node["Name"].as<std::string>("");
						bus.Volume = node["Volume"].as<float>(1.0f);
					}
					else
					{
						bus.Name = node.as<std::string>("");
					}
					if (false == bus.Name.empty()) outBuses.push_back(std::move(bus));
				}
			}
			return true;
		}
		catch (const YAML::Exception&)
		{
			return false;
		}
	}

	// 매니페스트 검증용 비교 — 이름과 음량이 모두 같아야 최신이다.
	bool AudioBusesEqual(const std::vector<AudioBusDef>& lhs, const std::vector<AudioBusDef>& rhs)
	{
		if (lhs.size() != rhs.size()) return false;
		for (std::size_t i = 0; i < lhs.size(); ++i)
		{
			if (lhs[i].Name != rhs[i].Name || lhs[i].Volume != rhs[i].Volume) return false;
		}
		return true;
	}

	bool InputMapsEqual(const std::vector<InputActionDef>& lhs, const std::vector<InputActionDef>& rhs)
	{
		if (lhs.size() != rhs.size())
		{
			return false;
		}
		for (std::size_t actionIndex = 0; actionIndex < lhs.size(); ++actionIndex)
		{
			const InputActionDef& leftAction = lhs[actionIndex];
			const InputActionDef& rightAction = rhs[actionIndex];
			if (leftAction.Name != rightAction.Name || leftAction.Type != rightAction.Type
				|| leftAction.Bindings.size() != rightAction.Bindings.size())
			{
				return false;
			}
			for (std::size_t bindingIndex = 0; bindingIndex < leftAction.Bindings.size(); ++bindingIndex)
			{
				const InputBinding& left = leftAction.Bindings[bindingIndex];
				const InputBinding& right = rightAction.Bindings[bindingIndex];
				if (left.Source != right.Source || left.Code != right.Code
					|| left.GamepadIndex != right.GamepadIndex || left.Composite != right.Composite)
				{
					return false;
				}
			}
		}
		return true;
	}

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
			<< L" --startup-canvas-guid <guid>"
			<< L" [--product-name <name>]"
			<< L" [--startup-canvas <path>]"
			<< L" [--width <int>]"
			<< L" [--height <int>]"
			<< L" [--pixels-per-unit <float>]"
			<< L" [--target-platform <name>]"
			<< L" [--script-mode <mode>]"
			<< L" [--script-module <path>]"
			<< L" [--orientation <Landscape|Portrait|Auto>]"
			<< L" [--input-map <project.jproject>]"
			<< L" [--build-canvas <path> --build-canvas-guid <guid>]..."
			<< std::endl
			<< L"   or: BuildManifestTool --validate <path>"
			<< L" [--startup-canvas-guid <guid>]"
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
			else if (arg == L"--startup-canvas-guid")
			{
				if (false == RequireValue(argc, argv, i)) return false;
				outOptions.StartupCanvasGuid = NarrowAscii(argv[++i]);
			}
			else if (arg == L"--startup-canvas")
			{
				if (false == RequireValue(argc, argv, i)) return false;
				outOptions.StartupCanvas = std::filesystem::path(argv[++i]).generic_string();
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
			else if (arg == L"--input-map")
			{
				if (false == RequireValue(argc, argv, i)) return false;
				outOptions.InputMapPath = File::Path(argv[++i]);
			}
			else if (arg == L"--build-canvas")
			{
				if (false == RequireValue(argc, argv, i)) return false;
				outOptions.BuildCanvases.push_back(std::filesystem::path(argv[++i]).generic_string());
			}
			else if (arg == L"--build-canvas-guid")
			{
				if (false == RequireValue(argc, argv, i)) return false;
				outOptions.BuildCanvasGuids.push_back(NarrowAscii(argv[++i]));
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
			&& false == outOptions.StartupCanvasGuid.empty();
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

		if (false == options.StartupCanvasGuid.empty() && manifest.StartupCanvasGuid != options.StartupCanvasGuid)
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
		if (false == options.InputMapPath.empty())
		{
			std::vector<InputActionDef> expectedActions;
			std::string expectedDefaultFamily;
			std::vector<std::string> expectedFallbackFamilies;
			std::vector<AudioBusDef> expectedBuses;
			if (false == LoadInputMap(options.InputMapPath, expectedActions)
				|| false == LoadFontSettings(options.InputMapPath, expectedDefaultFamily, expectedFallbackFamilies)
				|| false == LoadAudioBuses(options.InputMapPath, expectedBuses)
				|| false == InputMapsEqual(manifest.InputActions, expectedActions)
				|| manifest.DefaultFontFamilyGuid != expectedDefaultFamily
				|| manifest.FallbackFontFamilyGuids != expectedFallbackFamilies
				|| false == AudioBusesEqual(manifest.AudioBuses, expectedBuses))
			{
				std::cerr << "Build manifest project settings mismatch." << std::endl;
				return false;
			}
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
	manifest.StartupCanvasGuid = options.StartupCanvasGuid;
	manifest.StartupCanvas = options.StartupCanvas;
	manifest.PixelsPerUnit = options.PixelsPerUnit;
	manifest.TargetPlatform = options.TargetPlatform;
	manifest.ScriptMode = options.ScriptMode;
	manifest.ScriptModule = options.ScriptModule;
	manifest.Orientation = options.Orientation;
	manifest.BuildCanvases = options.BuildCanvases;
	manifest.BuildCanvasGuids = options.BuildCanvasGuids;
	// name/guid 는 항상 쌍으로 넘어오지만, 방어적으로 길이를 맞춘다(짧으면 빈 GUID 로 패딩).
	manifest.BuildCanvasGuids.resize(manifest.BuildCanvases.size());
	if (false == LoadInputMap(options.InputMapPath, manifest.InputActions))
	{
		std::cerr << "Failed to read input map from project." << std::endl;
		return 1;
	}
	if (false == LoadAudioBuses(options.InputMapPath, manifest.AudioBuses))
	{
		std::cerr << "Failed to read audio buses from the project file." << std::endl;
		return 1;
	}
	if (false == LoadFontSettings(options.InputMapPath, manifest.DefaultFontFamilyGuid, manifest.FallbackFontFamilyGuids))
	{
		std::cerr << "Failed to read font settings from project." << std::endl;
		return 1;
	}

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
	if (loadedManifest.StartupCanvasGuid != manifest.StartupCanvasGuid
		|| loadedManifest.ProductName != manifest.ProductName
		|| loadedManifest.ResolutionWidth != (manifest.ResolutionWidth > 0 ? manifest.ResolutionWidth : 1280)
		|| loadedManifest.ResolutionHeight != (manifest.ResolutionHeight > 0 ? manifest.ResolutionHeight : 720)
		|| loadedManifest.InputActions.size() != manifest.InputActions.size()
		|| loadedManifest.DefaultFontFamilyGuid != manifest.DefaultFontFamilyGuid
		|| loadedManifest.FallbackFontFamilyGuids != manifest.FallbackFontFamilyGuids)
	{
		std::cerr << "Generated build manifest round-trip validation failed." << std::endl;
		return 1;
	}

	return 0;
}
