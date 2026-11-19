#pragma once
#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Bool.h>

namespace JBro::Code
{
    struct Position
    {
        UInt32 line = 0;
        UInt32 character = 0; // LSP UTF-16 code units, zero based.
    };
    struct Range
    {
        Position start;
        Position end;
    };
    struct Diagnostic
    {
        Range range;
        UInt32 severity = 1;
        String code;
        String message;
    };
    struct SemanticToken
    {
        Range range;
        UInt32 kind = 0;
        UInt32 modifiers = 0;
    };
    struct Analysis
    {
        Int32 version = 0;
        UInt64 revision = 0;
        Array<Diagnostic> diagnostics;
        Array<SemanticToken> tokens;
    };
}
