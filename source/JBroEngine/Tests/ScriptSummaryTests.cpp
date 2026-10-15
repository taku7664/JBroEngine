#include <JBro/ScriptCompiler/Summary.h>

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace JBro;
    using namespace JBro::ScriptCompiler;

    void Check(Bool value, const char* message)
    {
        if (false == value)
        {
            throw std::runtime_error(message);
        }
    }

    OwnerPtr<AnalysisResult> Analyze(std::string_view text)
    {
        Array<DocumentInput> documents;
        documents.Add({"file:///C:/project/Contents/Summary.jscript", "Summary.jscript", String(text), 7});
        return AnalyzeProject({documents.Data(), documents.Size()}, ApiCatalog());
    }

    String Apply(std::string_view text, Array<SummaryTextEdit> edits)
    {
        std::sort(edits.begin(), edits.end(), [](const SummaryTextEdit& left, const SummaryTextEdit& right)
        {
            return left.Range.Begin.Offset > right.Range.Begin.Offset;
        });
        String result(text);
        for (const SummaryTextEdit& edit : edits)
        {
            result.replace(edit.Range.Begin.Offset, edit.Range.End.Offset - edit.Range.Begin.Offset, edit.NewText);
        }
        return result;
    }

    void TestSummary()
    {
        const auto result = Analyze("class Enemy\n{\n[prop] static Int total = 2\n[prop] const Int max = 5\nref Enemy target\nfn Tick(Float elapsed) callback\n{\n}\n}\nstruct Settings\n{\nInt speed = 3\n}\n");
        const ScriptSummary summary = SummarizeScript(*result, *result->Documents.First());
        Check(summary.Valid && summary.Types.Size() == 2, "summary keeps top-level type declaration order");
        const SummaryEntry& enemy = summary.Types[0];
        Check(enemy.Name == "Enemy" && enemy.Members.Size() == 4, "summary includes unexposed private fields and functions");
        Check(enemy.Members[0].IsStatic && enemy.Members[0].Effective.Property && enemy.Members[0].Effective.NoSerialize,
            "static properties show their effective serialization exclusion");
        Check(enemy.Members[1].IsConst && enemy.Members[1].Effective.ReadOnly && enemy.Members[1].Effective.NoSerialize,
            "const properties show effective read-only and serialization exclusion");
        Check(enemy.Members[2].IsRef && false == enemy.Members[2].Effective.Property,
            "a normal field stays unexposed and ref is declaration metadata");
        Check(enemy.Members[3].Kind == SummaryEntryKind::Function && enemy.Members[3].IsCallback
            && enemy.Members[3].Signature.Contains("Float elapsed"), "functions include callback and parameter metadata");
        Check(summary.Types[1].Members[0].Effective.Property, "struct fields are properties without an explicit prop attribute");
        const auto nested = Analyze("class Outer\n{\npublic struct Inner\n{\n[prop] Int value = 4\n}\n}\n");
        const ScriptSummary nestedSummary = SummarizeScript(*nested, *nested->Documents.First());
        Check(nestedSummary.Valid && nestedSummary.Types[0].Members[0].Kind == SummaryEntryKind::Type
            && nestedSummary.Types[0].Members[0].Members[0].Initializer == "4",
            "nested types and their editable fields appear recursively in source order");
        FieldSettingsChange nestedChange;
        nestedChange.ChangeInitializer = true;
        nestedChange.HasInitializer = true;
        nestedChange.Initializer = "8";
        Array<SummaryTextEdit> nestedEdits;
        String nestedError;
        Check(EditFieldSettings(*nested->Documents.First(), nestedSummary.Types[0].Members[0].Members[0].SelectionRange.Begin.Offset,
            nestedChange, nestedEdits, nestedError), "the summary edits a field inside a real nested type");
        const auto nestedUpdated = Analyze(Apply(nested->Documents.First()->Source.GetText(), nestedEdits).View());
        Check(SummarizeScript(*nestedUpdated, *nestedUpdated->Documents.First()).Types[0].Members[0].Members[0].Initializer == "8",
            "a nested field edit roundtrips through the actual language AST");
        const auto invalid = Analyze("class Outer\n{\nscript Inner\n{\n}\n}\n");
        Check(false == SummarizeScript(*invalid, *invalid->Documents.First()).Valid,
            "nested engine-attached scripts remain invalid syntax");
        ApiCatalog api;
        ApiType base;
        base.Name = "GameScript2D";
        base.Kind = ApiTypeKind::Class;
        ApiMember update;
        update.Name = "OnUpdate";
        update.Type = "Void";
        update.Kind = ApiMemberKind::Function;
        update.IsCallback = true;
        base.Members.Add(std::move(update));
        api.Types.Add(std::move(base));
        const DocumentInput callbackInput{"file:///callback.jscript", "callback.jscript",
            "script CallbackProbe\n{\npublic fn OnUpdate() override\n{\n}\n}\n", 1};
        const auto callback = AnalyzeProject({&callbackInput, 1}, api);
        Check(SummarizeScript(*callback, *callback->Documents.First()).Types[0].Members[0].IsCallback,
            "a recognized engine callback override is marked even without the callback keyword");
        const DocumentInput inheritedInput{"file:///inherited.jscript", "inherited.jscript",
            "script Base\n{\npublic fn OnUpdate() override {}\n}\n"
            "script Derived : Base\n{\npublic fn OnUpdate() override {}\n}\n", 1};
        const auto inherited = AnalyzeProject({&inheritedInput, 1}, api);
        Check(SummarizeScript(*inherited, *inherited->Documents.First()).Types[1].Members[0].IsCallback,
            "callback metadata follows a user script base to its engine callback contract");
    }

    void TestEdits()
    {
        const String source = "\xEF\xBB\xBF" "class Enemy\r\n{\r\n    [prop, name(\"\xED\x95\x9C\xEA\xB8\x80\"), future(7)] // keep attribute comment\r\n    Int hp = (1 + // keep initializer comment\r\n        2) // keep field comment\r\n    Int other = 4\r\n}\r\n";
        const auto analysis = Analyze(source.View());
        const DocumentAnalysis& document = *analysis->Documents.First();
        const ScriptSummary summary = SummarizeScript(*analysis, document);
        Check(summary.Valid, "summary parses BOM and CRLF with comments");
        FieldSettingsChange change;
        change.ChangeInitializer = true;
        change.HasInitializer = true;
        change.Initializer = "8";
        change.ChangeAttributes = true;
        change.Attributes.Add({"range", {"0", "20"}});
        Array<SummaryTextEdit> edits;
        String error;
        Check(EditFieldSettings(document, summary.Types[0].Members[0].SelectionRange.Begin.Offset, change, edits, error),
            "known field settings and initializer are editable from the AST");
        const String edited = Apply(source.View(), edits);
        Check(edited.StartsWith("\xEF\xBB\xBF") && edited.Contains("\r\n"), "edits preserve BOM and CRLF bytes");
        Check(edited.Contains("future(7)") && edited.Contains("// keep attribute comment")
            && edited.Contains("// keep initializer comment") && edited.Contains("// keep field comment")
            && edited.Contains("Int other = 4"), "edits preserve unknown attributes, comments and unrelated declarations");
        const auto updated = Analyze(edited.View());
        const ScriptSummary after = SummarizeScript(*updated, *updated->Documents.First());
        Check(after.Valid && after.Types[0].Members[0].Initializer == "8", "edited source reparses with the requested initializer");
        Check(after.Types[0].Members[0].Attributes.Size() == 2 && after.Types[0].Members[0].Effective.Property,
            "requested known attributes replace only known attributes, keeping unknowns reflected");

        change.Initializer = "1\nInt injected = 5";
        Check(false == EditFieldSettings(document, summary.Types[0].Members[0].SelectionRange.Begin.Offset, change, edits, error)
            && edits.IsEmpty(), "an initializer cannot inject another declaration");
        change.Initializer = "(1 +";
        Check(false == EditFieldSettings(document, summary.Types[0].Members[0].SelectionRange.Begin.Offset, change, edits, error),
            "incomplete expressions are rejected without text edits");
        change.Initializer = "4";
        change.Attributes = {{"range", {"0"}}};
        Check(false == EditFieldSettings(document, summary.Types[0].Members[0].SelectionRange.Begin.Offset, change, edits, error),
            "the compiler attribute schema rejects incorrect arity");
    }

    void TestAddRemove()
    {
        const String text = "class C\n{\nInt value // trailing\n}\n";
        const auto analysis = Analyze(text.View());
        const DocumentAnalysis& document = *analysis->Documents.First();
        const ScriptSummary initial = SummarizeScript(*analysis, document);
        const SummaryEntry& field = initial.Types[0].Members[0];
        const UInt32 position = field.SelectionRange.Begin.Offset;
        FieldSettingsChange change;
        change.ChangeInitializer = true;
        change.HasInitializer = true;
        change.Initializer = "10";
        change.ChangeAttributes = true;
        change.Attributes = {{"prop", {}}, {"tooltip", {"\"tip\""}}};
        Array<SummaryTextEdit> edits;
        String error;
        Check(EditFieldSettings(document, position, change, edits, error), "settings can add attributes and an absent initializer");
        const String added = Apply(text.View(), edits);
        const auto next = Analyze(added.View());
        const ScriptSummary summary = SummarizeScript(*next, *next->Documents.First());
        change.HasInitializer = false;
        change.Attributes.Clear();
        Check(EditFieldSettings(*next->Documents.First(), summary.Types[0].Members[0].SelectionRange.Begin.Offset, change, edits, error),
            "settings can remove all known attributes and the initializer");
        const String removed = Apply(added.View(), edits);
        const auto final = Analyze(removed.View());
        const ScriptSummary last = SummarizeScript(*final, *final->Documents.First());
        Check(last.Valid && false == last.Types[0].Members[0].HasInitializer && last.Types[0].Members[0].Attributes.IsEmpty()
            && removed.Contains("// trailing"), "removal leaves valid code and preserves the trailing comment");
    }

    void TestAttributeTriviaAndSeparators()
    {
        const Array<SummaryAttribute> known = {{"prop", {}}, {"name", {"\"after\""}}, {"hidden", {}}};
        for (const char* declaration : {"[prop, // middle comment\nname(\"before\"), hidden, future(7)] Int value = 1",
            "[prop][name(\"before\")][hidden][future(7)] Int value = 1"})
        {
            const String source = "class C\n{\n" + String(declaration) + "\n}\n";
            const auto analysis = Analyze(source.View());
            const DocumentAnalysis& document = *analysis->Documents.First();
            const ScriptSummary original = SummarizeScript(*analysis, document);
            Check(original.Valid, "attribute grouping fixture is valid source");
            for (UInt32 mask = 0; mask < 8; ++mask)
            {
                FieldSettingsChange change;
                change.ChangeAttributes = true;
                for (std::size_t index = 0; index < known.Size(); ++index)
                {
                    if ((mask.Get() & (1u << index)) != 0)
                    {
                        change.Attributes.Add(known[index]);
                    }
                }
                Array<SummaryTextEdit> edits;
                String error;
                Check(EditFieldSettings(document, original.Types[0].Members[0].SelectionRange.Begin.Offset, change, edits, error),
                    "removing any subset of grouped attributes leaves valid separators and brackets");
                for (std::size_t index = 1; index < edits.Size(); ++index)
                {
                    Check(edits[index - 1].Range.End.Offset <= edits[index].Range.Begin.Offset,
                        "summary source edits are ordered and do not overlap");
                }
                const String edited = Apply(source.View(), edits);
                const auto next = Analyze(edited.View());
                const ScriptSummary summary = SummarizeScript(*next, *next->Documents.First());
                Check(summary.Valid && summary.Types[0].Members[0].Attributes.Size() == change.Attributes.Size() + 1
                    && edited.Contains("future(7)"), "all requested known attributes and the unknown attribute survive a roundtrip");
                if (source.Contains("// middle comment"))
                {
                    Check(edited.Contains("// middle comment"), "deleting surrounding attribute syntax preserves comments inside brackets");
                }
            }
        }
        const auto invalid = Analyze("class C\n{\nInt value = (\n}\n");
        FieldSettingsChange change;
        change.ChangeInitializer = true;
        change.HasInitializer = true;
        change.Initializer = "2";
        Array<SummaryTextEdit> edits;
        String error;
        Check(false == EditFieldSettings(*invalid->Documents.First(), 14, change, edits, error) && edits.IsEmpty(),
            "an unsafe or incomplete syntax tree cannot be changed through the summary");
    }
}

JBro::Int32 RunScriptSummaryTests()
{
    TestSummary();
    TestEdits();
    TestAddRemove();
    TestAttributeTriviaAndSeparators();
    std::cout << "  ScriptSummary tests passed\n";
    return 0;
}
