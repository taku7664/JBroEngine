#pragma once
#include <JBro/ScriptCompiler/CppEmitter.h>
#include <filesystem>

namespace JBro::CompilerOutput
{
    Bool WriteProject(const std::filesystem::path& directory,
        const ScriptCompiler::CppProjectResult& project, String& error);
}
