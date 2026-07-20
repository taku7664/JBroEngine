#pragma once

#include <cstddef>

enum class EMemoryTag : unsigned char
{
	Unknown,
	Array,
	Table,
	String,
	Reflection
};

// 호스트가 게임 DLL 에 넘겨 주는 할당 함수의 형태. GameModuleHostApi 의 Allocate/Free 와
// 시그니처가 같아 어댑터 없이 그대로 연결된다(Utillity 가 Core 에 의존하지 않도록 여기 둔다).
using HeapAllocateFunc = void* (*)(std::size_t size, std::size_t alignment);
using HeapFreeFunc     = void  (*)(void* memory, std::size_t size, std::size_t alignment);

// ⚠ 정의는 Allocator.cpp 에 있다. 헤더에 두면 안 된다.
//
// operator new 는 프로그램에 하나가 아니라 **모듈마다 자기 CRT 힙**을 쓴다. 정의가 헤더에 있으면
// 게임 DLL 이 자기 operator new 로 인라인해 버리고, 그러면 에디터가 인스펙터로 채운 Array 버퍼를
// (호스트 힙) DLL 이 해제하려 들어(DLL 힙) 크래시한다. 스크립트 DLL 의 빌드 구성은 .jproject 의
// 사용자 설정이라 호스트와 CRT 가 다를 수 있다(Debug 에디터 + Release 스크립트).
//
// .cpp 로 내려야 호출이 함수 포인터를 타고, DLL 사본이 호스트 힙 하나로 모인다.
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

// 이 모듈의 힙을 호스트 것으로 **1회** 고정한다. 두 번째 호출부터는 무시된다 — 이미 나눠 준
// 메모리의 해제 경로가 도중에 바뀌면 반납처가 어긋나기 때문이다(스크립트가 다시 불러도 무해).
// 호스트는 부를 필요가 없다(기본값이 자기 operator new). 게임 DLL 이 로드 직후 1회 부른다.
void BindHeapAllocator(HeapAllocateFunc allocate, HeapFreeFunc free);
