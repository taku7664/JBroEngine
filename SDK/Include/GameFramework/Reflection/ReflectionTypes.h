#pragma once

#include "Core/Asset/AssetTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using TypeId = std::uint64_t;
inline constexpr TypeId INVALID_TYPE_ID = 0;

constexpr TypeId MakeStableTypeId(const char* name)
{
	if (nullptr == name || '\0' == name[0])
	{
		return INVALID_TYPE_ID;
	}

	TypeId hash = 14695981039346656037ull;
	while ('\0' != *name)
	{
		hash ^= static_cast<unsigned char>(*name++);
		hash *= 1099511628211ull;
	}
	return 0 == hash ? 1 : hash;
}

enum class EReflectTypeKind
{
	Unknown,
	Component,
	Script
};

enum class EComponentMultiplicity : std::uint8_t
{
	Single,
	Multiple
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
	Ref,       // Ref<T> — 오브젝트/컴포넌트/스크립트/에셋 참조. RefCategory/RefTypeName 참고.
	Array,
	Table
};

struct ReflectArrayOps
{
	std::size_t (*GetSize)(const void* array) = nullptr;
	std::size_t (*GetCapacity)(const void* array) = nullptr;
	void* (*GetElement)(void* array, std::size_t index) = nullptr;
	const void* (*GetConstElement)(const void* array, std::size_t index) = nullptr;
	void (*AddDefault)(void* array) = nullptr;
	void (*RemoveAt)(void* array, std::size_t index) = nullptr;
	void (*Move)(void* array, std::size_t fromIndex, std::size_t toIndex) = nullptr;
	void (*Clear)(void* array) = nullptr;
};

// Table 의 타입소거 조작. Array 와 달리 **인덱스로 원소를 지목할 수 없다** — open addressing
// 이라 슬롯이 조밀하지 않고, 삽입 한 번에 리해시가 일어나면 기존 슬롯 번호가 전부 무효가 된다.
// 그래서 순회는 불투명한 슬롯 커서로 하고, 수정은 슬롯이 아니라 **키**로 지목한다.
//
// 커서 계약: BeginSlot 이 첫 유효 슬롯을, NextSlot 이 그 다음을 준다. 더 없으면 둘 다
// InvalidTableSlot 을 돌려준다. 순회 도중 삽입/삭제하면 커서는 무효다(리해시 가능).
struct ReflectTableOps
{
	std::size_t (*GetSize)(const void* table) = nullptr;

	std::size_t (*BeginSlot)(const void* table) = nullptr;
	std::size_t (*NextSlot)(const void* table, std::size_t slot) = nullptr;

	const void* (*GetKeyAt)(const void* table, std::size_t slot) = nullptr;
	void* (*GetValueAt)(void* table, std::size_t slot) = nullptr;
	const void* (*GetConstValueAt)(const void* table, std::size_t slot) = nullptr;

	// key 는 Key 타입의 객체 주소. 이미 있는 키면 InsertDefault 는 아무것도 하지 않고 false.
	bool (*ContainsKey)(const void* table, const void* key) = nullptr;
	bool (*InsertDefault)(void* table, const void* key) = nullptr;
	bool (*RemoveKey)(void* table, const void* key) = nullptr;

	// 키로 값을 찾는다(없으면 nullptr). 슬롯 커서는 삽입 한 번에 무효가 되므로, 방금 넣은
	// 자리를 다시 잡으려면 이쪽을 써야 한다. 바이트 비교로 대신할 수 없다 — std::string 처럼
	// 내용이 밖에 있는 키는 객체 바이트가 달라도 같은 키다.
	void* (*FindValue)(void* table, const void* key) = nullptr;

	// 기본 생성된 Key 1개를 만들고 없앤다. 위 셋이 Key 객체 주소를 받는데, 호출부는 타입이
	// 소거돼 있어 스택에 Key 를 만들 수 없다 — 역직렬화가 YAML 에서 키를 복원할 때 필요하다.
	// **할당이 일어난다.** 직렬화/에디터 경로 전용이고 프레임 루프에서 쓰지 말 것.
	// CreateKey 로 받은 것은 반드시 같은 desc 의 DestroyKey 로 돌려준다.
	void* (*CreateKey)() = nullptr;
	void (*DestroyKey)(void* key) = nullptr;

