#pragma once

#include "Utillity/Pointer/SafePtr.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

class CFileSystem;

struct LocalizationLocaleInfo
{
	std::string Code;
	std::string DisplayName;
};

class CLocalizationManager final : public EnableSafeFromThis<CLocalizationManager>
{
public:
	bool Initialize(SafePtr<CFileSystem> fileSystem);
	void Finalize();

	bool LoadSettings();
	bool LoadLocale(const std::string& localeCode);
	bool SetCurrentLocale(const std::string& localeCode);

	const std::string& Text(const std::string& key) const;
	std::string TextOr(const std::string& key, const std::string& fallback) const;
	std::string Format(const std::string& key, const std::unordered_map<std::string, std::string>& args) const;

	const std::string& GetCurrentLocale() const;
	const std::string& GetDefaultLocale() const;
	const std::string& GetFallbackLocale() const;
	const std::vector<LocalizationLocaleInfo>& GetSupportedLocales() const;
	std::uint32_t GetRevision() const;

private:
	bool LoadLocaleFile(const std::string& localeCode, std::unordered_map<std::string, std::string>& outEntries) const;
	std::filesystem::path MakeOriginPath(const char* relativePath) const;
	bool IsSupportedLocale(const std::string& localeCode) const;

private:
	SafePtr<CFileSystem> m_fileSystem;
	std::string m_defaultLocale = "ko-KR";
	std::string m_fallbackLocale = "en-US";
	std::string m_currentLocale = "ko-KR";
	std::vector<LocalizationLocaleInfo> m_supportedLocales;
	std::unordered_map<std::string, std::string> m_entries;
	std::unordered_map<std::string, std::string> m_fallbackEntries;
	mutable std::string m_missingTextBuffer;
	std::uint32_t m_revision = 0;
};

namespace Loc
{
	const char* Text(const char* key);
	const char* TextOr(const char* key, const char* fallback);
	std::string Format(const char* key, const std::unordered_map<std::string, std::string>& args);
}
