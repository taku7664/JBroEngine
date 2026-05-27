#include "pch.h"
#include "GameScriptProjectGenerator.h"

#include "Core/Logging/LoggerInternal.h"

#include <cctype>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <vector>

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

namespace
{
	struct ScriptClassDesc
	{
		std::filesystem::path HeaderPath;
		std::string ClassName;
	};

	std::filesystem::path ResolveEnginePropsPath(const ProjectInfo& projectInfo)
	{
		const std::filesystem::path currentPath = std::filesystem::current_path();
		const std::vector<std::filesystem::path> candidates =
		{
			projectInfo.OriginPath / "SDK" / "JBroEngine.props",
			currentPath / "SDK" / "JBroEngine.props",
			currentPath / ".." / "SDK" / "JBroEngine.props",
			currentPath / ".." / ".." / "SDK" / "JBroEngine.props"
		};

		std::error_code errorCode;
		for (const std::filesystem::path& candidate : candidates)
		{
			std::filesystem::path normalized = std::filesystem::weakly_canonical(candidate, errorCode);
			if (errorCode)
			{
				errorCode.clear();
				normalized = std::filesystem::absolute(candidate, errorCode);
			}
			if (!errorCode && std::filesystem::exists(normalized, errorCode))
			{
				return normalized;
			}
			errorCode.clear();
		}

		return projectInfo.OriginPath / "SDK" / "JBroEngine.props";
	}

	std::filesystem::path ResolveProjectTemplatePath(const ProjectInfo& projectInfo)
	{
		const std::filesystem::path currentPath = std::filesystem::current_path();
		const std::vector<std::filesystem::path> candidates =
		{
			projectInfo.OriginPath / "SDK" / "Templates" / "GameScript.vcxproj.template",
			currentPath / "SDK" / "Templates" / "GameScript.vcxproj.template",
			currentPath / ".." / "SDK" / "Templates" / "GameScript.vcxproj.template",
			currentPath / ".." / ".." / "SDK" / "Templates" / "GameScript.vcxproj.template"
		};

		std::error_code errorCode;
		for (const std::filesystem::path& candidate : candidates)
		{
			std::filesystem::path normalized = std::filesystem::weakly_canonical(candidate, errorCode);
			if (errorCode)
			{
				errorCode.clear();
				normalized = std::filesystem::absolute(candidate, errorCode);
			}
			if (!errorCode && std::filesystem::exists(normalized, errorCode))
			{
				return normalized;
			}
			errorCode.clear();
		}

		return {};
	}

	bool ReadTextFile(const std::filesystem::path& path, std::string& outText)
	{
		std::ifstream file(path, std::ios::in | std::ios::binary);
		if (false == file.is_open())
		{
			return false;
		}

		outText.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		return true;
	}

	void ReplaceAll(std::string& text, const std::string& from, const std::string& to)
	{
		if (from.empty())
		{
			return;
		}

		std::size_t pos = 0;
		while ((pos = text.find(from, pos)) != std::string::npos)
		{
			text.replace(pos, from.length(), to);
			pos += to.length();
		}
	}

	std::string FormatVisualStudioGuid(File::Guid guid)
	{
		std::string text = guid.generic_string();
		if (text.length() != 32)
		{
			return "{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}";
		}

		for (char& ch : text)
		{
			ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
		}

		return "{"
			+ text.substr(0, 8) + "-"
			+ text.substr(8, 4) + "-"
			+ text.substr(12, 4) + "-"
			+ text.substr(16, 4) + "-"
			+ text.substr(20, 12) + "}";
	}

	std::uint64_t Fnv1a64(std::string_view text, std::uint64_t seed)
	{
		std::uint64_t hash = seed;
		for (char ch : text)
		{
			hash ^= static_cast<unsigned char>(ch);
			hash *= 1099511628211ull;
		}
		return hash;
	}

	File::Guid MakeStableProjectGuid(const ProjectInfo& projectInfo)
	{
		const std::string key = projectInfo.RootPath.generic_string() + "|GameScript";
		const std::uint64_t high = Fnv1a64(key, 14695981039346656037ull);
		const std::uint64_t low  = Fnv1a64(key, 1099511628211ull);

		std::ostringstream stream;
		stream << std::hex << std::setfill('0')
			<< std::setw(16) << high
			<< std::setw(16) << low;
		return File::Guid(stream.str());
	}

