#pragma once

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  ScriptSchema ─ 스크립트 .h 의 JPROP 프로퍼티 스키마 공유 로직 (UI 없음).
//
//  - 스크립트 생성 팝업(AssetBrowserTool)과 인스펙터 스키마 에디터가 같이 쓴다.
//  - 타입 토큰 테이블 / Ref 대상 목록 / 토큰 로직 + .h 파싱·기록(스플라이스).
//  - ImGui 의존 없음. 콤보 위젯 같은 UI 는 호출부가 담당.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

#include "Utillity/File/FilePath.h"

#include <string>
#include <vector>

namespace ScriptSchema
{
	// 한 JPROP 프로퍼티.
	//   TypeToken : 1차 콤보 토큰. "Bool".."Rect" 스칼라, 또는 Ref 3종
	//               "Ref<GameObject>" / "Ref<Component>" / "Ref<Asset>".
	//   RefTarget : Ref<Component>/Ref<Asset> 일 때 2차 콤보가 고른 C++ 타입(예: Transform2D, CSpriteAsset).
	//   RefInclude: RefTarget 에 필요한 헤더("" = 불필요).
	//   Category  : JPROP(Category("...")) 그룹 라벨("" = 없음).
	//   Name      : 필드 이름.
	struct Property
	{
		std::string TypeToken  = "Float";
		std::string RefTarget  = "GameObject";
		std::string RefInclude = "";
		std::string Name       = "";

		// JPROP 어트리뷰트(⋮ 메뉴에서 편집). 코드젠이 reflection 으로 전파.
		std::string DisplayName = "";      // Name("..") 인스펙터 표시 이름(C++ 멤버명과 별개)
		std::string Category    = "";      // Category("..") 인스펙터 그룹
		std::string Tooltip     = "";      // Tooltip("..") 인스펙터 설명
		bool        NoSerialize = false;   // NoSerialize 씬 저장 제외(인스펙터엔 노출)
		bool        HasRange    = false;   // Range(min,max) 슬라이더 범위
		double      RangeMin    = 0.0;
		double      RangeMax    = 1.0;
	};

	// 2차 콤보 한 항목.
	struct RefTargetInfo
	{
		std::string Label;     // 콤보 표시(친숙명)
		std::string TypeName;  // Ref<TypeName>
		std::string Include;   // "" = 불필요
	};

	struct ParsedScript
	{
		bool                  Found = false;   // JBRO_SCRIPT 클래스 1개 이상 발견
		std::string           ClassName;       // 첫 클래스명
		std::vector<Property> Properties;      // 소스 순서
	};

	// ── 타입 테이블 / 토큰 로직 ──────────────────────────────────────────────
	const std::vector<std::string>& BaseTypes();          // 1차 콤보 라벨(토큰)
	std::vector<RefTargetInfo>      ComponentTargets();   // Ref<Component>: 등록 컴포넌트+스크립트
	std::vector<RefTargetInfo>      AssetTargets();       // Ref<Asset>: 엔진 에셋 타입

	bool        IsRefToken(const std::string& token);         // "Ref<" 접두
	bool        NeedsTargetCombo(const std::string& token);   // Component/Asset 만 true
	void        ResetRefTargetForToken(Property& p);          // 1차 변경 시 2차 기본값
	std::string FinalTypeToken(const Property& p);            // 헤더에 쓸 C++ 타입
	std::string DefaultValueForToken(const std::string& finalToken);

	// "\tJPROP(attrs) <type> <name> = <default>;" 한 줄(개행 없음). 팝업/인스펙터/기록 공용.
	std::string FormatJpropLine(const Property& p);

	// 대소문자 무시 부분일치(검색 필터 공용).
	bool ContainsCaseInsensitive(const std::string& haystack, const char* needle);

	// 프로퍼티 이름 검증(공용).
	bool IsValidIdentifier(const std::string& s);   // C++ 식별자
	bool IsReservedName(const std::string& s);      // 라이프사이클/베이스 이름 충돌

	// ── .h 파싱 / 기록 ───────────────────────────────────────────────────────
	// 첫 JBRO_SCRIPT 클래스의 JPROP 필드를 읽는다.
	ParsedScript ParseHeaderFile(const File::Path& headerPath);

	// .h 의 JPROP 블록만 props 로 교체(나머지 줄 보존). 필요한 Ref include 는 추가(제거 안 함).
	// 이름 변경은 .h 한정(.cpp 미반영 — 인스펙터에선 이름 read-only). 성공 시 true.
	bool WriteHeaderFile(const File::Path& headerPath,
	                     const std::string& className,
	                     const std::vector<Property>& props);
}
