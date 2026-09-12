#include <JBro/Types/Allocator.h>

#include <algorithm>
#include <cstdlib>
#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace JBro
{
namespace
{
	// 각 정적 링크 모듈의 기본 Array/Table 할당기. 외부 할당기를 바인딩하기 전에 만든
	// 저장소는 같은 기본 함수로 반납되어야 한다. 현재 스크립트 ABI는 외부 할당기를 바인딩하지 않는다.
	void* DefaultAllocate(std::size_t size, std::size_t alignment)
	{
		const std::size_t effectiveSize      = std::max<std::size_t>(size, 1);
		const std::size_t effectiveAlignment = std::max<std::size_t>(alignment, alignof(void*));
#if defined(_MSC_VER)
		return _aligned_malloc(effectiveSize, effectiveAlignment);
#else
		// std::aligned_alloc 은 크기가 정렬의 배수여야 한다.
		const std::size_t remainder   = effectiveSize % effectiveAlignment;
		const std::size_t alignedSize = (0 == remainder)
			? effectiveSize
			: effectiveSize + (effectiveAlignment - remainder);
		return std::aligned_alloc(effectiveAlignment, alignedSize);
#endif
	}

	void DefaultFree(void* memory, std::size_t size, std::size_t alignment)
	{
		(void)size;
		(void)alignment;
#if defined(_MSC_VER)
		_aligned_free(memory);
#else
		std::free(memory);
#endif
	}

	// Core는 호스트와 게임 DLL에 각각 정적 링크되므로 이 포인터들도 모듈마다 별개의 사본이다.
	// 현재는 모든 사본이 기본값을 쓰며, 스크립트 컨테이너 저장소를 호스트가 직접 재할당·해제하지 않는다.
	HeapAllocateFunc g_allocate = &DefaultAllocate;
	HeapFreeFunc     g_free     = &DefaultFree;
	bool             g_bound    = false;
}

void* HeapAllocator::Allocate(std::size_t size, std::size_t alignment, EMemoryTag tag) const
{
	// 태그는 경계를 넘지 못한다 — 호스트가 넘겨 주는 할당 함수가 size/alignment 만 받기 때문이다.
	// 진단용 값이라 기능 손실은 없지만, DLL 이 잡은 몫은 태그 없이 집계된다.
	(void)tag;
	if (0 == size)
	{
		return nullptr;
	}

	return g_allocate(size, alignment);
}

void HeapAllocator::Deallocate(void* memory, std::size_t size, std::size_t alignment, EMemoryTag tag) const noexcept
{
	(void)tag;
	if (nullptr == memory)
	{
		return;
	}

	g_free(memory, size, alignment);
}

void* JAllocatorRef::Allocate(std::size_t size, std::size_t alignment, EMemoryTag tag) const
{
	if (0 == size)
	{
		return nullptr;
	}
	// 묶이지 않은 참조는 기본 힙으로 되돌아간다. 호스트가 frame 을 채우지 않은 프레임에서도
	// 컨테이너가 동작해야 하기 때문이다.
	if (nullptr == m_allocator || nullptr == m_allocator->allocate)
	{
		return HeapAllocator{}.Allocate(size, alignment, tag);
	}

	return m_allocator->allocate(m_allocator->userData, size, alignment);
}

void JAllocatorRef::Deallocate(void* memory, std::size_t size, std::size_t alignment, EMemoryTag tag) const noexcept
{
	if (nullptr == memory)
	{
		return;
	}
	if (nullptr == m_allocator || nullptr == m_allocator->allocate)
	{
		HeapAllocator{}.Deallocate(memory, size, alignment, tag);
		return;
	}
	// free 가 없는 할당기는 선형 할당기처럼 프레임 단위로 한꺼번에 되감는 쪽이다.
	if (nullptr != m_allocator->free)
	{
		m_allocator->free(m_allocator->userData, memory);
	}
}

void BindHeapAllocator(HeapAllocateFunc allocate, HeapFreeFunc free)
{
	// 1회만 받는다. 이미 나눠 준 메모리의 반납처가 도중에 바뀌면 해제가 엉뚱한 힙으로 가므로,
	// 늦은 호출은 성공시키는 것보다 무시하는 편이 안전하다.
	if (g_bound || nullptr == allocate || nullptr == free)
	{
		return;
	}

	g_allocate = allocate;
	g_free     = free;
	g_bound    = true;
}
}
