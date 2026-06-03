#include "pch.h"
#include "ScriptSchema.h"

#include "Engine/Core/Core.h"
#include "Engine/GameFramework/Reflection/ReflectionRegistry.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_set>

namespace ScriptSchema
{
	namespace
	{
		// 1차 콤보 토큰. Int/UInt 는 64비트(향후 32비트는 Int32/UInt32 명시).
		const std::vector<std::string> kBaseTypes = {
			"Bool", "Int", "UInt", "Float", "Degree", "Radian",
			"String", "Vector2", "Rect",
			"Ref<GameObject>", "Ref<Component>", "Ref<Asset>",
		};

		// 스크립트에서 참조 가능한 엔진 에셋 타입. 라벨(친숙명) / 클래스명(C 접두) / 헤더.
		struct AssetRefType { const char* Label; const char* TypeName; const char* Include; };
		constexpr std::array<AssetRefType, 4> kAssetTypes = {{
			{ "SpriteAsset",   "CSpriteAsset",   "Core/Asset/SpriteAsset.h"   },
			{ "AudioAsset",    "CAudioAsset",    "Core/Asset/AudioAsset.h"    },
			{ "MaterialAsset", "CMaterialAsset", "Core/Asset/MaterialAsset.h" },
			{ "FileAsset",     "CFileAsset",     "Core/Asset/FileAsset.h"     },
		}};

		// 컴포넌트/스크립트 타입명 → 헤더 경로(리플렉션으로 스크립트 여부 판정).
		std::string IncludeForComponentOrScript(const std::string& typeName)
		{
			const bool isScript = Core::Reflection.IsValid()
				&& nullptr != Core::Reflection->FindScriptByName(typeName.c_str());
			return isScript
				? ("Scripts/" + typeName + ".h")
				: ("GameFramework/Component/" + typeName + ".h");
		}

		// 에셋 타입명이면 그 헤더, 아니면 "".
		const char* AssetIncludeFor(const std::string& typeName)
		{
			for (const AssetRefType& a : kAssetTypes)
			{
				if (typeName == a.TypeName) return a.Include;
			}
			return nullptr;
		}

		// Ref<X> 의 X 추출(공백 제거). Ref 가 아니면 "".
		std::string ExtractRefInner(const std::string& cppType)
		{
			const std::size_t lt = cppType.find('<');
			const std::size_t gt = cppType.rfind('>');
			if (lt == std::string::npos || gt == std::string::npos || gt <= lt + 1) return "";
			std::string inner = cppType.substr(lt + 1, gt - lt - 1);
			std::string out;
			for (char c : inner) { if (false == std::isspace(static_cast<unsigned char>(c))) out.push_back(c); }
			return out;
		}

		// 줄 시작/끝(개행 포함) 오프셋.
		std::size_t LineStart(const std::string& s, std::size_t pos)
		{
			const std::size_t nl = s.rfind('\n', pos == 0 ? 0 : pos - 1);
			return (nl == std::string::npos) ? 0 : nl + 1;
		}
		std::size_t LineEndIncl(const std::string& s, std::size_t pos)
		{
			const std::size_t nl = s.find('\n', pos);
			return (nl == std::string::npos) ? s.size() : nl + 1;
		}

		// JPROP(<attrs>) <type> <name> [= default] ;  — attrs 한 단계 중첩 괄호 허용.
		const std::regex& JpropRegex()
		{
			static const std::regex re(
				R"(\bJPROP\s*\(((?:[^()]|\([^()]*\))*)\)\s*([A-Za-z_][A-Za-z0-9_:]*(?:\s*<[^>]*>)?)\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:=\s*[^;]*?)?\s*;)");
			return re;
		}
		const std::regex& ScriptClassRegex()
		{
			static const std::regex re(
				R"(\bJBRO_SCRIPT\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:final\b\s*)?(?::|\{))");
			return re;
		}
	}

	const std::vector<std::string>& BaseTypes() { return kBaseTypes; }

