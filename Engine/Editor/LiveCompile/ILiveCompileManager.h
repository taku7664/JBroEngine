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
};
