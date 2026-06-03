#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using TypeId = std::uint64_t;
inline constexpr TypeId INVALID_TYPE_ID = 0;

enum class EReflectTypeKind
{
	Unknown,
	Component,
	Script
};

enum class EReflectPropertyType
{
	Bool,
	Int32,
	Int64,
	UInt32,
	UInt64,
	Float,
	Degree,
	Radian,
	AngleDegrees,  // float (radians) stored in memory, displayed/edited as degrees in inspector
	String,
	Vector2Float,
	RectFloat,
	ColorFloat4,
	AssetGuid,
	Enum,
	Layout2D,  // Normalized(x,y) + Pixel(x,y) — maps to struct Layout2D
	Ref        // Ref<T> — 오브젝트/컴포넌트/스크립트/에셋 참조. RefCategory/RefTypeName 참고.
};

// Ref<T> 의 대상 분류. Ref.h 의 Ref<T>::Category 와 동일 값.
// (단일 정의를 여기 두고 Ref.h 가 이 헤더를 포함한다.)
enum class ERefCategory : std::uint8_t
{
	Component,
	Script,
	Asset,
	Object, // CGameObject 자체 참조(컴포넌트 아님) — InstanceGuid 로 오브젝트 해석
};

// Ref<T> 의 직렬화/저장부(타입소거 공통 레이아웃).
// guid 를 **고정 길이 문자열 버퍼**로 들고 있다 — File::Guid(std::filesystem::path) 처럼
// 내부 힙 포인터를 가진 객체를 게임 스크립트 인스턴스에 두면, 호스트(에디터)가 써 넣은
// 값을 게임 DLL 이 다른 ABI/레이아웃으로 읽어 깨진다. guid 는 고정 길이 식별자이므로
// 단순 char 버퍼(POD)로 두면 호스트↔DLL 이 동일 바이트를 동일하게 해석한다.
struct RefBase
{
	static constexpr std::size_t GuidCapacity = 64;
	char Guid[GuidCapacity] = {};   // null-종료 guid 문자열

	bool        IsNull()    const { return '\0' == Guid[0]; }
	void        Clear()           { Guid[0] = '\0'; }
	const char* GuidText()  const { return Guid; }
	void        SetGuidText(const char* text)
	{
		if (nullptr == text) { Guid[0] = '\0'; return; }
		std::size_t i = 0;
		for (; i + 1 < GuidCapacity && '\0' != text[i]; ++i) { Guid[i] = text[i]; }
		Guid[i] = '\0';
	}
};

struct ReflectTypeInfo
{
	TypeId Id = INVALID_TYPE_ID;
	const char* Name = nullptr;
	const char* DisplayName = nullptr;
	const char* Category = nullptr;
	EReflectTypeKind Kind = EReflectTypeKind::Unknown;
	std::size_t Size = 0;
	std::size_t Alignment = 0;
};

struct ReflectPropertyInfo
{
	const char*          Name         = nullptr;
	const char*          DisplayName  = nullptr;
	EReflectPropertyType Type         = EReflectPropertyType::Float;

	// ── 필드 접근 방식 (둘 중 하나만 사용) ───────────────────────────────────
	// Component 프로퍼티: AddProperty() 경유, offsetof 로 컴파일 타임에 계산됨.
	std::size_t          Offset       = 0;
	// Script 프로퍼티: REFLECT_FIELD 매크로 경유, 런타임 함수 포인터로 접근.
	// 클래스 선언부(불완전 타입)에서 offsetof 를 쓸 수 없으므로 이 방식을 사용한다.
	// (JPROP codegen 방식은 생성 파일에서 offsetof 를 써 Offset 으로 등록한다.)
	void*              (*GetFieldPtr)(void*) = nullptr;

	std::size_t          Size         = 0;
	std::size_t          ElementCount = 1;
	bool                 IsEditable   = true;

	// ── 인스펙터 메타데이터 (JPROP 어트리뷰트로 지정) ─────────────────────────
	const char*          Tooltip      = nullptr;   // 마우스오버 설명
	const char*          Category     = nullptr;   // 인스펙터 그룹 헤더
	bool                 HasRange     = false;      // true 면 슬라이더 + 클램프
	float                RangeMin     = 0.0f;
	float                RangeMax     = 0.0f;
	// false 면 인스펙터엔 노출하되 씬 파일에는 저장/복원하지 않는다(JPROP(NoSerialize)).
	bool                 Serialize    = true;

	// ── Ref 전용 (Type == Ref 일 때만 유효) ───────────────────────────────────
	ERefCategory         RefCategory  = ERefCategory::Component; // 드롭 대상 분류
	const char*          RefTypeName  = nullptr;                  // 대상 타입명(드롭 필터/표시)
};

// JPROP codegen 이 생성하는 스크립트 프로퍼티 1개의 명세.
// GeneratedScriptRegistry.cpp 가 offsetof 로 Offset 을 채워 RegisterScript 에 넘긴다.
struct ScriptPropertyDesc
{
	const char*          Name         = nullptr;
	EReflectPropertyType Type         = EReflectPropertyType::Float;
	std::size_t          Offset       = 0;
	std::size_t          Size         = 0;
	std::size_t          ElementCount = 1;
	const char*          DisplayName  = nullptr;
	const char*          Tooltip      = nullptr;
	const char*          Category     = nullptr;
	bool                 HasRange     = false;
	float                RangeMin     = 0.0f;
	float                RangeMax     = 0.0f;
	bool                 Serialize    = true;       // false = JPROP(NoSerialize)

	// ── Ref 전용 (Type == Ref 일 때만 유효) ───────────────────────────────────
	// 생성 코드가 Ref<X>::Category 와 "X" 를 채운다(코드젠은 분류 안 함).
	ERefCategory         RefCategory  = ERefCategory::Component;
	const char*          RefTypeName  = nullptr;
};

struct ComponentRegisterDesc
{
	const char* Name = nullptr;
	const char* DisplayName = nullptr;
	const char* Category = nullptr;
	bool CanAddToEntity  = true;
};

struct ScriptRegisterDesc
{
	const char* Name = nullptr;
	const char* DisplayName = nullptr;
	const char* Category = nullptr;
};