	std::string GetProjectName(const ProjectInfo& projectInfo)
	{
		const std::string rootName = projectInfo.RootPath.filename().generic_string();
		return rootName.empty() ? "GameScript" : rootName;
	}

	std::string ToIncludePath(const std::filesystem::path& path)
	{
		return path.generic_string();
	}

	std::vector<ScriptClassDesc> CollectScriptClasses(const ProjectInfo& projectInfo)
	{
		std::vector<ScriptClassDesc> scripts;
		const std::filesystem::path scriptPath = projectInfo.ScriptPath;
		if (scriptPath.empty())
		{
			return scripts;
		}

		std::error_code errorCode;
		if (false == std::filesystem::exists(scriptPath, errorCode) || false == std::filesystem::is_directory(scriptPath, errorCode))
		{
			return scripts;
		}

		const std::regex scriptClassRegex(R"(SCRIPT_CLASS\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\))");
		for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(scriptPath, errorCode))
		{
			if (errorCode)
			{
				errorCode.clear();
				break;
			}
			if (false == entry.is_regular_file(errorCode))
			{
				errorCode.clear();
				continue;
			}

			const std::filesystem::path extension = entry.path().extension();
			if (extension != ".h" && extension != ".hpp")
			{
				continue;
			}

			std::ifstream file(entry.path(), std::ios::in | std::ios::binary);
			if (false == file.is_open())
			{
				continue;
			}

			std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			std::smatch match;
			if (std::regex_search(text, match, scriptClassRegex) && match.size() >= 2)
			{
				ScriptClassDesc desc;
				desc.HeaderPath = std::filesystem::relative(entry.path(), projectInfo.ContentPath, errorCode);
				if (errorCode)
				{
					errorCode.clear();
					desc.HeaderPath = entry.path().filename();
				}
				desc.ClassName = match[1].str();
				scripts.push_back(desc);
			}
		}

		return scripts;
	}
}

bool CGameScriptProjectGenerator::EnsureProject(const ProjectInfo& projectInfo) const
{
	if (projectInfo.RootPath.empty())
	{
		return false;
	}

	const std::filesystem::path contentPath = projectInfo.ContentPath;
	const std::filesystem::path scriptsPath = projectInfo.ScriptPath;

	std::error_code errorCode;
	if (contentPath.empty() || scriptsPath.empty())
	{
		return false;
	}
	if (false == std::filesystem::exists(contentPath, errorCode) || false == std::filesystem::is_directory(contentPath, errorCode))
	{
		return false;
	}
	errorCode.clear();
	if (false == std::filesystem::exists(scriptsPath, errorCode) || false == std::filesystem::is_directory(scriptsPath, errorCode))
	{
		return false;
	}

	bool succeeded = true;
	RemoveStaleGeneratedFiles(contentPath);
	const std::string projectFile = BuildProjectFile(projectInfo);
	if (projectFile.empty())
	{
		return false;
	}
	succeeded &= WriteGeneratedFile(contentPath / "GameScript.vcxproj", projectFile);
	succeeded &= WriteGeneratedFile(contentPath / "pch.h", BuildPchHeader());
	succeeded &= WriteGeneratedFile(contentPath / "pch.cpp", BuildPchSource());
	succeeded &= WriteGeneratedFile(contentPath / "GameScriptApi.h", BuildGameScriptApiHeader());
	succeeded &= WriteGeneratedFile(contentPath / "GameModuleEntry.h", BuildGameModuleEntryHeader());
	succeeded &= WriteGeneratedFile(contentPath / "GameModule.cpp", BuildGameModuleSource());
	succeeded &= WriteGeneratedFile(contentPath / "GeneratedScriptRegistry.h", BuildGeneratedRegistryHeader());
	succeeded &= WriteGeneratedFile(contentPath / "GeneratedScriptRegistry.cpp", BuildGeneratedRegistrySource(projectInfo));
	succeeded &= WriteFileIfMissing(scriptsPath / "DefaultScript.h", BuildDefaultScriptHeader());
	succeeded &= WriteFileIfMissing(scriptsPath / "DefaultScript.cpp", BuildDefaultScriptSource());
	return succeeded;
}

