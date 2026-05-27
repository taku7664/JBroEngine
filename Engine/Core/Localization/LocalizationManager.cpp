#include "pch.h"
#include "LocalizationManager.h"

#include "Core/Core.h"
#include "Core/FileSystem/FileSystem.h"
#include "yaml-cpp/yaml.h"

namespace
{
	constexpr const char* LOCALIZATION_SETTINGS_PATH = "Localization/LocalizationSettings.yaml";
	constexpr const char* LOCALIZATION_DIRECTORY = "Localization";

	const std::string EMPTY_TEXT;

	std::filesystem::path MakeLocalePath(const std::string& localeCode)
	{
		std::filesystem::path path(LOCALIZATION_DIRECTORY);
		path /= localeCode + ".yaml";
		return path;
	}
}

bool CLocalizationManager::Initialize(SafePtr<CFileSystem> fileSystem)
{
	m_fileSystem = fileSystem;
	LoadSettings();
	m_currentLocale = m_defaultLocale;

	return LoadLocale(m_currentLocale);
}

void CLocalizationManager::Finalize()
{
	m_entries.clear();
	m_fallbackEntries.clear();
	m_supportedLocales.clear();
	m_fileSystem = nullptr;
	m_currentLocale = m_defaultLocale;
	++m_revision;
}

bool CLocalizationManager::LoadSettings()
{
	m_defaultLocale = "ko-KR";
	m_fallbackLocale = "en-US";
	m_supportedLocales.clear();

	const std::filesystem::path path = MakeOriginPath(LOCALIZATION_SETTINGS_PATH);
	YAML::Node root;
	try
	{
		root = YAML::LoadFile(path.string());
	}
	catch (const YAML::Exception&)
	{
		m_supportedLocales.push_back({ "ko-KR", "한국어" });
		m_supportedLocales.push_back({ "en-US", "English" });
		return false;
	}

	if (root["DefaultLocale"])
	{
		m_defaultLocale = root["DefaultLocale"].as<std::string>("ko-KR");
	}
	if (root["FallbackLocale"])
	{
		m_fallbackLocale = root["FallbackLocale"].as<std::string>("en-US");
	}
	if (root["SupportedLocales"] && root["SupportedLocales"].IsSequence())
	{
		for (const YAML::Node& localeNode : root["SupportedLocales"])
		{
			LocalizationLocaleInfo locale;
			locale.Code = localeNode["Code"].as<std::string>("");
			locale.DisplayName = localeNode["DisplayName"].as<std::string>(locale.Code);
			if (false == locale.Code.empty())
			{
				m_supportedLocales.push_back(std::move(locale));
			}
		}
	}

	if (m_supportedLocales.empty())
	{
		m_supportedLocales.push_back({ m_defaultLocale, m_defaultLocale });
	}
	return true;
}

bool CLocalizationManager::LoadLocale(const std::string& localeCode)
{
	std::unordered_map<std::string, std::string> nextEntries;
	std::unordered_map<std::string, std::string> nextFallbackEntries;

	const bool loadedCurrent = LoadLocaleFile(localeCode, nextEntries);
	const bool needsFallback = localeCode != m_fallbackLocale;
	const bool loadedFallback = needsFallback ? LoadLocaleFile(m_fallbackLocale, nextFallbackEntries) : true;
	if (false == loadedCurrent && false == loadedFallback)
	{
		return false;
	}

	m_currentLocale = localeCode;
	m_entries = std::move(nextEntries);
	m_fallbackEntries = needsFallback ? std::move(nextFallbackEntries) : m_entries;
	++m_revision;
	return true;
}

bool CLocalizationManager::SetCurrentLocale(const std::string& localeCode)
{
	if (localeCode == m_currentLocale)
	{
		return true;
	}
	if (false == IsSupportedLocale(localeCode))
	{
		return false;
	}
	if (false == LoadLocale(localeCode))
	{
		return false;
	}
	return true;
}

