#pragma once

#include <JBro/ScriptCompiler/SourceText.h>
#include <JBro/Types/Bool.h>

namespace JBro::ScriptCompiler
{
    // Returns false for syntactically invalid input. No file writes are performed.
    Bool FormatSource(const SourceText& source, UInt32 tabSize, Bool insertSpaces, String& formatted);
}