bool CGameScriptProjectGenerator::WriteFileIfMissing(const std::filesystem::path& path, const std::string& content) const
{
	std::error_code errorCode;
	if (std::filesystem::exists(path, errorCode))
	{
		return true;
	}
	return WriteTextFile(path, content);
}

bool CGameScriptProjectGenerator::WriteGeneratedFile(const std::filesystem::path& path, const std::string& content) const
{
	return WriteTextFile(path, content);
}

void CGameScriptProjectGenerator::RemoveStaleGeneratedFiles(const std::filesystem::path& contentPath) const
{
	const std::filesystem::path staleFiles[] =
	{
		contentPath / "GameCode.vcxproj",
		contentPath / "GameCode.vcxproj.user",
		contentPath / "GameCodeApi.h"
	};

	std::error_code errorCode;
	for (const std::filesystem::path& staleFile : staleFiles)
	{
		std::filesystem::remove(staleFile, errorCode);
		errorCode.clear();
	}
}

bool CGameScriptProjectGenerator::WriteTextFile(const std::filesystem::path& path, const std::string& content) const
{
	std::error_code errorCode;
	if (path.has_parent_path())
	{
		std::filesystem::create_directories(path.parent_path(), errorCode);
		if (errorCode)
		{
			return false;
		}
	}

	std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
	if (false == file.is_open())
	{
		return false;
	}
	file << content;
	return true;
}

std::string CGameScriptProjectGenerator::BuildProjectFile(const ProjectInfo& projectInfo) const
{
	const std::filesystem::path templatePath = ResolveProjectTemplatePath(projectInfo);
	if (templatePath.empty())
	{
		CSystemLog::Error("GameScript project template was not found.");
		return {};
	}

	std::string text;
	if (false == ReadTextFile(templatePath, text))
	{
		CSystemLog::Error("Failed to read GameScript project template: " + templatePath.string());
		return {};
	}

	const std::filesystem::path propsPath = ResolveEnginePropsPath(projectInfo);
	ReplaceAll(text, "{PROJECT_NAME}", GetProjectName(projectInfo));
	ReplaceAll(text, "{PROJECT_GUID}", FormatVisualStudioGuid(MakeStableProjectGuid(projectInfo)));
	ReplaceAll(text, "{ENGINE_PROPS}", propsPath.string());
	return text;
}

std::string CGameScriptProjectGenerator::BuildPchHeader() const
{
	return R"(#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
)";
}

std::string CGameScriptProjectGenerator::BuildPchSource() const
{
	return R"(#include "pch.h"
)";
}

std::string CGameScriptProjectGenerator::BuildGameScriptApiHeader() const
{
	return R"(#pragma once

#if defined(_WIN32) && (defined(GAMESCRIPT_EXPORTS) || defined(_USRDLL))
#define GAMESCRIPT_API __declspec(dllexport)
#else
#define GAMESCRIPT_API
#endif
)";
}

std::string CGameScriptProjectGenerator::BuildGameModuleEntryHeader() const
{
	return R"(#pragma once

#include "Core/Game/IGameModule.h"
#include "GameScriptApi.h"

extern "C" GAMESCRIPT_API IGameModule* CreateGameModule(const GameModuleHostApi* hostApi);
extern "C" GAMESCRIPT_API void DestroyGameModule(IGameModule* module, const GameModuleHostApi* hostApi);
)";
}