const std::string& CLocalizationManager::Text(const std::string& key) const
{
	const auto iter = m_entries.find(key);
	if (iter != m_entries.end())
	{
		return iter->second;
	}

	const auto fallbackIter = m_fallbackEntries.find(key);
	if (fallbackIter != m_fallbackEntries.end())
	{
		return fallbackIter->second;
	}

	m_missingTextBuffer = "!!" + key + "!!";
	return m_missingTextBuffer;
}

std::string CLocalizationManager::TextOr(const std::string& key, const std::string& fallback) const
{
	const auto iter = m_entries.find(key);
	if (iter != m_entries.end())
	{
		return iter->second;
	}

	const auto fallbackIter = m_fallbackEntries.find(key);
	if (fallbackIter != m_fallbackEntries.end())
	{
		return fallbackIter->second;
	}

	return fallback;
}

std::string CLocalizationManager::Format(const std::string& key, const std::unordered_map<std::string, std::string>& args) const
{
	std::string result = Text(key);
	for (const auto& [argKey, argValue] : args)
	{
		const std::string token = "{" + argKey + "}";
		std::size_t pos = 0;
		while ((pos = result.find(token, pos)) != std::string::npos)
		{
			result.replace(pos, token.length(), argValue);
			pos += argValue.length();
		}
	}
	return result;
}

const std::string& CLocalizationManager::GetCurrentLocale() const
{
	return m_currentLocale;
}

const std::string& CLocalizationManager::GetDefaultLocale() const
{
	return m_defaultLocale;
}

const std::string& CLocalizationManager::GetFallbackLocale() const
{
	return m_fallbackLocale;
}

const std::vector<LocalizationLocaleInfo>& CLocalizationManager::GetSupportedLocales() const
{
	return m_supportedLocales;
}

std::uint32_t CLocalizationManager::GetRevision() const
{
	return m_revision;
}

bool CLocalizationManager::LoadLocaleFile(const std::string& localeCode, std::unordered_map<std::string, std::string>& outEntries) const
{
	outEntries.clear();
	const std::filesystem::path path = MakeOriginPath(MakeLocalePath(localeCode).generic_string().c_str());
	YAML::Node root;
	try
	{
		root = YAML::LoadFile(path.string());
	}
	catch (const YAML::Exception&)
	{
		return false;
	}

	const YAML::Node entries = root["Entries"];
	if (false == entries.IsDefined() || false == entries.IsMap())
	{
		return false;
	}

	for (const auto& entry : entries)
	{
		outEntries.emplace(
			entry.first.as<std::string>(""),
			entry.second.as<std::string>(""));
	}
	return true;
}

std::filesystem::path CLocalizationManager::MakeOriginPath(const char* relativePath) const
{
	std::filesystem::path path = m_fileSystem.IsValid()
		? std::filesystem::path(m_fileSystem->GetOriginPath())
		: std::filesystem::current_path();
	path /= std::filesystem::path(relativePath ? relativePath : "");
	return path;
}

bool CLocalizationManager::IsSupportedLocale(const std::string& localeCode) const
{
	return std::any_of(
		m_supportedLocales.begin(),
		m_supportedLocales.end(),
		[&](const LocalizationLocaleInfo& locale) { return locale.Code == localeCode; });
}

const char* Loc::Text(const char* key)
{
	if (Core::Localization.IsValid())
	{
		return Core::Localization->Text(key ? key : "").c_str();
	}
	return key ? key : "";
}

const char* Loc::TextOr(const char* key, const char* fallback)
{
	static thread_local std::string result;
	if (Core::Localization.IsValid())
	{
		result = Core::Localization->TextOr(key ? key : "", fallback ? fallback : "");
		return result.c_str();
	}
	return fallback ? fallback : "";
}

std::string Loc::Format(const char* key, const std::unordered_map<std::string, std::string>& args)
{
	if (Core::Localization.IsValid())
	{
		return Core::Localization->Format(key ? key : "", args);
	}
	return EMPTY_TEXT;
}
