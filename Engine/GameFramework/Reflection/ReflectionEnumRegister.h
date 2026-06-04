#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  ReflectionEnumRegister.h — Enum 프로퍼티 등록의 호스트 전용 구현부.
//
//  ⚠ 이 헤더는 SDK 로 미러하지 않는다(StageSDK 제외). magic_enum 에 의존하므로 게임
//    스크립트 DLL 빌드(magic_enum 없음)에 노출하면 깨진다. 컴포넌트 등록 지점
//    (BuiltinComponentRegistry.cpp, 호스트)만 include 한다.
//
//  · MakeEnumTypeMeta<T>() : magic_enum 으로 enum 이름/변환 메타(EnumTypeMeta)를 자동 생성.
//  · CComponentRegistration::AddEnumProperty<T> : 위 메타를 마지막 프로퍼티에 부착.
// ─────────────────────────────────────────────────────────────────────────────

#include "GameFramework/Reflection/ReflectionRegistry.h"
#include "ThirdParty/magic_enum/magic_enum.hpp"

#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

// magic_enum 으로 enum 타입 T 의 타입소거 메타(EnumTypeMeta)를 만든다. 타입당 1개를 정적
// 수명으로 캐싱해 그 포인터를 반환한다. 이름들은 magic_enum 의 string_view(부분문자열, null-
// 종료 보장 X)를 정적 std::string 으로 복사해 안정적인 const char* 로 노출한다.
template<typename TEnum>
const EnumTypeMeta* MakeEnumTypeMeta()
{
	static_assert(std::is_enum_v<TEnum>, "MakeEnumTypeMeta<T>: T must be an enum.");

	// 정적 1회 초기화 — 이름 문자열/포인터 배열을 안정 수명으로 보관.
	static const std::vector<std::string> nameStorage = []
	{
		std::vector<std::string> out;
		for (const auto& sv : magic_enum::enum_names<TEnum>())
		{
			out.emplace_back(sv);
		}
		return out;
	}();
	static const std::vector<const char*> namePtrs = []
	{
		std::vector<const char*> out;
		out.reserve(nameStorage.size());
		for (const std::string& s : nameStorage)
		{
			out.push_back(s.c_str());
		}
		return out;
	}();

	// underlying 폭(size)만큼 안전하게 읽고/쓰는 보조 — enum 이 int 외(uint8 등)일 수 있다.
	static const EnumTypeMeta meta = []
	{
		EnumTypeMeta m;
		m.Names = namePtrs.data();
		m.Count = static_cast<int>(namePtrs.size());
		m.ToIndex = [](const void* field, std::size_t size) -> int
		{
			std::int64_t raw = 0;
			std::memcpy(&raw, field, size < sizeof(raw) ? size : sizeof(raw));
			if (auto idx = magic_enum::enum_index(static_cast<TEnum>(raw)))
			{
				return static_cast<int>(*idx);
			}
			return -1;
		};
		m.SetIndex = [](void* field, std::size_t size, int index)
		{
			if (index < 0 || index >= static_cast<int>(magic_enum::enum_count<TEnum>()))
			{
				return;
			}
			const TEnum value = magic_enum::enum_value<TEnum>(static_cast<std::size_t>(index));
			std::int64_t raw = static_cast<std::int64_t>(static_cast<std::underlying_type_t<TEnum>>(value));
			std::memcpy(field, &raw, size < sizeof(raw) ? size : sizeof(raw));
		};
		m.ToName = [](const void* field, std::size_t size) -> const char*
		{
			std::int64_t raw = 0;
			std::memcpy(&raw, field, size < sizeof(raw) ? size : sizeof(raw));
			if (auto idx = magic_enum::enum_index(static_cast<TEnum>(raw)))
			{
				return namePtrs[*idx];
			}
			return nullptr;
		};
		m.FromName = [](void* field, std::size_t size, const char* name) -> bool
		{
			if (nullptr == name)
			{
				return false;
			}
			if (auto value = magic_enum::enum_cast<TEnum>(name))
			{
				std::int64_t raw = static_cast<std::int64_t>(static_cast<std::underlying_type_t<TEnum>>(*value));
				std::memcpy(field, &raw, size < sizeof(raw) ? size : sizeof(raw));
				return true;
			}
			return false;
		};
		return m;
	}();

	return &meta;
}

// CComponentRegistration::AddEnumProperty<T> 정의(선언은 ReflectionRegistry.h).
// 마지막에 추가된 프로퍼티에 enum 메타를 부착한다.
template<typename TEnum>
CComponentRegistration& CComponentRegistration::AddEnumProperty(const char* name, std::size_t offset, bool isEditable)
{
	AddProperty(name, EReflectPropertyType::Enum, offset, sizeof(TEnum), 1, isEditable);
	if (ComponentTypeInfo* typeInfo = GetTypeInfo(); typeInfo && false == typeInfo->Properties.empty())
	{
		typeInfo->Properties.back().Enum = MakeEnumTypeMeta<TEnum>();
	}
	return *this;
}