	void (*Clear)(void* table) = nullptr;
};

// BeginSlot/NextSlot 의 "끝" 표식.
inline constexpr std::size_t InvalidTableSlot = static_cast<std::size_t>(-1);

// 타입 하나의 타입소거 명세. 컨테이너면 Element / Key+Value 로 원소 타입을 재귀 기술하고,
// 저장소 조작은 ArrayOps / TableOps 함수포인터로만 한다(인스펙터·직렬화가 레이아웃을 직접 캐스팅하지 않는다).
//
// Type 은 ReflectPropertyInfo::Type / ScriptPropertyDesc::Type 과 같은 값·같은 의미다.
// 한때 Kind(Scalar/Enum/Ref/Struct/Array/Table) 라는 별도 축이 함께 있었지만, 실제 분기는
// 전부 이 Type 으로만 이뤄졌고 Kind 는 Type 에서 그대로 유도되는 값이었다 — 채우는 곳마다
// 손으로 동기화해야 하는 이중 진실이라 삭제했다.
struct ReflectTypeDesc
{
	EReflectPropertyType Type = EReflectPropertyType::Float;
	std::size_t Size = 0;
	std::size_t Alignment = 0;
	bool IsTriviallyCopyable = false;
	const ReflectTypeDesc* Element = nullptr;
	const ReflectTypeDesc* Key = nullptr;
	const ReflectTypeDesc* Value = nullptr;
	const ReflectArrayOps* ArrayOps = nullptr;
	const ReflectTableOps* TableOps = nullptr;
};

// Enum 프로퍼티(Type == Enum)의 타입소거 메타. magic_enum 으로 자동 생성한다
// (MakeEnumTypeMeta<T>). 인스펙터 드롭다운 / 이름 직렬화에 필요한 변환을 함수포인터로
// 들고 있다. 정적 수명(타입당 1개) + 함수포인터 + 정적 이름배열이라 호스트↔게임 DLL
// 경계에 안전하다(각 모듈이 자기 모듈의 메타를 가리킨다 — POD 규칙 위반 아님).
struct EnumTypeMeta
{
	const char* const* Names = nullptr;   // 값 순서대로의 이름 배열(null-종료 문자열)
	int                Count = 0;

	// field 는 enum 변수의 주소, size 는 프로퍼티 크기(enum underlying 폭).
	int  (*ToIndex)  (const void* field, std::size_t size) = nullptr;          // 현재값 → 이름배열 인덱스(-1=미일치)
	void (*SetIndex) (void* field, std::size_t size, int index) = nullptr;     // 인덱스 → 값 기록
	const char* (*ToName)(const void* field, std::size_t size) = nullptr;      // 현재값 → 이름(미일치 시 nullptr)
	bool (*FromName) (void* field, std::size_t size, const char* name) = nullptr; // 이름 → 값 기록(성공 true)
};

// Ref<T> 의 대상 분류. Ref.h 의 Ref<T>::Category 와 동일 값.
// (단일 정의를 여기 두고 Ref.h 가 이 헤더를 포함한다.)
enum class ERefCategory : std::uint8_t
{
	Component,
	Script,
	Asset,
	Object, // CGameObject 자체 참조(컴포넌트 아님) — InstanceGuid 로 오브젝트 해석
	Canvas, // CGameCanvas 참조 — guid 가 아니라 캔버스 이름(CanvasManager 의 유일 키)으로 해석
};

// Ref<T> 의 직렬화/저장부(타입소거 공통 레이아웃).
// guid 를 **고정 길이 문자열 버퍼**로 들고 있다 — File::Guid(std::filesystem::path) 처럼
// 내부 힙 포인터를 가진 객체를 게임 스크립트 인스턴스에 두면, 호스트(에디터)가 써 넣은
// 값을 게임 DLL 이 다른 ABI/레이아웃으로 읽어 깨진다. guid 는 고정 길이 식별자이므로
// 단순 char 버퍼(POD)로 두면 호스트↔DLL 이 동일 바이트를 동일하게 해석한다.
struct RefBase
{
	static constexpr std::size_t GuidCapacity = 64;

