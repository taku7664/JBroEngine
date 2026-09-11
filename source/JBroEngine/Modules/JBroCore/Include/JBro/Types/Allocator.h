#pragma once

#include <cstddef>

namespace JBro
{
enum class EMemoryTag : unsigned char
{
	Unknown,
	Array,
	Table,
	String,
	Reflection
};

// 모듈들이 공유 힐을 쓰는 계약을 선택할 때 사용할 수 있는 원시 할당 함수 형태.
// 현재 ScriptModuleLoadContext는 이 함수를 전달하지 않고 BindHeapAllocator 호출부도 없으므로,
// 각 정적 링크 모듈은 서로 다른 기본 할당기 사본을 쓴다.
using HeapAllocateFunc = void* (*)(std::size_t size, std::size_t alignment);
using HeapFreeFunc     = void  (*)(void* memory, std::size_t size, std::size_t alignment);

// ⚠ 정의는 Allocator.cpp 에 있다. 헤더에 두면 안 된다.
//
// 정의를 .cpp에 두어 각 모듈의 기본 할당기 사본을 명시적으로 유지한다. 다른 모듈이 만든
// Array/Table 저장소를 이 모듈의 연산으로 재할당·해제하면 안 된다. 스크립트 리플렉션 편집은
// 해당 DLL이 제공하는 연산을 통하거나, 별도로 확정한 공유 할당기 ABI를 통해야 한다.
struct HeapAllocator
{
	static void* Allocate(
		std::size_t size,
		std::size_t alignment,
		EMemoryTag tag = EMemoryTag::Unknown);

	static void Deallocate(
		void* memory,
		std::size_t size,
		std::size_t alignment,
		EMemoryTag tag = EMemoryTag::Unknown) noexcept;
};

// 이 모듈의 Array/Table 할당기를 외부 함수로 1회 고정한다. 현재 스크립트 DLL ABI에는
// 연결되지 않은 보류 API다. 이미 할당한 뒤 반납처를 바꾸면 안 되므로 두 번째 호출부터는 무시한다.
void BindHeapAllocator(HeapAllocateFunc allocate, HeapFreeFunc free);
}
