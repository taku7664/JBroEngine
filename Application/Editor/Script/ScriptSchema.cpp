#include "pch.h"
#include "ScriptSchema.h"

#include "Editor/EditorContext.h"
#include "Engine/Core/EngineCore.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Engine/Editor/Project/ScriptEnumScanner.h"
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
		// 1차 콤보 토큰. 정수는 **폭을 밝힌 이름만** 고르게 한다 — `Int`/`UInt` 도 여전히
		// 유효한 별칭이지만(Int.h), 콤보에 별칭과 실체를 함께 놓으면 같은 코드를 뱉는
		// 항목이 둘이 되어 고를 이유를 설명할 수 없다. 옛 헤더의 `Int` 는 읽을 때
		// NormalizeScalarToken 이 `Int64` 로 바꿔 준다.
		const std::vector<std::string> kBaseTypes = {
			"Ref<GameObject>", "Ref<Component>", "Ref<Asset>",
			"Bool", "Int32", "Int64", "UInt32", "UInt64", "Float", "Degree", "Radian",
			"String", "Vector2", "Rect", "Color", "Layout2D",
			// Enum — 2차 콤보가 Scripts/ 에 선언된 enum class 목록을 채운다.
			"Enum",
			// 컨테이너 — Ref 처럼 2차 콤보(원소/키 타입)를 함께 쓴다. Table 은 값 타입 콤보가 하나 더 붙는다.
			"Array", "Table",
		};

		// Array 의 원소로 쓸 수 있는 타입. 코드젠이 원소 desc 를 GetScalarReflectTypeDesc 로만
		// 만들기 때문에 **스칼라만** 가능하다 — Ref 원소는 RefCategory 를 실을 자리가 없고,
		// 중첩 Array 는 코드젠이 거부한다. 여기 없는 걸 고르면 조용히 누락되므로 목록을 맞춰 둔다.
		const std::vector<std::string> kArrayElementTypes = {
			"Bool", "Int32", "Int64", "UInt32", "UInt64", "Float", "Degree", "Radian",
			"String", "Vector2", "Rect",
		};

		// Table 키로 쓸 수 있는 타입. 위 목록에서 Vector2/Rect 를 뺀 것이다 — 둘은 std::hash
		// 특수화가 없어 Hash<K> 가 인스턴스화되지 않고, 고르면 **생성된 코드가 컴파일에 실패한다**
		// (조용한 누락이 아니라 빌드가 깨지므로 목록에서 아예 막는다).
		const std::vector<std::string> kTableKeyTypes = {
			"Bool", "Int32", "Int64", "UInt32", "UInt64", "Float", "Degree", "Radian", "String",
		};

		// 스크립트에서 참조 가능한 엔진 에셋 타입. 라벨(친숙명) / 클래스명(C 접두) / 헤더.
		struct AssetRefType { const char* Label; const char* TypeName; const char* Include; };
		// ⚠ 새 에셋 클래스를 만들면 **여기에도** 넣어야 한다. 이 목록이 스크립트 프로퍼티
		//    저작 UI 의 `Ref<Asset>` 2차 콤보를 채운다 — 빠뜨리면 런타임/인스펙터는 멀쩡히
		//    동작하는데 사용자가 그 프로퍼티를 **만들 수가 없다**(에디터에 항목이 안 뜬다).
		constexpr std::array<AssetRefType, 9> kAssetTypes = {{
			{ "SpriteAsset",   "CSpriteAsset",   "Core/Asset/SpriteAsset.h"   },
			{ "AudioAsset",    "CAudioAsset",    "Core/Asset/AudioAsset.h"    },
			{ "AudioEffectAsset", "CAudioEffectAsset", "Core/Asset/AudioEffectAsset.h" },
			{ "MaterialAsset", "CMaterialAsset", "Core/Asset/MaterialAsset.h" },
			{ "CanvasAsset",   "CCanvasAsset",   "Core/Asset/CanvasAsset.h"   },
			{ "PrefabAsset",   "CPrefabAsset",   "Core/Asset/PrefabAsset.h"   },
			{ "LayerAsset",    "CLayerAsset",    "Core/Asset/LayerAsset.h"    },
			{ "FontFaceAsset", "CFontFaceAsset", "Core/Asset/FontAsset.h"     },
			{ "FontFamilyAsset", "CFontFamilyAsset", "Core/Asset/FontAsset.h" },
		}};

		const char* BuiltinComponentIncludeFor(const std::string& typeName)
		{
			if (typeName == "Square2D" || typeName == "Circle2D" || typeName == "Polygon2D")
			{
				return "GameFramework/Component/ShapeRenderers2D.h";
			}
			if (typeName == "Rigidbody2D" || typeName == "PolygonCollider2D" || typeName == "CircleCollider2D")
			{
				return "GameFramework/Component/Physics2DComponents.h";
			}
			if (typeName == "AudioListener" || typeName == "AudioPlayer")
			{
				return "GameFramework/Component/AudioComponents.h";
			}
			return nullptr;
		}

		// 컴포넌트/스크립트 타입명 → 헤더 경로(리플렉션으로 스크립트 여부 판정).
		std::string IncludeForComponentOrScript(const std::string& typeName)
		{
			const bool isScript = Engine.Reflection.IsValid()
				&& nullptr != Engine.Reflection->FindScriptByName(typeName.c_str());
			if (false == isScript)
			{
				if (const char* include = BuiltinComponentIncludeFor(typeName))
				{
					return include;
				}
			}
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

		// 현재 프로젝트 Scripts/ 의 enum class 목록. 코드 생성기와 같은 스캐너를 쓴다 —
		// 여기서 따로 파싱하면 "생성기는 아는데 콤보엔 안 뜨는" 목록이 또 생긴다.
		std::vector<ScriptEnumInfo> ProjectScriptEnums()
		{
			SafePtr<CProjectManager> projectManager = EditorContext::GetProjectManager();
			if (false == projectManager.IsValid())
			{
				return {};
			}
			return ScanScriptEnums(projectManager->GetScriptPath());
		}

		// 옛 헤더가 쓰는 폭 없는 별칭을 콤보 목록에 있는 실체 이름으로 바꾼다.
		// 별칭이라 생성되는 코드는 어느 쪽이든 같다 — 콤보가 현재 값을 못 찾아
		// 다른 타입으로 튀는 것만 막으면 된다.
		std::string NormalizeScalarToken(const std::string& token)
		{
			if (token == "Int")  return "Int64";
			if (token == "UInt") return "UInt64";
			return token;
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
		if (Engine.Reflection.IsValid())
		{
			for (std::size_t i = 0; i < Engine.Reflection->GetComponentTypeCount(); ++i)
			{
				const ComponentTypeInfo* ti = Engine.Reflection->GetComponentType(i);
				if (nullptr == ti || nullptr == ti->Type.Name) continue;
				const char* n = ti->Type.Name;
				if (0 == std::strcmp(n, "GameObject")) continue;            // Ref<GameObject> 로 분리
				if (0 == std::strcmp(n, "TransformHierarchy2D")) continue;  // 내부용
				out.push_back({ n, n, IncludeForComponentOrScript(n) });
			}
			for (std::size_t i = 0; i < Engine.Reflection->GetScriptTypeCount(); ++i)
			{
				const ScriptTypeInfo* si = Engine.Reflection->GetScriptType(i);
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
	bool IsArrayToken(const std::string& t)      { return t == "Array"; }
	bool IsTableToken(const std::string& t)      { return t == "Table"; }
	bool NeedsTargetCombo(const std::string& t)
	{
		return t == "Ref<Component>" || t == "Ref<Asset>" || IsArrayToken(t) || IsTableToken(t)
			|| IsEnumToken(t);
	}
	bool NeedsValueCombo(const std::string& t)   { return IsTableToken(t); }

	std::vector<RefTargetInfo> ArrayElementTargets()
	{
		std::vector<RefTargetInfo> out;
		out.reserve(kArrayElementTypes.size());
		for (const std::string& t : kArrayElementTypes)
		{
			// 원소는 엔진 기본 타입이라 별도 헤더가 필요 없다(라벨 = 타입명).
			out.push_back({ t, t, "" });
		}
		return out;
	}

	std::vector<RefTargetInfo> EnumTargets()
	{
		std::vector<RefTargetInfo> out;
		for (const ScriptEnumInfo& e : ProjectScriptEnums())
		{
			// enum 은 스크립트 헤더에 선언돼 있고 그 헤더는 자기 자신이므로 include 가 없다
			// (다른 헤더의 enum 을 쓰려면 사용자가 직접 include 한다 — Ref 와 달리 코드젠이
			//  대신 넣어 주지 않는다).
			out.push_back({ e.ClassName, e.ClassName, "" });
		}
		return out;
	}

	bool IsEnumToken(const std::string& t) { return t == "Enum"; }

	std::vector<RefTargetInfo> TableKeyTargets()
	{
		std::vector<RefTargetInfo> out;
		out.reserve(kTableKeyTypes.size());
		for (const std::string& t : kTableKeyTypes)
		{
			out.push_back({ t, t, "" });
		}
		return out;
	}
	bool SupportsRange(const std::string& t)
	{
		return t == "Int32" || t == "Int64" || t == "UInt32" || t == "UInt64"
		    || t == "Float" || t == "Degree" || t == "Radian";
	}

	void ResetRefTargetForToken(Property& p)
	{
		if (p.TypeToken == "Ref<GameObject>")
		{
			p.RefTarget = "GameObject"; p.RefInclude = "";
			return;
		}
		if (IsArrayToken(p.TypeToken))
		{
			// 원소 타입은 RefTarget 을 그대로 재사용한다 — 2차 콤보 배선(초기화/파싱/기록)이
			// 이미 이 필드를 타므로, 컨테이너용 필드를 따로 두면 같은 경로가 두 벌이 된다.
			p.RefTarget = "Float"; p.RefInclude = "";
			return;
		}
		if (IsTableToken(p.TypeToken))
		{
			// 키는 RefTarget, 값은 ValueTarget. 키 기본값을 Int64 로 두는 건 Float 키가
			// 정확 일치 비교라 실수하기 쉬워서다(고를 수는 있게 두되 기본값으로는 안 민다).
			p.RefTarget = "Int64"; p.RefInclude = "";
			p.ValueTarget = "Float";
			return;
		}
		const std::vector<RefTargetInfo> targets =
			IsEnumToken(p.TypeToken)      ? EnumTargets() :
			(p.TypeToken == "Ref<Asset>") ? AssetTargets() : ComponentTargets();
		if (targets.empty()) { p.RefTarget = ""; p.RefInclude = ""; return; }
		p.RefTarget  = targets.front().TypeName;
		p.RefInclude = targets.front().Include;
	}

	std::string FinalTypeToken(const Property& p)
	{
		if (p.TypeToken == "Ref<GameObject>") return "Ref<GameObject>";
		if (IsArrayToken(p.TypeToken))        return "Array<" + p.RefTarget + ">";
		if (IsTableToken(p.TypeToken))        return "Table<" + p.RefTarget + ", " + p.ValueTarget + ">";
		// enum 은 감싸는 템플릿이 없다 — 고른 enum 클래스 이름이 곧 필드 타입이다.
		if (IsEnumToken(p.TypeToken))         return p.RefTarget;
		if (NeedsTargetCombo(p.TypeToken))    return "Ref<" + p.RefTarget + ">";
		return p.TypeToken;
	}

	std::string DefaultValueForToken(const std::string& finalToken)
	{
		if (finalToken == "Bool")   return "false";
		if (finalToken == "Int32" || finalToken == "Int64")   return "0";
		if (finalToken == "UInt32" || finalToken == "UInt64") return "0u";
		if (finalToken == "Float")  return "0.0f";
		if (finalToken == "Degree") return "Degree(0.0f)";
		if (finalToken == "Radian") return "Radian(0.0f)";
		if (finalToken == "String") return "\"\"";
		if (finalToken == "Color")  return "Color{ 1.0f, 1.0f, 1.0f, 1.0f }";   // 기본은 흰색(무보정)
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
			"OnCreate", "OnStart", "OnUpdate", "OnFixedUpdate", "OnDestroy", "GetOwner", "GetCanvas"
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
		// 프로퍼티마다 다시 스캔하지 않도록 파일당 한 번만 읽는다(디스크를 훑는 작업이다).
		const std::vector<RefTargetInfo> enumTargets = EnumTargets();
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
			if (0 == cppType.rfind("Array<", 0))
			{
				// 원소는 RefTarget 에 담는다(기록 경로 FinalTypeToken 과 짝).
				p.TypeToken = "Array";
				p.RefTarget = NormalizeScalarToken(ExtractRefInner(cppType));
				p.RefInclude = "";
			}
			else if (0 == cppType.rfind("Table<", 0))
			{
				// 키는 RefTarget, 값은 ValueTarget (기록 경로 FinalTypeToken 과 짝).
				const std::string inner = ExtractRefInner(cppType);
				const std::size_t comma = inner.find(',');
				if (std::string::npos == comma)
				{
					// 인자를 못 가르면 토큰을 못 만든다 — 손대지 않고 그대로 둔다.
					// (기본 토큰 Float 로 떨어지면 헤더에 있는 Table 필드를 다른 타입으로
					//  덮어써 버린다.)
					continue;
				}
				p.TypeToken   = "Table";
				// ExtractRefInner 가 공백을 이미 다 걷어냈으므로 그대로 자른다.
				p.RefTarget   = NormalizeScalarToken(inner.substr(0, comma));
				p.ValueTarget = NormalizeScalarToken(inner.substr(comma + 1));
				p.RefInclude  = "";
			}
			else if (0 == cppType.rfind("Ref<", 0))
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
				p.TypeToken = NormalizeScalarToken(cppType);   // 스칼라
				// Scripts/ 에 선언된 enum 이면 "Enum" 토큰 + 2차 콤보 대상으로 되돌린다.
				// 안 그러면 콤보가 현재 값을 못 찾아 첫 항목으로 떨어지고, 기록할 때
				// 헤더의 enum 필드가 엉뚱한 타입으로 덮인다.
				for (const RefTargetInfo& e : enumTargets)
				{
					if (p.TypeToken == e.TypeName)
					{
						p.TypeToken = "Enum";
						p.RefTarget = e.TypeName;
						p.RefInclude = "";
						break;
					}
				}
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
