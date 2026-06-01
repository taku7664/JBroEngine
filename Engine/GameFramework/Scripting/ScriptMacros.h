#pragma once

#include "GameFramework/Reflection/ReflectionTypes.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  스크립트 리플렉션 시스템
//
//  사용 방법:
//    JBRO_SCRIPT MyScript : public CGameScript       // 'class' 대신 JBRO_SCRIPT
//    {
//        SCRIPT_CLASS(MyScript)                       // 클래스 최상단에 한 번
//    public:
//        REFLECT_FIELD(float,       Speed,     5.0f)  // Inspector 노출 + 씬 직렬화
//        REFLECT_FIELD(bool,        IsGrounded, false)
//        REFLECT_FIELD(std::int32_t, MaxJumps,  2)
//
//        int m_internalTimer = 0;  // 일반 멤버: Inspector 미노출, 리로드 시 초기화
//    };
//
//  지원 타입: Bool, Int, UInt, Float, Degree, Radian, String, Asset, Vector2, Rect
//             (ScriptAPI.h 를 #include 해야 사용 가능)
//
//  JBRO_SCRIPT 매크로:
//    - 컴파일러에는 단순한 'class' 별칭이라 동작에 영향 없음.
//    - 프로젝트 생성기(GameScriptProjectGenerator)가 이 키워드를 찾아
//      Scripts/*.h 의 스크립트 클래스를 자동 등록한다.
//    - 다중 상속 / 깊은 상속 트리에서도 마커 한 줄로 의도를 명시할 수 있다.
//    - struct 는 지원하지 않는다. 반드시 class 만 사용.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

// ── JBRO_SCRIPT ──────────────────────────────────────────────────────────────
// 스크립트 클래스 선언부의 'class' 자리에 사용한다.
// 빌드 전 단계의 헤더 스캐너가 이 토큰을 grep 하여 GeneratedScriptRegistry.cpp 를
// 자동 생성한다 — 유저는 RegisterScript 호출을 직접 작성하지 않아도 된다.
#define JBRO_SCRIPT class

// ── JPROP(...) ───────────────────────────────────────────────────────────────
// 멤버 변수 바로 앞에 붙이는 "어트리뷰트 마커". 컴파일러에는 아무 토큰도 아니지만
// (확장 결과가 비어 있음), 빌드 전 헤더 스캐너가 이 마커 + 뒤따르는 멤버 선언을
// 파싱해 GeneratedScriptRegistry.cpp 에 등록 코드를 자동 생성한다.
//
//   JBRO_SCRIPT MyScript final : public CGameScript
//   {
//       JPROP() float Speed = 5.0f;
//       JPROP(Name("이동 속도"), Range(0, 100), Tooltip("초당 이동"), Category("Movement"))
//       float MoveSpeed = 3.0f;
//       JPROP() AssetGuid Icon;       // 에셋 picker
//   };
//
// 지원 어트리뷰트:
//   Name("..")      — 인스펙터 표시 이름(멤버 이름과 분리)
//   Tooltip("..")   — 마우스오버 설명
//   Category("..")  — 인스펙터 그룹 헤더
//   Range(min, max) — 슬라이더 + 값 클램프
//   NoSerialize     — 인스펙터엔 노출하되 씬 파일에는 저장/복원하지 않음(런타임 전용)
// 지원 타입: Bool, Int, UInt, Float, Degree, Radian, String, Vector2, Rect, Asset
//            레거시 표기(bool, std::int32_t, std::int64_t, std::uint32_t, float, Vector2, AssetGuid)도 허용
// (SCRIPT_CLASS / REFLECT_FIELD 없이 JPROP 만으로 충분하다. REFLECT_FIELD 는 레거시 호환.)
#define JPROP(...)

// ── ScriptReflectEntry ────────────────────────────────────────────────────────
// REFLECT_FIELD 매크로가 등록하는 단일 필드 메타데이터.
// RegisterScript<T>() 가 읽어 ScriptTypeInfo::Properties 를 자동 구성한다.
//
// Offset 대신 GetFieldPtr 를 사용하는 이유:
//   REFLECT_FIELD 는 클래스 본문(불완전 타입) 내부에서 확장되므로
//   offsetof(ScriptSelfType, FieldName) 이 C2079 컴파일 오류를 일으킨다.
//   GetFieldPtr 는 static 초기화 시점(클래스 완성 후)에 실행되는 람다이므로
//   항상 안전하게 멤버 주소를 반환할 수 있다.
struct ScriptReflectEntry
{
	const char*          Name         = nullptr;
	EReflectPropertyType Type         = EReflectPropertyType::Float;
	void*              (*GetFieldPtr)(void*) = nullptr;  // &static_cast<T*>(obj)->Field
	std::size_t          Size         = 0;
	std::size_t          ElementCount = 1;
};

