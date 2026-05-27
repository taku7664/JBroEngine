#pragma once

#include "Editor/LiveCompile/ICompilePipeline.h"

class CCompilePipeline final : public ICompilePipeline
{
public:
	LiveCompileResult Compile(const LiveCompileDesc& desc) override;
	std::future<LiveCompileResult> CompileAsync(LiveCompileDesc desc) override;
};
