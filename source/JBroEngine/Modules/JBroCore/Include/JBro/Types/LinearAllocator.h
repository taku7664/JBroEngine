#pragma once

#include <JBro/Types/Allocator.h>
#include <JBro/Types/Array.h>

#include <cstddef>
#include <cstdint>

namespace JBro
{
// 프레임 임시 메모리다(D-52). 한 번 잡은 블록에서 앞으로만 나눠 주고,
// 프레임 경계에서 한꺼번에 되감는다. 개별 해제는 아무 일도 하지 않는다.
//
// 되감기가 살아 있는 객체를 무효화하므로, 여기서 받은 메모리는 그 프레임을 넘겨 살 수 없다.
// 소멸자가 필요한 타입을 여기에 두지 않는다.
//
// 용량이 모자라면 nullptr 을 돌려주지 않고 기본 힙에서 받아 온다 — 프레임 하나가
// 예산을 넘겼다고 화면이 죽는 것보다, 느려지고 카운터에 남는 편이 낫다.
// 그렇게 새어 나간 블록도 Reset 이 함께 거둔다.
class LinearAllocator final
{
public:
	LinearAllocator() = default;
	~LinearAllocator();
	LinearAllocator(const LinearAllocator&) = delete;
	LinearAllocator& operator=(const LinearAllocator&) = delete;
	LinearAllocator(LinearAllocator&&) = delete;
	LinearAllocator& operator=(LinearAllocator&&) = delete;

	bool Initialize(std::size_t capacityBytes);
	void Shutdown();

	// 프레임 경계에서 부른다. 나눠 준 모든 포인터가 이 시점에 무효가 된다.
	void Reset();

	// 값으로 넘길 수 있는 인터페이스다. userData 는 this 이므로
	// 이 객체보다 오래 사는 곳에 보관하면 안 된다.
	JAllocator GetInterface();

	bool        IsInitialized()      const;
	std::size_t GetCapacity()        const;
	std::size_t GetUsedBytes()       const;
	// 이 할당기를 거쳐 나간 요청 수. 리셋해도 줄지 않는다.
	std::size_t GetRequestCount()    const;
	// 용량을 넘겨 기본 힙으로 새어 나간 요청 수. 예산을 재는 카운터다.
	std::size_t GetOverflowCount()   const;
	// 한 프레임에서 가장 많이 쓴 바이트. 예산을 정할 때 본다.
	std::size_t GetPeakUsedBytes()   const;

private:
	struct OverflowBlock
	{
		void*       memory = nullptr;
		std::size_t size = 0;
		std::size_t alignment = 0;
	};

	static void* AllocateThunk(void* userData, std::size_t size, std::size_t alignment);
	static void  FreeThunk(void* userData, void* memory);

	void* Allocate(std::size_t size, std::size_t alignment);
	void  ReleaseOverflow();

	std::byte*           m_base = nullptr;
	std::size_t          m_capacity = 0;
	std::size_t          m_used = 0;
	std::size_t          m_peakUsed = 0;
	std::size_t          m_requestCount = 0;
	std::size_t          m_overflowCount = 0;
	Array<OverflowBlock> m_overflow;
};
}
