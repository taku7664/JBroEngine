#pragma once

#include <JBro/ScriptCompiler/Analysis.h>

namespace JBro::ScriptCompiler
{
    struct FieldAttributeDefinition
    {
        const char* Name;
        const char* NativeName;
        UInt32 ArgumentCount;
    };

    ArrayView<const FieldAttributeDefinition> GetFieldAttributeDefinitions();
    const FieldAttributeDefinition* FindFieldAttributeDefinition(std::string_view name);

    struct SummaryAttribute
    {
        String Name;
        Array<String> Arguments;
    };

    struct EffectiveFieldSettings
    {
        Bool Property = false;
        Bool ReadOnly = false;
        Bool NoSerialize = false;
        Bool Hidden = false;
    };

    enum class SummaryEntryKind : unsigned char { Type, Field, Function };

    struct SummaryEntry
    {
        SummaryEntryKind Kind = SummaryEntryKind::Type;
        String Name;
        String Type;
        String Signature;
        String Access;
        SourceRange Range;
        SourceRange SelectionRange;
        Bool IsStatic = false;
        Bool IsConst = false;
        Bool IsRef = false;
        Bool IsCallback = false;
        Bool HasInitializer = false;
        String Initializer;
        Array<SummaryAttribute> Attributes;
        EffectiveFieldSettings Effective;
        Array<SummaryEntry> Members;
    };

    struct ScriptSummary
    {
        Bool Valid = false;
        Array<SummaryEntry> Types;
    };

    ScriptSummary SummarizeScript(const AnalysisResult& analysis, const DocumentAnalysis& document);

    struct FieldSettingsChange
    {
        Bool ChangeInitializer = false;
        Bool HasInitializer = false;
        String Initializer;
        // Only recognized attributes are replaced. Unknown source attributes are preserved.
        Bool ChangeAttributes = false;
        Array<SummaryAttribute> Attributes;
    };

    struct SummaryTextEdit
    {
        SourceRange Range;
        String NewText;
    };

    // Produces non-overlapping UTF-8 byte edits without changing the analyzed source.
    // The caller must enforce its document version before applying the edits.
    // On failure, error contains a key for DiagnosticMessages::FormatKey.
    Bool EditFieldSettings(const DocumentAnalysis& document, UInt32 selectionOffset,
        const FieldSettingsChange& change, Array<SummaryTextEdit>& edits, String& error);
}