	// Guid          = 주 대상 식별자.
	//                   · Object/Script → 오브젝트 InstanceGuid
	//                   · Asset         → AssetGuid
	//                   · Component     → 소유 오브젝트의 InstanceGuid
	// ComponentGuid = 컴포넌트 카테고리 전용 보조 식별자(= 컴포넌트 InstanceGuid).
	//                   한 오브젝트에 같은 타입 컴포넌트가 여럿일 때 특정 1개를 지목한다.
	//                   다른 카테고리에선 비어 있다.
	char Guid[GuidCapacity] = {};            // null-종료 guid 문자열
	char ComponentGuid[GuidCapacity] = {};

	bool        IsNull()    const { return '\0' == Guid[0]; }
	void        Clear()           { Guid[0] = '\0'; ComponentGuid[0] = '\0'; }
	const char* GuidText()  const { return Guid; }
	const char* ComponentGuidText() const { return ComponentGuid; }
	void        SetGuidText(const char* text)          { CopyGuid(Guid, text); }
	void        SetComponentGuidText(const char* text) { CopyGuid(ComponentGuid, text); }

private:
	static void CopyGuid(char (&dst)[GuidCapacity], const char* text)
	{
		if (nullptr == text) { dst[0] = '\0'; return; }
		std::size_t i = 0;
		for (; i + 1 < GuidCapacity && '\0' != text[i]; ++i) { dst[i] = text[i]; }
		dst[i] = '\0';
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
	const ReflectTypeDesc* Descriptor = nullptr;

	// ── 인스펙터 메타데이터 (JPROP 어트리뷰트로 지정) ─────────────────────────
	const char*          Tooltip      = nullptr;   // 마우스오버 설명
	const char*          Category     = nullptr;   // 인스펙터 그룹 헤더
	bool                 HasRange     = false;      // true 면 슬라이더 + 클램프
	float                RangeMin     = 0.0f;
	float                RangeMax     = 0.0f;
	// false 면 인스펙터엔 노출하되 캔버스 파일에는 저장/복원하지 않는다(JPROP(NoSerialize)).
	bool                 Serialize    = true;

	// ── Ref 전용 (Type == Ref 일 때만 유효) ───────────────────────────────────
	ERefCategory         RefCategory  = ERefCategory::Component; // 드롭 대상 분류
	const char*          RefTypeName  = nullptr;                  // 대상 타입명(드롭 필터/표시)

	// ── AssetGuid / asset Ref 전용 ───────────────────────────────────────────
	EAssetType           ExpectedAssetType = EAssetType::Unknown; // Unknown 이면 타입 제한 없음

	// ── Enum 전용 (Type == Enum 일 때만 유효) ─────────────────────────────────
	const EnumTypeMeta*  Enum         = nullptr;                  // 이름 목록/변환(magic_enum)
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
	const ReflectTypeDesc* Descriptor = nullptr;

	// ── Ref 전용 (Type == Ref 일 때만 유효) ───────────────────────────────────
	// 생성 코드가 Ref<X>::Category 와 "X" 를 채운다(코드젠은 분류 안 함).
	ERefCategory         RefCategory  = ERefCategory::Component;
	const char*          RefTypeName  = nullptr;
	EAssetType           ExpectedAssetType = EAssetType::Unknown;

	// ── Enum 전용 (Type == Enum 일 때만 유효) ─────────────────────────────────
	// 스크립트 enum 은 후속(JPROP codegen) — 현재 컴포넌트만 채운다.
	const EnumTypeMeta*  Enum         = nullptr;
};

struct ComponentRegisterDesc
{
	const char* Name = nullptr;
	const char* DisplayName = nullptr;
	const char* Category = nullptr;
	bool CanAddToEntity  = true;
	EComponentMultiplicity Multiplicity = EComponentMultiplicity::Multiple;
};

struct ScriptRegisterDesc
{
	const char* Name = nullptr;
	const char* DisplayName = nullptr;
	const char* Category = nullptr;
};
