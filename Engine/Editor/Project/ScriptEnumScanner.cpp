#include "pch.h"
#include "Editor/Project/ScriptEnumScanner.h"

#include "Core/Logging/LoggerInternal.h"

#include <cctype>
#include <fstream>
#include <iterator>
#include <regex>

namespace
{
	// 주석을 지운다. enum 본문을 쉼표로 가르기 전에 필요하다 —
	// `Idle,  // 대기, 정지` 처럼 주석 안에 쉼표가 있으면 없는 열거자가 하나 생긴다.
	std::string StripComments(const std::string& text)
	{
		std::string out;
		out.reserve(text.size());
		for (std::size_t i = 0; i < text.size(); ++i)
		{
			if ('/' == text[i] && i + 1 < text.size() && '/' == text[i + 1])
			{
				while (i < text.size() && '\n' != text[i])
				{
					++i;
				}
				out.push_back('\n');
				continue;
			}
			if ('/' == text[i] && i + 1 < text.size() && '*' == text[i + 1])
			{
				i += 2;
				while (i + 1 < text.size() && !('*' == text[i] && '/' == text[i + 1]))
				{
					++i;
				}
				++i;
				out.push_back(' ');
				continue;
			}
			out.push_back(text[i]);
		}
		return out;
	}

	// enum 본문을 깊이 0 의 쉼표로 가르고 각 조각의 선행 식별자만 취한다
	// (`Run = 1 << 3` → "Run").
	std::vector<std::string> ParseEnumerators(const std::string& body)
	{
		std::vector<std::string> names;
		std::string token;
		int depth = 0;

		const auto flush = [&]()
		{
			std::string name;
			for (char c : token)
			{
				if (std::isalnum(static_cast<unsigned char>(c)) || '_' == c)
				{
					name.push_back(c);
					continue;
				}
				if (false == name.empty())
				{
					break;
				}
			}
			if (false == name.empty())
			{
				names.push_back(name);
			}
			token.clear();
		};

		for (char c : body)
		{
			if ('(' == c || '<' == c)
			{
				++depth;
			}
			else if (')' == c || '>' == c)
			{
				--depth;
			}
			if (',' == c && 0 == depth)
			{
				flush();
				continue;
			}
			token.push_back(c);
		}
		flush();
		return names;
	}
}

std::vector<ScriptEnumInfo> ScanScriptEnums(const std::filesystem::path& scriptRoot)
{
	std::vector<ScriptEnumInfo> enums;
	if (scriptRoot.empty())
	{
		return enums;
	}

	std::error_code errorCode;
	if (false == std::filesystem::exists(scriptRoot, errorCode)
		|| false == std::filesystem::is_directory(scriptRoot, errorCode))
	{
		return enums;
	}

	static const std::regex enumRegex(
		R"(\benum\s+class\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?::\s*[A-Za-z_][A-Za-z0-9_:\s]*)?\s*\{([^}]*)\})");

	errorCode.clear();
	for (const std::filesystem::directory_entry& entry
	     : std::filesystem::recursive_directory_iterator(scriptRoot, errorCode))
	{
		if (errorCode)
		{
			// 한 항목 오류로 전체 스캔을 중단하지 않는다(잠긴/권한거부 파일 건너뜀).
			errorCode.clear();
			continue;
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

		const std::string raw((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		const std::string text = StripComments(raw);

		for (auto it = std::sregex_iterator(text.begin(), text.end(), enumRegex);
			it != std::sregex_iterator(); ++it)
		{
			ScriptEnumInfo info;
			info.ClassName = (*it)[1].str();

			bool alreadyKnown = false;
			for (const ScriptEnumInfo& existing : enums)
			{
				if (existing.ClassName == info.ClassName)
				{
					alreadyKnown = true;
					break;
				}
			}
			if (alreadyKnown)
			{
				CSystemLog::Warning("[JPROP] duplicate enum class '" + info.ClassName
					+ "' - the first one is used for property metadata.");
				continue;
			}

			info.Enumerators = ParseEnumerators((*it)[2].str());
			if (info.Enumerators.empty())
			{
				continue;   // 빈 enum — 등록할 이름표가 없다.
			}
			enums.push_back(std::move(info));
		}
	}

	return enums;
}