std::string CGameScriptProjectGenerator::BuildGameModuleSource() const
{
	return R"(#include "pch.h"

#include "Core/EngineCore.h"
#include "Core/Game/GameModuleTypes.h"
#include "GameFramework/Reflection/ReflectionRegistry.h"
#include "GameModuleEntry.h"
#include "GeneratedScriptRegistry.h"

class GameScriptModule final : public IGameModule
{
public:
	bool Initialize(const GameModuleContext& context) override
	{
		BindEngineCore(context.HostEngine);

		m_registry = Engine.Reflection.TryGet();
		if (nullptr == m_registry)
		{
			UnbindEngineCore();
			return false;
		}

		RegisterGeneratedScripts(*m_registry);
		return true;
	}

	void Tick() override
	{
	}

	void Finalize() override
	{
		if (m_registry)
		{
			UnregisterGeneratedScripts(*m_registry);
			m_registry = nullptr;
		}
		UnbindEngineCore();
	}

	const GameModuleDesc& GetDesc() const override
	{
		static const GameModuleDesc desc{ "GameScript", "1.0.0" };
		return desc;
	}

private:
	CReflectionRegistry* m_registry = nullptr;
};

extern "C" GAMESCRIPT_API
IGameModule* CreateGameModule(const GameModuleHostApi* hostApi)
{
	if (nullptr == hostApi || nullptr == hostApi->Allocate)
	{
		return nullptr;
	}

	void* memory = hostApi->Allocate(sizeof(GameScriptModule), alignof(GameScriptModule));
	return memory ? new (memory) GameScriptModule() : nullptr;
}

extern "C" GAMESCRIPT_API
void DestroyGameModule(IGameModule* module, const GameModuleHostApi* hostApi)
{
	if (nullptr == module)
	{
		return;
	}

	GameScriptModule* typedModule = static_cast<GameScriptModule*>(module);
	typedModule->~GameScriptModule();
	if (hostApi && hostApi->Free)
	{
		hostApi->Free(typedModule, sizeof(GameScriptModule), alignof(GameScriptModule));
	}
}
)";
}

std::string CGameScriptProjectGenerator::BuildGeneratedRegistryHeader() const
{
	return R"(#pragma once

class CReflectionRegistry;

void RegisterGeneratedScripts(CReflectionRegistry& registry);
void UnregisterGeneratedScripts(CReflectionRegistry& registry);
)";
}

std::string CGameScriptProjectGenerator::BuildGeneratedRegistrySource(const ProjectInfo& projectInfo) const
{
	const std::vector<ScriptClassDesc> scripts = CollectScriptClasses(projectInfo);

	std::ostringstream out;
	out << R"(#include "pch.h"
#include "GeneratedScriptRegistry.h"

#include "GameFramework/Reflection/ReflectionRegistry.h"
)";

	for (const ScriptClassDesc& script : scripts)
	{
		out << "#include \"" << ToIncludePath(script.HeaderPath) << "\"\r\n";
	}

	out << R"(

void RegisterGeneratedScripts(CReflectionRegistry& registry)
{
)";

	for (const ScriptClassDesc& script : scripts)
	{
		out << "\tregistry.RegisterScript<" << script.ClassName << ">({\r\n";
		out << "\t\t\"" << script.ClassName << "\",\r\n";
		out << "\t\t\"" << script.ClassName << "\",\r\n";
		out << "\t\t\"GameScript\"\r\n";
		out << "\t});\r\n";
	}

	out << R"(}

void UnregisterGeneratedScripts(CReflectionRegistry& registry)
{
)";

	for (const ScriptClassDesc& script : scripts)
	{
		out << "\tregistry.UnregisterScript(CReflectionRegistry::MakeTypeId(\"" << script.ClassName << "\"));\r\n";
	}

	out << R"(}
)";

	return out.str();
}

std::string CGameScriptProjectGenerator::BuildDefaultScriptHeader() const
{
	return R"(#pragma once

#include "GameFramework/Scripting/ScriptAPI.h"

class CDefaultScript final : public CGameScript
{
	SCRIPT_CLASS(CDefaultScript)

protected:
	void OnCreate() override;
	void OnStart() override;
	void OnUpdate() override;
	void OnFixedUpdate() override;
	void OnDestroy() override;
};
)";
}

std::string CGameScriptProjectGenerator::BuildDefaultScriptSource() const
{
	return R"(#include "pch.h"
#include "DefaultScript.h"

void CDefaultScript::OnCreate()
{
	if (Engine.Debug)
	{
		Engine.Debug->Log("CDefaultScript::OnCreate");
	}
}

void CDefaultScript::OnStart()
{
	if (Engine.Debug)
	{
		Engine.Debug->Log("CDefaultScript::OnStart");
	}
}

void CDefaultScript::OnUpdate()
{
}

void CDefaultScript::OnFixedUpdate()
{
}

void CDefaultScript::OnDestroy()
{
	if (Engine.Debug)
	{
		Engine.Debug->Log("CDefaultScript::OnDestroy");
	}
}
)";
}

#endif
