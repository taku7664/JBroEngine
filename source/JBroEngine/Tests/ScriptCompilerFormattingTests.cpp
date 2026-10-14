#include <JBro/ScriptCompiler/Formatting.h>
#include <JBro/ScriptCompiler/Lexer.h>
#include <JBro/Types/Int.h>

#include <iostream>
#include <stdexcept>

namespace
{
    using namespace JBro;
    using namespace JBro::ScriptCompiler;

    void Check(Bool condition, const char* message)
    {
        if (false == condition)
        {
            throw std::runtime_error(message);
        }
    }
}

JBro::Int32 RunScriptCompilerFormattingTests()
{
    using namespace JBro;
    using namespace JBro::ScriptCompiler;
    const String original = "script Enemy\r\n{\r\n// braces { \" are comment text\r\nfn OnUpdate() callback\r\n{\r\nString text = \"{}\"\r\nInt value = 1\r\n}\r\n}\r\n";
    const SourceText source("Enemy.jscript", original);
    String formatted;
    Check(FormatSource(source, 4, true, formatted), "valid source formats");
    Check(formatted.Contains("    fn OnUpdate() callback\r\n    {\r\n        String text = \"{}\""), "block indentation is canonical");
    Check(formatted.Contains("    // braces { \" are comment text\r\n"), "comment content and CRLF survive");
    String again;
    Check(FormatSource(SourceText("Enemy.jscript", formatted), 4, true, again) && again == formatted, "formatting is idempotent");
    DiagnosticList beforeErrors;
    DiagnosticList afterErrors;
    const auto before = Lex(source, beforeErrors);
    const SourceText afterSource("Enemy.jscript", formatted);
    const auto after = Lex(afterSource, afterErrors);
    Check(before.Size() == after.Size(), "formatting preserves token count");
    for (std::size_t index = 0; index < before.Size(); ++index)
    {
        Check(before[index].Kind == after[index].Kind && before[index].Text == after[index].Text, "formatting preserves token meanings");
    }
    String untouched = "untouched";
    Check(false == FormatSource(SourceText("bad.jscript", "script Broken { fn"), 4, true, untouched), "invalid syntax receives no edit");
    Check(untouched == "untouched", "failure preserves caller output");
    String tabs;
    Check(FormatSource(source, 4, false, tabs) && tabs.Contains("\t\tInt value"), "tab indentation honors client option");
    String bom;
    Check(FormatSource(SourceText("bom.jscript", String("\xEF\xBB\xBF").Append(original.View())), 2, true, bom), "BOM source formats");
    Check(bom.View().starts_with("\xEF\xBB\xBF"), "BOM is preserved");
    std::cout << "ScriptCompilerFormatting passed\n";
    return 0;
}
