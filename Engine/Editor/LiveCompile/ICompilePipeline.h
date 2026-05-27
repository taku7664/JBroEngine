#pragma once

#include "Editor/LiveCompile/LiveCompileTypes.h"

#include <future>

class ICompilePipeline
{
public:
	virtual ~ICompilePipeline() = default;

public:
	// 동기 컴파일. 호출자 스레드를 빌드 종료까지 블로킹.
	virtual LiveCompileResult Compile(const LiveCompileDesc& desc) = 0;

	// 비동기 컴파일. 즉시 future 반환, 워커 스레드에서 빌드 수행.
	// future.wait_for(0ms) 으로 완료 여부 폴링 가능.
	// 호출자(메인 스레드)는 future.get() 후 DLL 교체 같은 메인-스레드 한정 작업을 수행해야 한다.
	virtual std::future<LiveCompileResult> CompileAsync(LiveCompileDesc desc) = 0;
};
