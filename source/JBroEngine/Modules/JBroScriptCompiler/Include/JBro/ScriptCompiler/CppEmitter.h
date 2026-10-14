#pragma once

#include <JBro/ScriptCompiler/ApiCatalog.h>
#include <JBro/ScriptCompiler/SyntaxTree.h>

namespace JBro::ScriptCompiler
{
    struct CppEmitResult
    {
        String Code;
        Array<String> Errors;
    };

    // Emits one source file for a 2D game DLL. Unsupported syntax is reported
    // explicitly; a partial translation must never be compiled as a script.
    CppEmitResult EmitCpp(const SourceText& source, const SyntaxTree& tree,
        const ApiCatalog& api);
}
