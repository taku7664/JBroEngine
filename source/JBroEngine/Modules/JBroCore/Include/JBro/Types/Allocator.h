#pragma once

#include <cstddef>

namespace JBro
{
// MSVC 는 표준 철자의 [[no_unique_address]] 를 ABI 호환을 위해 무시한다.
// 상태 없는 정책이 컨테이너 크기를 늘리지 않으려면 공급사 철자가 필요하다.
#if defined(_MSC_VER)
#define JBRO_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define JBRO_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

enum class EMemoryTag : unsigned char
{
	Unknown,
	Array,
	Table,
	String,
	Reflection
};

// 할당자 인터페이스. 함수 포인터 세트로 두어 어느 모듈에서든 값으로 넘길 수 있다.
struct JAllocator
{
	void* userData = nullptr;
	void* (*allocate)(void* userData, std::size_t size, std::size_t alignment) = nullptr;
	void  (*free)   (void* userData, void* memory) = nullptr;
	void* (*reallocate)(void* userData, void* memory, std::size_t newSize, std::size_t alignment) = nullptr;
};

// 모듈들이 공유 힙을 쓰는 계약을 선택할 때 사용할 수 있는 원시 할당 함수 형태.
// 현재 ScriptModuleLoadContext는 이 함수를 전달하지 않고 BindHeapAllocator 호출부도 없으므로,
// 각 정적 링크 모듈은 서로 다른 기본 할당기 사본을 쓴다.
using HeapAllocateFunc = void* (*)(std::size_t size, std::size_t alignment);
using HeapFreeFunc     = void  (*)(void* memory, std::size_t size, std::size_t alignment);

// ⚠ 정의는 Allocator.cpp 에 있다. 헤더에 두면 안 된다.
//
// 정의를 .cpp에 두어 각 모듈의 기본 할당기 사본을 명시적으로 유지한다. 다른 모듈이 만든
// Array/Table 저장소를 이 모듈의 연산으로 재할당·해제하면 안 된다. 스크립트 리플렉션 편집은
// 해당 DLL이 제공하는 연산을 통하거나, 별도로 확정한 공유 할당기 ABI를 통해야 한다.
//
// 상태 없는 정책이다. 멤버로 들고 다녀도 JBRO_NO_UNIQUE_ADDRESS 덕분에 크기가 늘지 않는다.
struct HeapAllocator
{
	void* Allocate(
		std::size_t size,
		std::size_t alignment,
		EMemoryTag tag = EMemoryTag::Unknown) const;

	void Deallocate(
		void* memory,
		std::size_t size,
		std::size_t alignment,
		EMemoryTag tag = EMemoryTag::Unknown) const noexcept;
};

// 이 모듈의 Array/Table 할당기를 외부 함수로 1회 고정한다. 현재 스크립트 DLL ABI에는
// 연결되지 않은 보류 API다. 이미 할당한 뒤 반납처를 바꾸면 안 되므로 두 번째 호출부터는 무시한다.
void BindHeapAllocator(HeapAllocateFunc allocate, HeapFreeFunc free);

// 남이 소유한 JAllocator 를 가리키는 정책이다(D-52). 컨테이너보다 오래 사는 할당기를
// 가리켜야 하며, 포인터 하나만큼 컨테이너가 커진다.
// 비어 있는 채로 쓰면 기본 힙으로 되돌아간다 — 호스트가 frame 을 채우지 않은 경우다.
class JAllocatorRef
{
public:
	JAllocatorRef() = default;

	explicit JAllocatorRef(const JAllocator& allocator)
		: m_allocator(&allocator)
	{
	}

	void* Allocate(
		std::size_t size,
		std::size_t alignment,
		EMemoryTag tag = EMemoryTag::Unknown) const;

	void Deallocate(
		void* memory,
		std::size_t size,
		std::size_t alignment,
		EMemoryTag tag = EMemoryTag::Unknown) const noexcept;

	bool IsBound() const
	{
		return m_allocator != nullptr;
	}

private:
	const JAllocator* m_allocator = nullptr;
};
}
