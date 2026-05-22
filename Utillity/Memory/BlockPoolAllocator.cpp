#include "pch.h"
#include "BlockPoolAllocator.h"

#include <cstdlib>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

CBlockPoolAllocator::CBlockPoolAllocator(std::size_t blockSize, std::size_t blockAlignment, std::size_t blocksPerChunk)
{
	Initialize(blockSize, blockAlignment, blocksPerChunk);
}

CBlockPoolAllocator::~CBlockPoolAllocator()
{
	Reset();
}

bool CBlockPoolAllocator::Initialize(std::size_t blockSize, std::size_t blockAlignment, std::size_t blocksPerChunk)
{
	Reset();

	m_blockAlignment = std::max<std::size_t>(blockAlignment, alignof(void*));
	m_blockSize = std::max<std::size_t>(blockSize, sizeof(FreeBlock));
	m_blockStride = CalculateBlockStride(m_blockSize, m_blockAlignment);
	m_blocksPerChunk = std::max<std::size_t>(blocksPerChunk, 1);

	return Grow();
}

void CBlockPoolAllocator::Reset()
{
	Chunk* chunk = m_chunks;
	while (chunk)
	{
		Chunk* next = chunk->Next;
#if defined(_MSC_VER)
		_aligned_free(chunk->Memory);
#else
		std::free(chunk->Memory);
#endif
		delete chunk;
		chunk = next;
	}

	m_freeList = nullptr;
	m_chunks = nullptr;
	m_stats = AllocatorStats{};
}

void* CBlockPoolAllocator::Allocate(std::size_t size, std::size_t alignment)
{
	if (0 == m_blockStride)
	{
		return nullptr;
	}

	if (size > m_blockSize || alignment > m_blockAlignment)
	{
		return nullptr;
	}

	if (nullptr == m_freeList && false == Grow())
	{
		return nullptr;
	}

	FreeBlock* block = m_freeList;
	m_freeList = m_freeList->Next;

	m_stats.TotalAllocatedBytes += m_blockSize;
	m_stats.ActiveAllocatedBytes += m_blockSize;
	++m_stats.AllocationCount;

	return block;
}

void CBlockPoolAllocator::Free(void* ptr, std::size_t)
{
	if (nullptr == ptr)
	{
		return;
	}

	FreeBlock* block = static_cast<FreeBlock*>(ptr);
	block->Next = m_freeList;
	m_freeList = block;

	if (m_blockSize <= m_stats.ActiveAllocatedBytes)
	{
		m_stats.ActiveAllocatedBytes -= m_blockSize;
	}
	else
	{
		m_stats.ActiveAllocatedBytes = 0;
	}
	++m_stats.FreeCount;
}

const AllocatorStats& CBlockPoolAllocator::GetStats() const
{
	return m_stats;
}

bool CBlockPoolAllocator::Grow()
{
	const std::size_t chunkSize = m_blockStride * m_blocksPerChunk;
#if defined(_MSC_VER)
	void* memory = _aligned_malloc(chunkSize, m_blockAlignment);
#else
	void* memory = std::aligned_alloc(m_blockAlignment, chunkSize);
#endif
	if (nullptr == memory)
	{
		return false;
	}

	Chunk* chunk = new Chunk();
	chunk->Memory = memory;
	chunk->Next = m_chunks;
	m_chunks = chunk;

	std::byte* bytes = static_cast<std::byte*>(memory);
	for (std::size_t i = 0; i < m_blocksPerChunk; ++i)
	{
		FreeBlock* block = reinterpret_cast<FreeBlock*>(bytes + (i * m_blockStride));
		block->Next = m_freeList;
		m_freeList = block;
	}

	return true;
}

std::size_t CBlockPoolAllocator::CalculateBlockStride(std::size_t blockSize, std::size_t blockAlignment) const
{
	const std::size_t remainder = blockSize % blockAlignment;
	if (0 == remainder)
	{
		return blockSize;
	}

	return blockSize + (blockAlignment - remainder);
}
