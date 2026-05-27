#pragma once

#include "IAllocator.h"

class CBlockPoolAllocator final : public IAllocator
{
public:
	CBlockPoolAllocator() = default;
	CBlockPoolAllocator(std::size_t blockSize, std::size_t blockAlignment, std::size_t blocksPerChunk);
	~CBlockPoolAllocator() override;

	CBlockPoolAllocator(const CBlockPoolAllocator&) = delete;
	CBlockPoolAllocator& operator=(const CBlockPoolAllocator&) = delete;

public:
	bool Initialize(std::size_t blockSize, std::size_t blockAlignment, std::size_t blocksPerChunk);
	void Reset();

	void* Allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) override;
	void Free(void* ptr, std::size_t size = 0) override;
	const AllocatorStats& GetStats() const override;

private:
	struct FreeBlock
	{
		FreeBlock* Next = nullptr;
	};

	struct Chunk
	{
		void* Memory = nullptr;
		Chunk* Next = nullptr;
	};

private:
	bool Grow();
	std::size_t CalculateBlockStride(std::size_t blockSize, std::size_t blockAlignment) const;

private:
	std::size_t m_blockSize = 0;
	std::size_t m_blockAlignment = alignof(std::max_align_t);
	std::size_t m_blockStride = 0;
	std::size_t m_blocksPerChunk = 0;
	FreeBlock* m_freeList = nullptr;
	Chunk* m_chunks = nullptr;
	AllocatorStats m_stats;
};

