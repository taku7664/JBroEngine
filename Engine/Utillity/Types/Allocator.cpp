#include "Utillity/Types/Allocator.h"

#include <algorithm>
#include <cstdlib>
#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace
{
	// ⚠ 기본 구현은 호스트가 넘겨 주는 GameModuleHostApi 의 Allocate/Free 와 **같은 원시 함수**를
	//   써야 한다. 여기만 ::operator new 로 두면, 바인딩 전에 잡은 메모리를 바인딩 후에 해제할 때
	//   (또는 호스트에서 잡아 DLL 에서 해제할 때) operator new 로 받은 포인터가 _aligned_free 로
	//   넘어가 디버그 CRT 가 assert 한다. 실제로 그렇게 터졌다 —
	//   AllocateModuleMemory/FreeModuleMemory(LiveCompileManager.cpp 등)와 반드시 짝을 맞출 것.
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

	// 이 모듈이 쓰는 힙. Engine.lib 는 호스트와 게임 DLL 에 각각 정적 링크되므로 이 두 포인터도
	// 모듈마다 **별개의 사본**이다. 호스트 사본은 기본값을 끝까지 쓰고, DLL 사본만 로드 직후
	// BindHeapAllocator 로 호스트 것을 받아 간다. 위 주석대로 둘은 서로 교환 가능해야 한다.
	HeapAllocateFunc g_allocate = &DefaultAllocate;
	HeapFreeFunc     g_free     = &DefaultFree;
	bool             g_bound    = false;
}

void* HeapAllocator::Allocate(std::size_t size, std::size_t alignment, EMemoryTag tag)
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

void HeapAllocator::Deallocate(void* memory, std::size_t size, std::size_t alignment, EMemoryTag tag) noexcept
{
	(void)tag;
	if (nullptr == memory)
	{
		return;
	}

	g_free(memory, size, alignment);
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