	std::vector<RefTargetInfo> ComponentTargets()
	{
		std::vector<RefTargetInfo> out;
		if (Core::Reflection.IsValid())
		{
			for (std::size_t i = 0; i < Core::Reflection->GetComponentTypeCount(); ++i)
			{
				const ComponentTypeInfo* ti = Core::Reflection->GetComponentType(i);
				if (nullptr == ti || nullptr == ti->Type.Name) continue;
				const char* n = ti->Type.Name;
				if (0 == std::strcmp(n, "GameObject")) continue;            // Ref<GameObject> 로 분리
				if (0 == std::strcmp(n, "TransformHierarchy2D")) continue;  // 내부용
				if (0 == std::strcmp(n, "ScriptComponent")) continue;       // 컨테이너
				out.push_back({ n, n, std::string("GameFramework/Component/") + n + ".h" });
			}
			for (std::size_t i = 0; i < Core::Reflection->GetScriptTypeCount(); ++i)
			{
				const ScriptTypeInfo* si = Core::Reflection->GetScriptType(i);
				if (nullptr == si || nullptr == si->Type.Name) continue;
				const char* n = si->Type.Name;
				out.push_back({ n, n, std::string("Scripts/") + n + ".h" });
			}
		}
		return out;
	}

	std::vector<RefTargetInfo> AssetTargets()
	{
		std::vector<RefTargetInfo> out;
		for (const AssetRefType& a : kAssetTypes)
		{
			out.push_back({ a.Label, a.TypeName, a.Include });
		}
		return out;
	}

	bool IsRefToken(const std::string& t)        { return 0 == t.rfind("Ref<", 0); }
	bool NeedsTargetCombo(const std::string& t)  { return t == "Ref<Component>" || t == "Ref<Asset>"; }

	void ResetRefTargetForToken(Property& p)
	{
		if (p.TypeToken == "Ref<GameObject>")
		{
			p.RefTarget = "GameObject"; p.RefInclude = "";
			return;
		}
		const std::vector<RefTargetInfo> targets =
			(p.TypeToken == "Ref<Asset>") ? AssetTargets() : ComponentTargets();
		if (targets.empty()) { p.RefTarget = ""; p.RefInclude = ""; return; }
		p.RefTarget  = targets.front().TypeName;
		p.RefInclude = targets.front().Include;
	}

	std::string FinalTypeToken(const Property& p)
	{
		if (p.TypeToken == "Ref<GameObject>") return "Ref<GameObject>";
		if (NeedsTargetCombo(p.TypeToken))    return "Ref<" + p.RefTarget + ">";
		return p.TypeToken;
	}

	std::string DefaultValueForToken(const std::string& finalToken)
	{
		if (finalToken == "Bool")   return "false";
		if (finalToken == "Int")    return "0";
		if (finalToken == "UInt")   return "0u";
		if (finalToken == "Float")  return "0.0f";
		if (finalToken == "Degree") return "Degree(0.0f)";
		if (finalToken == "Radian") return "Radian(0.0f)";
		if (finalToken == "String") return "\"\"";
		return "{}";   // Vector2 / Rect / Ref<...>
	}

	namespace
	{
		// double → 간결한 C++ 부동소수 리터럴(불필요한 0 제거, 'f' 접미).
		std::string FormatRangeNumber(double v)
		{
			char buf[64];
			std::snprintf(buf, sizeof(buf), "%g", v);
			std::string s = buf;
			if (s.find('.') == std::string::npos && s.find('e') == std::string::npos
				&& s.find("inf") == std::string::npos && s.find("nan") == std::string::npos)
			{
				s += ".0";
			}
			return s + "f";
		}
	}

	std::string FormatJpropLine(const Property& p)
	{
		// 어트리뷰트 수집(코드젠이 아는 표준 명칭: Name/Tooltip/Category/Range/NoSerialize).
		std::vector<std::string> attrs;
		if (false == p.DisplayName.empty()) attrs.push_back("Name(\"" + p.DisplayName + "\")");
		if (false == p.Tooltip.empty())  attrs.push_back("Tooltip(\"" + p.Tooltip + "\")");
		if (false == p.Category.empty()) attrs.push_back("Category(\"" + p.Category + "\")");
		if (p.HasRange)
		{
			attrs.push_back("Range(" + FormatRangeNumber(p.RangeMin) + ", " + FormatRangeNumber(p.RangeMax) + ")");
		}
		if (p.NoSerialize) attrs.push_back("NoSerialize");

		std::string attrStr;
		for (std::size_t i = 0; i < attrs.size(); ++i)
		{
			if (i) attrStr += ", ";
			attrStr += attrs[i];
		}

		const std::string token = FinalTypeToken(p);
		return "\tJPROP(" + attrStr + ") " + token + " " + p.Name
		     + " = " + DefaultValueForToken(token) + ";";
	}