// ── ScriptFieldTypeOf<T>() ────────────────────────────────────────────────────
// C++ 타입 → EReflectPropertyType 매핑.
// 지원하지 않는 타입을 REFLECT_FIELD 에 사용하면 컴파일 오류가 발생한다.
// 엔진 타입 특수화는 ScriptAPI.h(EngineTypes.h / Vector2T.h 포함 후)에서 제공된다.
template<typename T> inline EReflectPropertyType ScriptFieldTypeOf()
{
	static_assert(sizeof(T) == 0,
		"REFLECT_FIELD: unsupported type. "
		"Supported: Bool, Int, UInt, Float, Degree, Radian, String, Vector2, Rect, Asset. "
		"Include ScriptAPI.h for engine type support.");
	return EReflectPropertyType::Float;
}
template<> inline EReflectPropertyType ScriptFieldTypeOf<bool>()              { return EReflectPropertyType::Bool; }
template<> inline EReflectPropertyType ScriptFieldTypeOf<std::int32_t>()      { return EReflectPropertyType::Int32; }
template<> inline EReflectPropertyType ScriptFieldTypeOf<std::int64_t>()      { return EReflectPropertyType::Int64; }
template<> inline EReflectPropertyType ScriptFieldTypeOf<std::uint32_t>()     { return EReflectPropertyType::UInt32; }
template<> inline EReflectPropertyType ScriptFieldTypeOf<float>()             { return EReflectPropertyType::Float; }

// ── SCRIPT_CLASS(T) ───────────────────────────────────────────────────────────
// 스크립트 클래스 본문 최상단에 한 번 기재한다.
//
//   using ScriptSelfType = T
//     → REFLECT_FIELD 내부에서 offsetof(ScriptSelfType, member) 에 사용.
//
//   static GetReflectEntries()
//     → 이 클래스의 REFLECT_FIELD 목록을 반환. RegisterScript<T>() 가 읽는다.
//     → 함수-로컬 정적 벡터이므로 클래스별로 독립된 목록을 가진다.
#define SCRIPT_CLASS(T)                                                          \
public:                                                                          \
	using ScriptSelfType = T;                                                    \
	static std::vector<ScriptReflectEntry>& GetReflectEntries()                  \
	{                                                                            \
		static std::vector<ScriptReflectEntry> s_entries;                        \
		return s_entries;                                                        \
	}

// ── REFLECT_FIELD(CppType, FieldName, DefaultValue) ──────────────────────────
// 멤버 변수를 선언하고 리플렉션 시스템에 등록한다.
//
// 효과:
//   1. Inspector 에 해당 필드가 자동으로 표시된다.
//   2. 씬 파일 저장/로드 시 값이 자동으로 직렬화된다.
//   3. 라이브 리로드(DLL 교체) 후에도 값이 유지된다.
//
// 주의:
//   - SCRIPT_CLASS(T) 선언 이후에만 사용 가능하다.
//   - inline static 멤버는 C++17 이상이 필요하다.
//   - GetFieldPtr 람다는 클래스가 완성된 뒤 static 초기화 시점에 실행되므로
//     불완전 타입 문제(C2079)가 발생하지 않는다.
#define REFLECT_FIELD(CppType, FieldName, DefaultValue)                          \
public:                                                                          \
	CppType FieldName = (DefaultValue);                                          \
private:                                                                         \
	template<typename TScript>                                                   \
	static void* _srefl_get_##FieldName(void* _obj)                              \
	{                                                                            \
		return &static_cast<TScript*>(_obj)->FieldName;                          \
	}                                                                            \
	inline static const bool _srefl_##FieldName = []() -> bool                  \
	{                                                                            \
		ScriptSelfType::GetReflectEntries().push_back(ScriptReflectEntry{        \
			#FieldName,                                                          \
			ScriptFieldTypeOf<CppType>(),                                        \
			&_srefl_get_##FieldName<ScriptSelfType>,                             \
			sizeof(CppType),                                                     \
			1                                                                    \
		});                                                                      \
		return true;                                                             \
	}();                                                                         \
public:
