#pragma once
#include <JBro/Editor/Code/Analysis.h>

namespace JBro::Code
{
    struct HoverResult
    {
        UInt64 requestId = 0;
        Int32 version = 0;
        UInt64 revision = 0;
        Bool ready = false, success = false;
        Position position;
        Range range;
        String text, error;
    };
    struct ReferencesResult
    {
        UInt64 requestId = 0;
        Int32 version = 0;
        UInt64 revision = 0;
        Bool ready = false, success = false;
        Position position;
        Array<Range> ranges;
        String error;
    };
}
