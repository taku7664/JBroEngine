#pragma once
#include <JBro/Editor/Code/Analysis.h>

namespace JBro::Code
{
    struct TextEdit
    {
        Range range;
        String newText;
    };
    enum class SummaryKind { Type, Field, Function };
    struct FieldAttribute
    {
        String name;
        Array<String> arguments;
    };
    struct EffectiveFieldSettings
    {
        Bool property = false;
        Bool readOnly = false;
        Bool noSerialize = false;
        Bool hidden = false;
    };
    struct SummaryEntry
    {
        SummaryKind kind = SummaryKind::Type;
        String name, type, signature, access;
        Range range, selectionRange;
        Bool isStatic = false, isConst = false, isRef = false, isCallback = false;
        Bool initializerPresent = false;
        String initializer;
        Array<FieldAttribute> attributes;
        EffectiveFieldSettings effective;
        Array<SummaryEntry> members;
    };
    struct ScriptSummary
    {
        Int32 version = 0;
        UInt64 revision = 0;
        Bool valid = false;
        Array<SummaryEntry> types;
    };
    struct FieldSettings
    {
        Bool replaceInitializer = false;
        Bool initializerPresent = false;
        String initializer;
        Array<FieldAttribute> attributes;
    };
    struct FieldEditResult
    {
        UInt64 requestId = 0;
        Int32 version = 0;
        Bool ready = false, success = false;
        String error;
        Array<TextEdit> edits;
    };
}
