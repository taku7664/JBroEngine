#pragma once

#include <JBro/ScriptCompiler/CppEmitter.h>

namespace JBro::ScriptCompiler::Internal
{
    struct CppDeclarationPart
    {
        String Name;
        String Code;
    };

    struct CppUnitParts
    {
        Array<CppDeclarationPart> Declarations;
        String Code;
        Array<String> Errors;
    };

    CppUnitParts EmitCppParts(ArrayView<const CppSource> sources, const ApiCatalog& api,
        const CppSource& unit, bool emitModuleEntry, const AnalysisResult* analysis);
}
