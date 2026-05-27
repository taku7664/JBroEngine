#pragma once

#include "Core/Game/GameModuleTypes.h"

class IGameModule
{
public:
	virtual ~IGameModule() = default;

public:
	virtual bool Initialize(const GameModuleContext& context) = 0;
	virtual void Tick() = 0;
	virtual void Finalize() = 0;

	virtual const GameModuleDesc& GetDesc() const = 0;
};