	bool ContainsCaseInsensitive(const std::string& haystack, const char* needle)
	{
		if (nullptr == needle || '\0' == needle[0]) return true;
		auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
		std::string h; h.reserve(haystack.size());
		for (char c : haystack) h.push_back(lower(static_cast<unsigned char>(c)));
		std::string n;
		for (const char* p = needle; *p; ++p) n.push_back(lower(static_cast<unsigned char>(*p)));
		return h.find(n) != std::string::npos;
	}

	bool IsValidIdentifier(const std::string& s)
	{
		if (s.empty()) return false;
		const unsigned char first = static_cast<unsigned char>(s[0]);
		if (false == (std::isalpha(first) || s[0] == '_')) return false;
		for (char c : s)
		{
			const unsigned char ch = static_cast<unsigned char>(c);
			if (false == (std::isalnum(ch) || c == '_')) return false;
		}
		return true;
	}

	bool IsReservedName(const std::string& s)
	{
		static const std::array<const char*, 7> kReserved = {
			"OnCreate", "OnStart", "OnUpdate", "OnFixedUpdate", "OnDestroy", "GetOwner", "GetScene"
		};
		for (const char* r : kReserved)
		{
			if (s == r) return true;
		}
		return false;
	}

	ParsedScript ParseHeaderFile(const File::Path& headerPath)
	{
		ParsedScript result;
		std::ifstream file(headerPath, std::ios::in | std::ios::binary);
		if (false == file.is_open()) return result;
		const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

		// 첫 클래스명.
		std::smatch cm;
		if (std::regex_search(text, cm, ScriptClassRegex()))
		{
			result.Found     = true;
			result.ClassName = cm[1].str();
		}

		// JPROP 필드(소스 순서).
		for (auto it = std::sregex_iterator(text.begin(), text.end(), JpropRegex());
			it != std::sregex_iterator(); ++it)
		{
			const std::string attrs  = (*it)[1].str();
			const std::string cppType= (*it)[2].str();
			const std::string name   = (*it)[3].str();

			Property p;
			p.Name = name;

			// 어트리뷰트(코드젠과 동일 명칭): Tooltip/Category/Range/NoSerialize.
			std::smatch am;
			static const std::regex nameRe(R"rx(\bName\s*\(\s*"([^"]*)"\s*\))rx");
			static const std::regex catRe(R"rx(\bCategory\s*\(\s*"([^"]*)"\s*\))rx");
			static const std::regex tipRe(R"rx(\bTooltip\s*\(\s*"([^"]*)"\s*\))rx");
			static const std::regex rngRe(R"rx(\bRange\s*\(\s*([-0-9.eEfF]+)\s*,\s*([-0-9.eEfF]+)\s*\))rx");
			static const std::regex serRe(R"rx(\bNoSerialize\b)rx");
			if (std::regex_search(attrs, am, nameRe)) p.DisplayName = am[1].str();
			if (std::regex_search(attrs, am, catRe)) p.Category = am[1].str();
			if (std::regex_search(attrs, am, tipRe)) p.Tooltip  = am[1].str();
			if (std::regex_search(attrs, am, rngRe))
			{
				p.HasRange = true;
				try { p.RangeMin = std::stod(am[1].str()); } catch (...) { p.RangeMin = 0.0; }
				try { p.RangeMax = std::stod(am[2].str()); } catch (...) { p.RangeMax = 1.0; }
			}
			if (std::regex_search(attrs, serRe)) p.NoSerialize = true;

			// 타입 역매핑.
			if (0 == cppType.rfind("Ref<", 0))
			{
				const std::string inner = ExtractRefInner(cppType);
				if (inner == "GameObject")
				{
					p.TypeToken = "Ref<GameObject>"; p.RefTarget = "GameObject"; p.RefInclude = "";
				}
				else if (const char* ai = AssetIncludeFor(inner))
				{
					p.TypeToken = "Ref<Asset>"; p.RefTarget = inner; p.RefInclude = ai;
				}
				else
				{
					p.TypeToken = "Ref<Component>"; p.RefTarget = inner;
					p.RefInclude = IncludeForComponentOrScript(inner);
				}
			}
			else
			{
				p.TypeToken = cppType;   // 스칼라 — 그대로
			}

			result.Properties.push_back(std::move(p));
		}
		return result;
	}

	bool WriteHeaderFile(const File::Path& headerPath,
	                     const std::string& className,
	                     const std::vector<Property>& props)
	{
		std::string src;
		{
			std::ifstream file(headerPath, std::ios::in | std::ios::binary);
			if (false == file.is_open()) return false;
			src.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		}

		const std::string eol = (src.find("\r\n") != std::string::npos) ? "\r\n" : "\n";

		// 1) 새 JPROP 블록 문자열.
		std::string block;
		for (const Property& p : props)
		{
			block += FormatJpropLine(p) + eol;
		}

		// 2) 기존 JPROP 문장들의 줄 범위 수집.
		std::vector<std::pair<std::size_t, std::size_t>> ranges;
		for (auto it = std::sregex_iterator(src.begin(), src.end(), JpropRegex());
			it != std::sregex_iterator(); ++it)
		{
			const std::size_t mStart = static_cast<std::size_t>(it->position(0));
			const std::size_t mEnd   = mStart + static_cast<std::size_t>(it->length(0));
			ranges.push_back({ LineStart(src, mStart), LineEndIncl(src, mEnd) });
		}

		std::string out;
		if (false == ranges.empty())
		{
			// 첫 JPROP 자리에 블록 삽입, 나머지 JPROP 줄 제거.
			const std::size_t anchor = ranges.front().first;
			out += src.substr(0, anchor);
			out += block;
			std::size_t cur = anchor;
			for (const std::pair<std::size_t, std::size_t>& r : ranges)
			{
				if (r.first > cur) out += src.substr(cur, r.first - cur);
				cur = r.second;
			}
			out += src.substr(cur);
		}
		else
		{
			// 기존 JPROP 없음 — 클래스 public: (없으면 여는 '{') 다음에 삽입.
			std::smatch cm;
			std::size_t anchor = std::string::npos;
			if (std::regex_search(src, cm, ScriptClassRegex()))
			{
				const std::size_t classPos = static_cast<std::size_t>(cm.position(0));
				const std::size_t brace = src.find('{', classPos);
				if (brace != std::string::npos)
				{
					const std::size_t pub = src.find("public:", brace);
					anchor = (pub != std::string::npos)
						? LineEndIncl(src, pub)
						: LineEndIncl(src, brace);
				}
			}
			if (anchor == std::string::npos) return false;   // 클래스 못 찾음 — 안전상 중단
			out += src.substr(0, anchor);
			out += block;
			out += src.substr(anchor);
		}

		// 3) 필요한 Ref include 추가(이미 있으면 스킵, 제거는 안 함).
		{
			std::unordered_set<std::string> needed;
			for (const Property& p : props)
			{
				if (false == IsRefToken(p.TypeToken) || p.RefInclude.empty()) continue;
				if (p.RefTarget == className) continue;   // 자가 포함 방지
				needed.insert(p.RefInclude);
			}
			std::vector<std::string> toAdd;
			for (const std::string& inc : needed)
			{
				if (out.find("\"" + inc + "\"") == std::string::npos) toAdd.push_back(inc);
			}
			if (false == toAdd.empty())
			{
				// 마지막 #include 줄 다음에 삽입.
				const std::size_t lastInc = out.rfind("#include");
				const std::size_t insertAt = (lastInc != std::string::npos)
					? LineEndIncl(out, lastInc)
					: 0;
				std::string incBlock;
				for (const std::string& inc : toAdd) incBlock += "#include \"" + inc + "\"" + eol;
				out.insert(insertAt, incBlock);
			}
		}

		std::ofstream ofs(headerPath, std::ios::out | std::ios::binary | std::ios::trunc);
		if (false == ofs.is_open()) return false;
		ofs.write(out.data(), static_cast<std::streamsize>(out.size()));
		return ofs.good();
	}
}
