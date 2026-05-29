#include "pch.h"
#include "AssetPath.h"

#include <algorithm>
#include <cctype>

bool CAssetPath::NormalizeRelativePath(const char* path, std::string& outPath)
{
	outPath.clear();
	if (nullptr == path || '\0' == path[0])
	{
		return false;
	}

	std::string normalized(path);
	for (char& ch : normalized)
	{
		if ('\\' == ch)
		{
			ch = '/';
		}
	}

	while (false == normalized.empty() && '/' == normalized.front())
	{
		normalized.erase(normalized.begin());
	}

	if (normalized.empty() || HasDrivePrefix(normalized))
	{
		return false;
	}

	std::vector<std::string> parts;
	std::size_t start = 0;
	while (start <= normalized.size())
	{
		const std::size_t slash = normalized.find('/', start);
		const std::size_t count = slash == std::string::npos ? std::string::npos : slash - start;
		std::string part = normalized.substr(start, count);

		if (part.empty() || part == ".")
		{
		}
		else if (part == "..")
		{
			return false;
		}
		else
		{
			parts.push_back(std::move(part));
		}

		if (slash == std::string::npos)
		{
			break;
		}
		start = slash + 1;
	}

	if (parts.empty())
	{
		return false;
	}

	for (std::size_t i = 0; i < parts.size(); ++i)
	{
		if (0 != i)
		{
			outPath.push_back('/');
		}
		outPath += parts[i];
	}

	return false == outPath.empty();
}

bool CAssetPath::IsValidRelativePath(const char* path)
{
	std::string normalized;
	return NormalizeRelativePath(path, normalized);
}

bool CAssetPath::IsAbsoluteAssetPath(const char* path)
{
	if (nullptr == path) return false;
	std::string s(path);
	for (char& ch : s) { if ('\\' == ch) ch = '/'; }
	return HasDrivePrefix(s);
}

bool CAssetPath::NormalizeAssetKey(const char* path, std::string& outPath)
{
	outPath.clear();
	if (nullptr == path || '\0' == path[0])
	{
		return false;
	}

	// 절대경로(드라이브 접두사 포함) — 슬래시 변환 + 소문자만 적용해 그대로 키로 사용.
	// 외부 path-based 자산(.Jmeta 없이 직접 등록)을 지원한다.
	if (IsAbsoluteAssetPath(path))
	{
		outPath.assign(path);
		for (char& ch : outPath) { if ('\\' == ch) ch = '/'; }
		std::transform(outPath.begin(), outPath.end(), outPath.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return false == outPath.empty();
	}

	// 상대경로 — 기존 NormalizeRelativePath 규칙.
	return NormalizeRelativePath(path, outPath);
}

std::string CAssetPath::MakeMetaPath(const char* normalizedPath)
{
	if (nullptr == normalizedPath)
	{
		return std::string();
	}

	return std::string(normalizedPath) + GetMetaExtension();
}

std::string CAssetPath::StripMetaExtension(const char* normalizedMetaPath)
{
	if (nullptr == normalizedMetaPath)
	{
		return std::string();
	}

	std::string value(normalizedMetaPath);
	const char* extensions[] = { GetMetaExtension(), GetLegacyMetaExtension() };
	for (const char* extension : extensions)
	{
		const std::size_t extensionLength = std::strlen(extension);
		if (value.size() >= extensionLength && value.compare(value.size() - extensionLength, extensionLength, extension) == 0)
		{
			value.resize(value.size() - extensionLength);
			return value;
		}
	}

	return value;
}

std::string CAssetPath::GetDisplayNameFromPath(const char* normalizedPath)
{
	if (nullptr == normalizedPath)
	{
		return std::string();
	}

	std::string path(normalizedPath);
	const std::size_t slash = path.find_last_of('/');
	if (slash != std::string::npos)
	{
		path.erase(0, slash + 1);
	}

	const std::size_t dot = path.find_last_of('.');
	if (dot != std::string::npos)
	{
		path.erase(dot);
	}

	return path;
}

AssetGuid CAssetPath::GenerateAssetGuid()
{
	return File::GenerateGuid();
}

bool CAssetPath::IsMetaPath(const char* path)
{
	if (nullptr == path)
	{
		return false;
	}

	const std::string value(path);
	const char* extensions[] = { GetMetaExtension(), GetLegacyMetaExtension() };
	for (const char* extension : extensions)
	{
		const std::size_t extensionLength = std::strlen(extension);
		if (value.size() >= extensionLength && value.compare(value.size() - extensionLength, extensionLength, extension) == 0)
		{
			return true;
		}
	}
	return false;
}

const char* CAssetPath::GetMetaExtension()
{
	return ".Jmeta";
}

const char* CAssetPath::GetLegacyMetaExtension()
{
	return ".jmeta";
}

bool CAssetPath::HasDrivePrefix(const std::string& path)
{
	return path.size() >= 2 && ':' == path[1];
}
