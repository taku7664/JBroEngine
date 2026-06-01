#include "pch.h"
#include "GameScriptProjectGenerator.h"

#include "Core/Logging/LoggerInternal.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <vector>

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

namespace
{
	// JPROP 으로 파싱된 스크립트 프로퍼티 1개.
	struct ScriptPropParse
	{
		std::string Name;
		std::string CppType;        // "Float", "Vector2", "Asset" ...
		std::string EnumType;       // "EReflectPropertyType::Float" ...
		std::string DisplayName;    // Name("..")
		std::string Tooltip;        // Tooltip("..")
		std::string Category;       // Category("..")
		bool        HasRange = false;
		std::string RangeMin;
		std::string RangeMax;
		bool        NoSerialize = false;   // JPROP(NoSerialize) — 인스펙터 노출, 씬 저장 제외
	};

	struct ScriptClassDesc
	{
		std::filesystem::path HeaderPath;
		std::string ClassName;
		std::vector<ScriptPropParse> Props;   // JPROP 멤버 (없으면 레거시 REFLECT_FIELD 경로)
	};

	// C++ 타입 토큰 → EReflectPropertyType. 미지원이면 false.
	bool MapScriptPropType(std::string cppType, std::string& outEnum)
	{
		// 공백 제거(예: "Vector2" -> "Vector2").
		cppType.erase(std::remove_if(cppType.begin(), cppType.end(),
			[](unsigned char c) { return std::isspace(c); }), cppType.end());

		if (cppType == "Bool" || cppType == "bool")                    { outEnum = "EReflectPropertyType::Bool";          return true; }
		if (cppType == "Int" || cppType == "int64_t" || cppType == "std::int64_t" || cppType == "longlong") { outEnum = "EReflectPropertyType::Int64"; return true; }
		if (cppType == "int" || cppType == "int32_t" || cppType == "std::int32_t") { outEnum = "EReflectPropertyType::Int32";  return true; }
		if (cppType == "UInt" || cppType == "uint32_t" || cppType == "std::uint32_t" || cppType == "unsignedint") { outEnum = "EReflectPropertyType::UInt32"; return true; }
		if (cppType == "Float" || cppType == "float")                  { outEnum = "EReflectPropertyType::Float";         return true; }
		if (cppType == "Degree")                                       { outEnum = "EReflectPropertyType::Degree";        return true; }
		if (cppType == "Radian")                                       { outEnum = "EReflectPropertyType::Radian";        return true; }
		if (cppType == "String")                                       { outEnum = "EReflectPropertyType::String";        return true; }
		if (cppType == "Vector2")                                      { outEnum = "EReflectPropertyType::Vector2Float";  return true; }
		if (cppType == "Rect")                                         { outEnum = "EReflectPropertyType::RectFloat";     return true; }
		if (cppType == "Asset" || cppType == "AssetGuid" || cppType == "File::Guid") { outEnum = "EReflectPropertyType::AssetGuid"; return true; }
		return false;
	}

