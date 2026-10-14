#pragma once

#include <JBro/ScriptCompiler/ApiCatalog.h>
#include <JBro/ScriptCompiler/SyntaxTree.h>
#include <JBro/Types/ArrayView.h>

namespace JBro::ScriptCompiler
{
    struct CppEmitResult
    {
        String Code;
        Array<String> Errors;
    };

    struct CppSource
    {
        const SourceText* Source = nullptr;
        const SyntaxTree* Tree = nullptr;
    };

    // Emits C++ for a 2D game DLL. Unsupported syntax is reported
    // explicitly; a partial translation must never be compiled as a script.
    CppEmitResult EmitCpp(const SourceText& source, const SyntaxTree& tree,
        const ApiCatalog& api, bool emitModuleEntry = true);
    // Emits one translation unit for all files in a game project. Type
    // declarations are ordered across files; source locations remain per file.
    CppEmitResult EmitCpp(ArrayView<const CppSource> sources, const ApiCatalog& api,
        bool emitModuleEntry = true);
}
