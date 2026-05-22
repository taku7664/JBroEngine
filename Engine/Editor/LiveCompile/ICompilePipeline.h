#pragma once

#include "Editor/LiveCompile/LiveCompileTypes.h"

class ICompilePipeline
{
public:
	virtual ~ICompilePipeline() = default;

public:
	virtual LiveCompileResult Compile(const LiveCompileDesc& desc) = 0;
};