	// JPROP(...) 어트리뷰트 인자에서 메타데이터를 추출한다.
	void ParseScriptPropAttributes(const std::string& args, ScriptPropParse& out)
	{
		// 주의: 패턴 안의 )"( 시퀀스가 기본 R"(...)" 구분자를 조기 종료시키므로
		// 커스텀 구분자 R"rx(...)rx" 를 사용한다.
		std::smatch m;
		if (std::regex_search(args, m, std::regex(R"rx(Name\s*\(\s*"([^"]*)"\s*\))rx")))      out.DisplayName = m[1].str();
		if (std::regex_search(args, m, std::regex(R"rx(Tooltip\s*\(\s*"([^"]*)"\s*\))rx")))   out.Tooltip     = m[1].str();
		if (std::regex_search(args, m, std::regex(R"rx(Category\s*\(\s*"([^"]*)"\s*\))rx")))  out.Category    = m[1].str();
		if (std::regex_search(args, m, std::regex(R"rx(Range\s*\(\s*([-0-9.eEfF]+)\s*,\s*([-0-9.eEfF]+)\s*\))rx")))
		{
			out.HasRange = true;
			out.RangeMin = m[1].str();
			out.RangeMax = m[2].str();
		}
		if (std::regex_search(args, m, std::regex(R"rx(\bNoSerialize\b)rx")))
		{
			out.NoSerialize = true;
		}
	}

	// C++ 문자열 리터럴용 이스케이프(따옴표/백슬래시). UTF-8 그대로 보존.
	std::string EscapeCppString(const std::string& s)
	{
		std::string out;
		out.reserve(s.size() + 2);
		for (char c : s)
		{
			if (c == '\\' || c == '"') out.push_back('\\');
			out.push_back(c);
		}
		return out;
	}

	// JPROP(...) 어트리뷰트 인자에서 알 수 없는(오타) 어트리뷰트를 로그로 경고한다.
	// 빌드당 1회만 도는 codegen 경로라 비용은 사실상 0.
	void WarnUnknownJpropAttributes(const std::string& args, const std::string& className, const std::string& propName)
	{
		// 문자열 리터럴 내용 제거 — 그 안의 식별자(예: 한글 라벨)를 어트리뷰트로 오인하지 않게.
		std::string stripped;
		stripped.reserve(args.size());
		bool inString = false;
		for (char c : args)
		{
			if (c == '"') { inString = !inString; continue; }
			if (false == inString) stripped.push_back(c);
		}

		static const char* const kKnown[] = { "Name", "Tooltip", "Category", "Range", "NoSerialize" };
		const std::regex identRegex(R"([A-Za-z_]\w*)");
		for (auto it = std::sregex_iterator(stripped.begin(), stripped.end(), identRegex);
			it != std::sregex_iterator(); ++it)
		{
			const std::string token = it->str();
			bool known = false;
			for (const char* k : kKnown) { if (token == k) { known = true; break; } }
			if (false == known)
			{
				CSystemLog::Warning("[JPROP] " + className + "." + propName
					+ ": unknown attribute '" + token + "' (ignored). Available: Name, Tooltip, Category, Range, NoSerialize.");
			}
		}
	}

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

		// "JBRO_SCRIPT <ClassName> [final] (':' 또는 '{')" 형태를 정의로 인정한다.
		// 주의: C++ 의 'final' 은 클래스명 "뒤"에 온다(JBRO_SCRIPT Foo final : ...).
		// (예전 정규식은 final 을 이름 "앞"에서 찾아, final 을 쓰는 모든 스크립트가
		//  스캔에서 누락 → 레지스트리가 비어 "등록된 스크립트 없음" 이 떴다.)
		// forward declaration("JBRO_SCRIPT Foo;") 은 ';' 라 자연스럽게 제외된다.
		const std::regex scriptClassRegex(
			R"(\bJBRO_SCRIPT\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:final\b\s*)?(?::|\{))");
		for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(scriptPath, errorCode))
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

			const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

			// 이 파일의 클래스 정의 위치들을 수집한다(여러 클래스 가능).
			struct LocalClass { std::size_t Pos; ScriptClassDesc Desc; };
			std::vector<LocalClass> localClasses;
			for (auto it = std::sregex_iterator(text.begin(), text.end(), scriptClassRegex);
				it != std::sregex_iterator(); ++it)
			{
				LocalClass lc;
				lc.Pos = static_cast<std::size_t>(it->position(0));
				lc.Desc.HeaderPath = std::filesystem::relative(entry.path(), projectInfo.ContentPath, errorCode);
				if (errorCode) { errorCode.clear(); lc.Desc.HeaderPath = entry.path().filename(); }
				lc.Desc.ClassName = (*it)[1].str();
				localClasses.push_back(std::move(lc));
			}

			// JPROP(<attrs>) <type> <name> [= default] ;  — attrs 는 한 단계 중첩 괄호 허용
			// (예: Range(0,100)). 각 JPROP 은 바로 앞에 선언된 클래스에 귀속시킨다.
			static const std::regex jpropRegex(
				R"(\bJPROP\s*\(((?:[^()]|\([^()]*\))*)\)\s*([A-Za-z_][A-Za-z0-9_:]*(?:\s*<[^>]*>)?)\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:=\s*[^;]*?)?\s*;)");
			const std::string fileName = entry.path().filename().generic_string();

			// 파싱 성공/실패를 비교하기 위해 파일 내 JPROP 마커 총 개수를 센다.
			// (sregex_iterator 는 regex 포인터를 보관하므로 임시 regex 금지 — named 사용.)
			static const std::regex jpropMarkerRegex(R"(\bJPROP\s*\()");
			std::size_t markerCount = 0;
			for (auto it = std::sregex_iterator(text.begin(), text.end(), jpropMarkerRegex);
				it != std::sregex_iterator(); ++it) { ++markerCount; }

			std::size_t matchedCount = 0;
			for (auto it = std::sregex_iterator(text.begin(), text.end(), jpropRegex);
				it != std::sregex_iterator(); ++it)
			{
				++matchedCount;
				const std::size_t pos = static_cast<std::size_t>(it->position(0));
				ScriptPropParse prop;
				prop.CppType = (*it)[2].str();
				prop.Name    = (*it)[3].str();

				// 귀속 클래스를 먼저 찾는다(로그 메시지에 클래스명 포함).
				LocalClass* owner = nullptr;
				for (LocalClass& lc : localClasses)
				{
					if (lc.Pos < pos && (nullptr == owner || lc.Pos > owner->Pos)) owner = &lc;
				}
				const std::string ownerName = owner ? owner->Desc.ClassName : std::string("<global>");

				// 어트리뷰트 오타 검출.
				WarnUnknownJpropAttributes((*it)[1].str(), ownerName, prop.Name);

				// 미지원 타입 — 등록 제외 + 경고.
				if (false == MapScriptPropType(prop.CppType, prop.EnumType))
				{
					CSystemLog::Warning("[JPROP] " + ownerName + "." + prop.Name
						+ ": unsupported type '" + prop.CppType + "' - excluded. Supported: Bool, Int, UInt, Float, Degree, Radian, String, Vector2, Rect, Asset.");
					continue;
				}
				ParseScriptPropAttributes((*it)[1].str(), prop);

				if (nullptr == owner)
				{
					CSystemLog::Warning("[JPROP] '" + prop.Name + "': not inside any JBRO_SCRIPT class - excluded.");
					continue;
				}

				// 같은 클래스 내 이름 중복 검출 — 중복 등록을 막는다.
				bool duplicate = false;
				for (const ScriptPropParse& existing : owner->Desc.Props)
				{
					if (existing.Name == prop.Name) { duplicate = true; break; }
				}
				if (duplicate)
				{
					CSystemLog::Warning("[JPROP] " + ownerName + ": duplicate property name '" + prop.Name + "' - second one excluded.");
					continue;
				}

				owner->Desc.Props.push_back(std::move(prop));
			}

			// JPROP 마커는 있는데 파싱 못 한 게 있으면(문법 오류 등) 경고.
			if (matchedCount < markerCount)
			{
				CSystemLog::Warning("[JPROP] " + fileName + ": failed to parse "
					+ std::to_string(markerCount - matchedCount) + " JPROP marker(s) - check syntax: JPROP(attrs) <type> <name>;");
			}

			for (LocalClass& lc : localClasses)
			{
				scripts.push_back(std::move(lc.Desc));
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

	// DefaultScript 가 디스크에 존재해야 BuildProjectFile 의 Scripts 폴더 스캔이
	// 그것을 vcxproj 에 포함시킨다.  따라서 스크립트 템플릿을 먼저 만들고
	// 그 다음에 vcxproj/sln/registry 를 생성한다.
	succeeded &= WriteFileIfMissing(scriptsPath / "DefaultScript.h", BuildDefaultScriptHeader());
	succeeded &= WriteFileIfMissing(scriptsPath / "DefaultScript.cpp", BuildDefaultScriptSource());

	const std::string projectFile = BuildProjectFile(projectInfo);
	if (projectFile.empty())
	{
		return false;
	}
	succeeded &= WriteGeneratedFile(contentPath / "GameScript.vcxproj", projectFile);
	// .sln 도 함께 생성해 두면 VS 가 "임시 솔루션 저장하시겠습니까?" 를 묻지 않는다.
	// 사용자가 GameScript.vcxproj 대신 GameScript.sln 을 열면 명시적 솔루션으로 working.
	succeeded &= WriteGeneratedFile(contentPath / "GameScript.sln", BuildSolutionFile(projectInfo));
	succeeded &= WriteGeneratedFile(contentPath / "pch.h", BuildPchHeader());
	succeeded &= WriteGeneratedFile(contentPath / "pch.cpp", BuildPchSource());
	succeeded &= WriteGeneratedFile(contentPath / "GameScriptApi.h", BuildGameScriptApiHeader());
	succeeded &= WriteGeneratedFile(contentPath / "GameModuleEntry.h", BuildGameModuleEntryHeader());
	succeeded &= WriteGeneratedFile(contentPath / "GameModule.cpp", BuildGameModuleSource());
	succeeded &= WriteGeneratedFile(contentPath / "GeneratedScriptRegistry.h", BuildGeneratedRegistryHeader());
	succeeded &= WriteGeneratedFile(contentPath / "GeneratedScriptRegistry.cpp", BuildGeneratedRegistrySource(projectInfo));
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
	// 내용이 이미 같으면 다시 쓰지 않는다 — 파일 수정시각(mtime)을 보존해
	// MSBuild 의 불필요한 재컴파일을 막는다. (매 빌드 직전 재생성을 해도, 실제로
	// 바뀐 파일만 갱신되므로 증분 빌드가 빠르게 유지된다.)
	std::error_code errorCode;
	if (std::filesystem::exists(path, errorCode))
	{
		std::ifstream existing(path, std::ios::in | std::ios::binary);
		if (existing.is_open())
		{
			const std::string current((std::istreambuf_iterator<char>(existing)), std::istreambuf_iterator<char>());
			if (current == content)
			{
				return true;   // 변경 없음 — 쓰기 생략.
			}
		}
	}
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

	// ── Scripts/ 폴더 스캔 후 .cpp / .h 목록을 명시적으로 삽입 ────────────────
	// MSBuild 가 wildcard Include 를 경고하므로 (VC 프로젝트 형식에서는 비지원),
	// EnsureProject 호출 시점에 실제 파일을 나열해 vcxproj 에 박아 넣는다.
	std::ostringstream cppList, hList;
	if (false == projectInfo.ScriptPath.empty())
	{
		std::error_code errorCode;
		if (std::filesystem::exists(projectInfo.ScriptPath, errorCode))
		{
			std::vector<std::filesystem::path> cppFiles;
			std::vector<std::filesystem::path> hFiles;
			for (const std::filesystem::directory_entry& entry
			     : std::filesystem::recursive_directory_iterator(projectInfo.ScriptPath, errorCode))
			{
				if (errorCode) { errorCode.clear(); break; }
				if (false == entry.is_regular_file(errorCode)) { errorCode.clear(); continue; }
				const std::filesystem::path ext = entry.path().extension();
				std::filesystem::path rel = std::filesystem::relative(
					entry.path(), projectInfo.ContentPath, errorCode);
				if (errorCode) { errorCode.clear(); rel = entry.path().filename(); }
				if      (ext == ".cpp" || ext == ".cc") cppFiles.push_back(std::move(rel));
				else if (ext == ".h" || ext == ".hpp")  hFiles.push_back(std::move(rel));
			}
			auto pathLess = [](const std::filesystem::path& a, const std::filesystem::path& b)
				{ return a.generic_string() < b.generic_string(); };
			std::sort(cppFiles.begin(), cppFiles.end(), pathLess);
			std::sort(hFiles.begin(),   hFiles.end(),   pathLess);

			for (const std::filesystem::path& p : cppFiles)
			{
				// vcxproj 는 백슬래시 경로 선호.
				std::string s = p.generic_string();
				for (char& c : s) if (c == '/') c = '\\';
				cppList << "    <ClCompile Include=\"" << s << "\" />\r\n";
			}
			for (const std::filesystem::path& p : hFiles)
			{
				std::string s = p.generic_string();
				for (char& c : s) if (c == '/') c = '\\';
				hList << "    <ClInclude Include=\"" << s << "\" />\r\n";
			}
		}
	}
	ReplaceAll(text, "{SCRIPT_CPP_FILES}", cppList.str());
	ReplaceAll(text, "{SCRIPT_H_FILES}",   hList.str());
	return text;
}

std::string CGameScriptProjectGenerator::BuildSolutionFile(const ProjectInfo& projectInfo) const
{
	// C++ 프로젝트 타입 GUID — VS 가 .vcxproj 를 인식할 때 쓰는 고정값.
	constexpr const char* CPP_PROJECT_TYPE_GUID = "{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}";
	const std::string projectGuid = FormatVisualStudioGuid(MakeStableProjectGuid(projectInfo));
	const std::string projectName = GetProjectName(projectInfo);

	std::ostringstream out;
	out << "Microsoft Visual Studio Solution File, Format Version 12.00\r\n";
	out << "# Visual Studio Version 17\r\n";
	out << "Project(\"" << CPP_PROJECT_TYPE_GUID << "\") = \""
	    << projectName << "\", \"GameScript.vcxproj\", \"" << projectGuid << "\"\r\n";
	out << "EndProject\r\n";
	out << "Global\r\n";
	out << "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\r\n";
	out << "\t\tDebug|x64 = Debug|x64\r\n";
	out << "\t\tRelease|x64 = Release|x64\r\n";
	out << "\tEndGlobalSection\r\n";
	out << "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\r\n";
	out << "\t\t" << projectGuid << ".Debug|x64.ActiveCfg = Debug|x64\r\n";
	out << "\t\t" << projectGuid << ".Debug|x64.Build.0 = Debug|x64\r\n";
	out << "\t\t" << projectGuid << ".Release|x64.ActiveCfg = Release|x64\r\n";
	out << "\t\t" << projectGuid << ".Release|x64.Build.0 = Release|x64\r\n";
	out << "\tEndGlobalSection\r\n";
	out << "\tGlobalSection(SolutionProperties) = preSolution\r\n";
	out << "\t\tHideSolutionNode = FALSE\r\n";
	out << "\tEndGlobalSection\r\n";
	out << "EndGlobal\r\n";
	return out.str();
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

#include <cstddef>
#include <vector>
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
		if (script.Props.empty())
		{
			// 레거시(REFLECT_FIELD 또는 프로퍼티 없음) — GetReflectEntries 경로로 등록.
			out << "\tregistry.RegisterScript<" << script.ClassName << ">({ \""
				<< script.ClassName << "\", \"" << script.ClassName << "\", \"GameScript\" });\r\n";
			continue;
		}

		// JPROP 방식 — 명시적 프로퍼티 목록(offset/메타데이터)으로 등록.
		out << "\tregistry.RegisterScript<" << script.ClassName << ">(\r\n";
		out << "\t\tScriptRegisterDesc{ \"" << script.ClassName << "\", \"" << script.ClassName << "\", \"GameScript\" },\r\n";
		out << "\t\tstd::vector<ScriptPropertyDesc>{\r\n";
		for (const ScriptPropParse& p : script.Props)
		{
			const std::string display  = p.DisplayName.empty() ? std::string("nullptr") : ("\"" + EscapeCppString(p.DisplayName) + "\"");
			const std::string tooltip  = p.Tooltip.empty()     ? std::string("nullptr") : ("\"" + EscapeCppString(p.Tooltip) + "\"");
			const std::string category = p.Category.empty()    ? std::string("nullptr") : ("\"" + EscapeCppString(p.Category) + "\"");
			const std::string hasRange = p.HasRange ? "true" : "false";
			const std::string rmin     = p.HasRange ? p.RangeMin : "0.0f";
			const std::string rmax     = p.HasRange ? p.RangeMax : "0.0f";
			const std::string serialize = p.NoSerialize ? "false" : "true";
			out << "\t\t\tScriptPropertyDesc{ \"" << p.Name << "\", " << p.EnumType
				<< ", offsetof(" << script.ClassName << ", " << p.Name << ")"
				<< ", sizeof(" << p.CppType << "), 1, "
				<< display << ", " << tooltip << ", " << category << ", "
				<< hasRange << ", static_cast<float>(" << rmin << "), static_cast<float>(" << rmax << ")"
				<< ", " << serialize << " },\r\n";
		}
		out << "\t\t});\r\n";
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

JBRO_SCRIPT CDefaultScript final : public CGameScript
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
