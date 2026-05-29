#pragma once

#include "Editor/LiveCompile/LiveCompileTypes.h"
#include "Utillity/SafePtr.h"

class ICompilePipeline;
class IDynamicLibrary;
class IGameModule;

class ILiveCompileManager : public EnableSafeFromThis<ILiveCompileManager>
{
public:
	virtual ~ILiveCompileManager() = default;

public:
	virtual bool Initialize(const LiveCompileDesc& desc) = 0;
	virtual void Finalize() = 0;

	virtual void Tick(bool scanSourceChanges) = 0;
	virtual LiveCompileResult RebuildAndReload() = 0;
	virtual IGameModule* GetGameModule() const = 0;
	virtual ELiveCompileState GetState() const = 0;
	// 마지막 실패의 풀 메시지(컴파일러 출력 포함).  소비형 — 한 번 가져가면
	// 자동으로 비워진다.  UI 가 매 프레임 폴링해 새 실패만 팝업으로 띄울 때 사용.
	virtual std::string ConsumeLastFailureMessage() = 0;
};
