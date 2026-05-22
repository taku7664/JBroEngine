#include "pch.h"
#include "CompilePipeline.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

LiveCompileResult CCompilePipeline::Compile(const LiveCompileDesc& desc)
{
	LiveCompileResult result;
	result.OutputLibraryPath = desc.OutputLibraryPath;
	if (desc.BuildCommand.empty())
	{
		result.Message = "LiveCompile BuildCommand is empty.";
		return result;
	}

	const int exitCode = std::system(desc.BuildCommand.c_str());
	result.ExitCode = exitCode;
	result.Succeeded = 0 == exitCode;
	result.Message = result.Succeeded ? "Compile succeeded." : "Compile failed.";
	return result;
}

#else

LiveCompileResult CCompilePipeline::Compile(const LiveCompileDesc& desc)
{
	(void)desc;
	LiveCompileResult result;
	result.Message = "LiveCompile is editor-only.";
	return result;
}

#endif

