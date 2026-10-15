#pragma once
#include <JBro/Editor/Code/Summary.h>
#include <JBro/Types/ArrayView.h>
#include <JBro/Editor/Widget/Common.h>

namespace JBro::Widget
{
    struct ScriptOutlineItem
    {
        String key, name, detail;
        Code::SummaryKind kind = Code::SummaryKind::Type;
        UInt32 depth = 0;
        String tooltip;
    };
    void ScriptSummaryHeader(const char* filename, const char* path, UInt32 fields, UInt32 functions,
        const char* status = nullptr, Bool warning = false);
    Bool BeginScriptOutline();
    void EndScriptOutline();
    Bool ScriptOutline(ArrayView<const ScriptOutlineItem> items, String& selected);
    Bool BeginScriptDetails();
    void EndScriptDetails();
    void ScriptMemberHeader(const char* name, const char* type, const char* qualifiers, Code::SummaryKind kind);
    void ScriptSummaryNote(const char* text, Bool error = false);
    Bool ScriptInitializerField(const char* id, String& text, Bool& present, Bool invalid = false);
}
