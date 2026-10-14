#include <JBro/ScriptCompiler/Analysis.h>
#include <JBro/ScriptCompiler/Parser.h>
#include <JBro/ScriptCompiler/CppEmitter.h>

#include <cstdlib>
#include <iostream>

namespace
{
    using namespace JBro;
    using namespace JBro::ScriptCompiler;

    void Check(Bool condition, const char* message)
    {
        if (false == condition)
        {
            std::cerr << "Script analysis: " << message << '\n';
            std::exit(1);
        }
    }

    ApiCatalog Catalog()
    {
        ApiCatalog catalog;
        ApiType& handle = catalog.Types.Emplace();
        handle.Name = "Transform2D";
        handle.Kind = ApiTypeKind::Handle;
        ApiMember& field = handle.Members.Emplace();
        field.Name = "position";
        field.Type = "Int";
        field.Kind = ApiMemberKind::Field;
        field.Writable = true;
        ApiMember& readonlyField = handle.Members.Emplace();
        readonlyField.Name = "valid";
        readonlyField.Type = "Bool";
        readonlyField.Kind = ApiMemberKind::Field;
        ApiMember& next = handle.Members.Emplace();
        next.Name = "next";
        next.Type = "ref Transform2D";
        next.Kind = ApiMemberKind::Field;
        ApiType& base = catalog.Types.Emplace();
        base.Name = "GameScript2D";
        base.Kind = ApiTypeKind::Class;
        ApiMember& callback = base.Members.Emplace();
        callback.Name = "OnUpdate";
        callback.Type = "Void";
        callback.IsCallback = true;
        ApiType& array = catalog.Types.Emplace();
        array.Name = "Array";
        ApiMember& add = array.Members.Emplace();
        add.Name = "Add";
        add.Type = "Void";
        add.Parameters.Add({"value", "T", false, false, {}});
        add.MutatesReceiver = true;
        for (const char* name : {"RemoveAt", "RemoveAtSwap", "Resize", "Reserve", "FutureSizeMutation"})
        {
            ApiMember& member = array.Members.Emplace();
            member.Name = name;
            member.Type = "Void";
            member.Parameters.Add({"index", "Int", false, false, {}});
            member.MutatesReceiver = true;
        }
        ApiType& object = catalog.Types.Emplace();
        object.Name = "GameObject";
        object.Kind = ApiTypeKind::Handle;
        ApiMember& get = object.Members.Emplace();
        get.Name = "GetComponent";
        get.Type = "ref T";
        get.TypeParameters.Add("T");
        return catalog;
    }

    OwnerPtr<AnalysisResult> Analyze(const char* source, const char* other = nullptr)
    {
        Array<DocumentInput> documents;
        documents.Add({"file:///test.jscript", "test.jscript", source, 1});
        if (nullptr != other)
        {
            documents.Add({"file:///other.jscript", "other.jscript", other, 1});
        }
        return AnalyzeProject(documents.View(), Catalog());
    }

    Bool Has(const AnalysisResult& result, const char* code)
    {
        for (const auto& document : result.Documents)
        {
            for (const SemanticDiagnostic& diagnostic : document->Diagnostics)
            {
                if (diagnostic.Code == code)
                {
                    return true;
                }
            }
        }
        return false;
    }

    void TestNullableGrammar()
    {
        SourceText source("nullable.jscript", "script S\n{\n    ref Transform2D t\n    Array<ref S> peers\n    fn F()\n    {\n        Int p = t?.position ?? 0\n        Int q = t!.position\n    }\n}\n");
        DiagnosticList diagnostics;
        const SyntaxTree tree = Parse(source, diagnostics);
        Check(0 == diagnostics.GetCount(), "D-262 nullable declarations and operators parse");
        Check(tree.GetNodeCount() > 10, "nullable grammar produces a tree");
    }

    void TestSingleRefSyntax()
    {
        auto source = Analyze("script S\n{\nref Transform2D first\nref Int maybe = null\nfn F()\n{\nInt x = first.next.position\nInt y = maybe\nref Int copied = maybe\n}\n}\n");
        Check(source->Documents[0]->Diagnostics.IsEmpty(),
            "ref handles and optional values work without a written null check");
        auto unknown = Analyze("script S\n{\nref Transform2D first\nfn F()\n{\nInt x = first.missing\n}\n}\n");
        Check(Has(*unknown, "JBC3032"), "unknown members remain static errors on ref receivers");
        auto legacy = Analyze("script S\n{\nTransform2D? old\n}\n");
        Check(Has(*legacy, "JBC3040"), "legacy question-suffix type has a migration diagnostic");
    }

    void TestCppEmitterChecksEveryRefLink()
    {
        SourceText source("chain.jscript", "class Link\n{\nref Transform2D target2\n}\n"
            "script Probe\n{\nref Link target1\nfn OnUpdate() override\n{\nInt before = 1\n"
            "Int position = target1.target2.position\n}\n}\n");
        DiagnosticList parseDiagnostics;
        const SyntaxTree tree = Parse(source, parseDiagnostics);
        Check(false == parseDiagnostics.HasErrors(), "emitter scenario parses");
        const CppEmitResult emitted = EmitCpp(source, tree, Catalog());
        Check(emitted.Errors.IsEmpty(), "valid chained ref script emits C++");
        Check(emitted.Code.find("ScriptGuard::Require") != String::npos,
            "emitted access uses the callback guard");
        Check(emitted.Code.find("chain.jscript:11:") != String::npos,
            "the failure key identifies the source access site");
        Check(emitted.Code.find("catch (const JBro::ScriptGuard::AbortCallback&)") != String::npos,
            "a generated callback catches only its own abort");
        Check(emitted.Code.find("JBRO_SCRIPT_MODULE_2D_REQUIRING_ERROR_CONTEXT()") != String::npos,
            "generated modules declare the host error sink as a required context");
        Check(emitted.Code.find("MarkGeneratedScript2D") != String::npos,
            "generated scripts mark mixed C++ projects as requiring an error sink");
        const CppEmitResult projectSource = EmitCpp(source, tree, Catalog(), false);
        Check(projectSource.Errors.IsEmpty()
                && projectSource.Code.find("JBRO_SCRIPT_MODULE_2D_REQUIRING_ERROR_CONTEXT()") == String::npos
                && projectSource.Code.find("RegisterScriptType2D<Probe>()") != String::npos,
            "project mode registers the generated script without a duplicate DLL entry point");
        Check(emitted.Code.find("#line 10 \"chain.jscript\"") != String::npos,
            "generated statements point C++ diagnostics back to the JBroScript source");
    }

    void TestCppEmitterRejectsRecursiveOwnedRefs()
    {
        for (const char* text : {
            "class Node\n{\nref Node next\n}\nscript Probe\n{\nref Node root\n}\n",
            "class A\n{\nref B other\n}\nclass B\n{\nref A other\n}\nscript Probe\n{\nref A root\n}\n"
        })
        {
            SourceText source("cycle.jscript", text);
            DiagnosticList diagnostics;
            const SyntaxTree tree = Parse(source, diagnostics);
            Check(false == diagnostics.HasErrors(), "recursive owned ref sample parses");
            const CppEmitResult emitted = EmitCpp(source, tree, Catalog());
            Check(false == emitted.Errors.IsEmpty(),
                "owned ref cycles must be rejected before a C++ compiler sees an infinite-size value");
        }
    }

    void TestCppEmitterCombinesProjectFiles()
    {
        SourceText link("link.jscript", "class Link\n{\npublic ref Transform2D target2\n}\n");
        SourceText probe("probe.jscript", "script Probe\n{\npublic ref Link target1\n"
            "fn OnUpdate() override\n{\nVector2 position = target1.target2.position\n}\n}\n");
        DiagnosticList linkDiagnostics;
        DiagnosticList probeDiagnostics;
        const SyntaxTree linkTree = Parse(link, linkDiagnostics);
        const SyntaxTree probeTree = Parse(probe, probeDiagnostics);
        Check(false == linkDiagnostics.HasErrors() && false == probeDiagnostics.HasErrors(),
            "cross-file emitter scenario parses");
        const CppSource sources[] = {{&link, &linkTree}, {&probe, &probeTree}};
        const CppEmitResult result = EmitCpp(sources, Catalog(), false);
        Check(result.Errors.IsEmpty() && result.Code.find("class Link") != String::npos
                && result.Code.find("class Probe") != String::npos
                && result.Code.find("Probe::OnUpdate()") != String::npos
                && result.Code.find("probe.jscript:6:") != String::npos,
            "one generated translation unit contains both files and retains the failing source location");
    }

    void TestCppEmitterClassConstructionAndStaticMembers()
    {
        SourceText source("members.jscript", "class Counter\n{\npublic const Int step = 2\n"
            "public static Int total = 0\npublic fn Counter(Int start)\n{\n}\n"
            "public static fn Next(const Int amount) -> Int\n{\ntotal += amount\nreturn total\n}\n}\n"
            "script Probe\n{\npublic fn F() -> Int\n{\nCounter c = Counter(3)\n"
            "const Int increment = 1\nreturn c.step + Counter.Next(increment)\n}\n}\n");
        DiagnosticList diagnostics;
        const SyntaxTree tree = Parse(source, diagnostics);
        Check(false == diagnostics.HasErrors(), "class member scenario parses");
        const CppEmitResult emitted = EmitCpp(source, tree, Catalog());
        Check(emitted.Errors.IsEmpty()
                && emitted.Code.find("const JBro::Int step") != String::npos
                && emitted.Code.find("inline static JBro::Int total") != String::npos
                && emitted.Code.find("Counter::Counter(JBro::Int start)") != String::npos
                && emitted.Code.find("Counter::Next(const JBro::Int amount)") != String::npos
                && emitted.Code.find(" + Counter::Next(increment)") != String::npos,
            "constructors, const fields and static members are preserved in generated C++");
    }

    void TestCppEmitterPreservesFieldVisibilityAndReflection()
    {
        SourceText source("fields.jscript", "script Probe\n{\nInt hidden\n[prop]\npublic Int shown\n}\n");
        DiagnosticList diagnostics;
        const SyntaxTree tree = Parse(source, diagnostics);
        Check(false == diagnostics.HasErrors(), "field visibility sample parses");
        const CppEmitResult emitted = EmitCpp(source, tree, Catalog());
        Check(emitted.Errors.IsEmpty(), "field visibility sample emits");
        Check(emitted.Code.find("private:\n    JBro::Int hidden") != String::npos,
            "an unannotated script field remains private and unreflected");
        Check(emitted.Code.find("MakeFieldEntry<&Probe::shown>()") != String::npos
                && emitted.Code.find("field.name = \"shown\"") != String::npos,
            "an annotated public field is registered for scene and inspector reflection");
    }

    void TestStaticPropertiesAreSharedAndNotSerialized()
    {
        auto implicit = Analyze("script S\n{\n[prop]\npublic static Int shared = 3\n}\n");
        Check(Has(*implicit, "JBC3041"),
            "an implicit NoSerialize static property warns that edits are not saved");
        Check(implicit->Documents[0]->Diagnostics[0].Warning,
            "implicit static property persistence diagnostic is a warning");
        auto explicitStorage = Analyze("script S\n{\n[prop, noserialize]\npublic static Int shared = 3\n}\n");
        Check(!Has(*explicitStorage, "JBC3041"),
            "an explicit noserialize static property needs no persistence warning");
        Array<DocumentInput> first;
        first.Add({"file:///static.jscript", "static.jscript",
            "script S\n{\n[prop]\npublic static Int shared = 3\n}\n", 1});
        auto previous = AnalyzeProject(first.View(), Catalog());
        Array<DocumentInput> edited;
        edited.Add({"file:///static.jscript", "static.jscript",
            "script S\n{\n[prop, noserialize]\npublic static Int shared = 3\n}\n", 2});
        auto updated = AnalyzeProjectIncremental(edited.View(), Catalog(), std::move(previous));
        Check(!Has(*updated, "JBC3041"),
            "adding noserialize clears the static property warning during incremental editing");
        SourceText source("static.jscript", "script S\n{\n[prop]\npublic static Int shared = 3\n}\n");
        DiagnosticList diagnostics;
        const SyntaxTree tree = Parse(source, diagnostics);
        const CppEmitResult emitted = EmitCpp(source, tree, Catalog());
        Check(emitted.Errors.IsEmpty()
                && emitted.Code.find("MakeStaticFieldEntry<&S::shared>(NoSerialize())") != String::npos,
            "static script properties use shared reflection storage excluded from instance saves");
    }

    void TestSwitchRejectsNonComparableValues()
    {
        auto result = Analyze("class Box\n{\npublic Int value\n}\n"
            "script S\n{\nfn F()\n{\nBox box\nswitch (box)\n{\ndefault { }\n}\n}\n}\n");
        Check(Has(*result, "JBC3042"),
            "switch rejects class values before C++ emission because they have no equality contract");
    }

    void TestEqualityOperatorsNeedAnExplicitOverload()
    {
        auto missing = Analyze("struct Box\n{\nInt value\n}\n"
            "class Holder\n{\nfn F(Box a, Box b) -> Bool\n{\nreturn a == b\n}\n}\n");
        Check(Has(*missing, "JBC3031"), "user values without operator== fail semantic analysis");

        auto valid = Analyze("struct Box\n{\nInt value\n"
            "fn operator==(Box other) -> Bool\n{\nreturn value == other.value\n}\n"
            "fn operator!=(Box other) -> Bool\n{\nreturn value != other.value\n}\n}\n"
            "class Holder\n{\nfn F(Box a, Box b) -> Bool\n{\nreturn a == b and a != b\n}\n}\n");
        Check(!valid->Documents[0]->ParseDiagnostics.HasErrors()
                && valid->Documents[0]->Diagnostics.IsEmpty(),
            "matching explicit equality overloads accept comparisons");
        const Symbol* equality = valid->Symbols.FindBy([](const Symbol& symbol)
        {
            return symbol.Name == "operator==";
        });
        String renameError;
        Check(equality != nullptr && !CanRenameSymbol(*valid, equality->Id, "Equals", renameError),
            "operator declarations cannot be renamed into ordinary methods");

        auto onlyEqual = Analyze("struct Box\n{\n"
            "fn operator==(Box other) -> Bool\n{\nreturn true\n}\n"
            "fn F(Box other) -> Bool\n{\nreturn this != other\n}\n}\n");
        Check(Has(*onlyEqual, "JBC3031"), "operator!= needs its own declaration");

        auto ownedRefs = Analyze("class Holder\n{\n"
            "ref Int first\nref Int second\n"
            "fn Compare() -> Bool\n{\nreturn first == second\n}\n"
            "fn CompareNull() -> Bool\n{\nreturn first != null\n}\n"
            "fn CheckNull() -> Bool\n{\nreturn first is null\n}\n}\n");
        Check(ownedRefs->Documents[0]->Diagnostics.Size() == 2
                && ownedRefs->Documents[0]->Diagnostics[0].Code == "JBC3031"
                && ownedRefs->Documents[0]->Diagnostics[1].Code == "JBC3031",
            "owned nullable refs reject both value and null equality while is null remains valid");

        auto otherParameter = Analyze("struct Box\n{\n"
            "fn operator==(Int other) -> Bool\n{\nreturn true\n}\n"
            "fn F(Int other) -> Bool\n{\nreturn this == other\n}\n}\n");
        Check(otherParameter->Documents[0]->Diagnostics.IsEmpty(),
            "an equality overload can accept a right operand of another value type");

        auto wrongArity = Analyze("struct Box\n{\n"
            "fn operator==() -> Bool\n{\nreturn true\n}\n}\n");
        Check(Has(*wrongArity, "JBC3043"), "an equality operator requires one right operand");

        auto inherited = Analyze("class Base\n{\n"
            "public fn operator==(Int other) -> Bool\n{\nreturn true\n}\n}\n"
            "class Child : Base\n{\nfn F(Int other) -> Bool\n{\nreturn this == other\n}\n}\n");
        Check(inherited->Documents[0]->Diagnostics.IsEmpty(),
            "a derived value can use an accessible inherited equality overload");

        auto hidden = Analyze("class Base\n{\n"
            "public fn operator==(Int other) -> Bool\n{\nreturn true\n}\n}\n"
            "class Child : Base\n{\n"
            "public fn operator==(String other) -> Bool\n{\nreturn true\n}\n"
            "fn F(Int other) -> Bool\n{\nreturn this == other\n}\n}\n");
        Check(Has(*hidden, "JBC3031"),
            "a child equality declaration hides base overloads that generated C++ cannot call");

        auto wrongReturn = Analyze("struct Box\n{\n"
            "fn operator==(Box other) -> Int\n{\nreturn 1\n}\n}\n");
        Check(Has(*wrongReturn, "JBC3043"), "an equality operator must return Bool");

        auto staticOperator = Analyze("struct Box\n{\n"
            "static fn operator==(Box other) -> Bool\n{\nreturn true\n}\n}\n");
        Check(Has(*staticOperator, "JBC3043"), "an equality operator cannot be static");
    }

    void TestCppEmitterFloatLiteralMatchesEngineValueType()
    {
        SourceText source("float.jscript", "script Probe\n{\npublic Float factor = 0.5\n}\n");
        DiagnosticList diagnostics;
        const SyntaxTree tree = Parse(source, diagnostics);
        Check(false == diagnostics.HasErrors(), "float sample parses");
        const CppEmitResult emitted = EmitCpp(source, tree, Catalog());
        Check(emitted.Errors.IsEmpty() && emitted.Code.find("0.5f") != String::npos,
            "Float literals are emitted as float, not a double conversion");
    }

    void TestWitnessRegressions()
    {
        auto assignment = Analyze("class Box\n{\npublic ref GameObject target\n}\nclass Reader\n{\nBox box\nfn F()\n{\nif (box.target is not null)\n{\nbox.target = null\n}\n}\n}\n");
        auto unreachableReturn = Analyze("class Reader\n{\nfn F() -> Int\n{\nswitch (1)\n{\ndefault\n{\nbreak\nreturn 1\n}\n}\n}\n}\n");
        auto removal = Analyze("class Reader\n{\nArray<Int> values\nfn F()\n{\nfor (value in values)\n{\nvalues.RemoveAtSwap(0)\n}\n}\n}\n");
        const Bool assignmentPassed = assignment->Documents[0]->Diagnostics.IsEmpty();
        const Bool returnPassed = Has(*unreachableReturn, "JBC3011");
        const Bool mutationPassed = Has(*removal, "JBC3019");
        if (false == assignmentPassed)
        {
            std::cerr << "  witness regression failed: nullable member assignment used a narrowed storage type\n";
        }
        if (false == returnPassed)
        {
            std::cerr << "  witness regression failed: return after break satisfied the function return path\n";
        }
        if (false == mutationPassed)
        {
            std::cerr << "  witness regression failed: RemoveAtSwap changed the iterated container without a diagnostic\n";
        }
        Check(assignmentPassed && returnPassed && mutationPassed, "all three independently reproduced witness regressions must be fixed");
    }

    void TestJumpControlFlowAndContainerEffects()
    {
        auto conditionalBreak = Analyze("class Reader\n{\nBool condition = true\nfn F() -> Int\n{\nswitch (1)\n{\ndefault\n{\nif (condition)\n{\nbreak\n}\nreturn 1\n}\n}\n}\n}\n");
        Check(Has(*conditionalBreak, "JBC3011"), "a conditional break still leaves a missing return path through switch");
        auto liveReturn = Analyze("class Reader\n{\nfn F() -> Int\n{\nswitch (1)\n{\ndefault\n{\nbreak\n}\n}\nreturn 1\n}\n}\n");
        Check(liveReturn->Documents[0]->Diagnostics.IsEmpty(), "a return after switch is reachable when a case breaks");
        auto exhaustive = Analyze("enum Mode\n{\nFirst\nSecond\n}\nclass Reader\n{\nMode mode = Mode.First\nfn F() -> Int\n{\nswitch (mode)\n{\ncase Mode.First\n{\nreturn 1\n}\ncase Mode.Second\n{\nreturn 2\n}\n}\n}\n}\n");
        Check(exhaustive->Documents[0]->Diagnostics.IsEmpty(), "all returning enum cases satisfy the return path");
        auto innerLoop = Analyze("class Reader\n{\nBool condition = false\nfn F() -> Int\n{\nswitch (1)\n{\ndefault\n{\nwhile (condition)\n{\ncontinue\n}\nreturn 1\n}\n}\n}\n}\n");
        Check(innerLoop->Documents[0]->Diagnostics.IsEmpty(), "continue belongs to the inner loop and does not make the later return unreachable");
        auto reserve = Analyze("class Reader\n{\nArray<Int> values\nfn F()\n{\nfor (value in values)\n{\nvalues.Reserve(0)\n}\n}\n}\n");
        Check(false == Has(*reserve, "JBC3019"), "capacity-only Reserve is not classified as a size mutation");
        for (const char* method : {"RemoveAt", "RemoveAtSwap", "Resize", "FutureSizeMutation"})
        {
            String source("class Reader\n{\nArray<Int> values\nfn F()\n{\nfor (value in values)\n{\nvalues.");
            source.Append(method).Append("(0)\n}\n}\n}\n");
            auto result = Analyze(source.c_str());
            Check(Has(*result, "JBC3019"), "every mutating container API except known capacity-only operations is checked");
        }
    }

    void TestCrossFileSymbolsAndOverloads()
    {
        auto result = Analyze("class Client\n{\n    Worker value\n    fn Run() -> Int\n    {\n        return value.F(3)\n    }\n}\n", "class Worker\n{\n    public fn F(Int x) -> Int\n    {\n        return x\n    }\n    public fn F(String x) -> Int\n    {\n        return 1\n    }\n}\n");
        Check(result->Documents[0]->Diagnostics.IsEmpty(), "cross-file overload selection succeeds");
        const CallSite& call = result->Documents[0]->Calls.Last();
        Check(2 == call.Overloads.Size() && InvalidSymbol != call.Resolved, "calls retain all overloads and the selected target");
        const Symbol* selected = FindSymbol(*result, call.Resolved);
        Check(nullptr != selected && selected->Parameters[0].Type == "Int", "exact Int overload is selected");
        Check(selected->SelectionRange.End.Offset - selected->SelectionRange.Begin.Offset == 1, "function selection range covers only its name");
        Check(result->Documents[1]->Tree.Get(result->Documents[1]->Tree.GetChild(result->Documents[1]->Tree.GetRoot(), 0)).Text == "Worker", "AST text remains owned by its document snapshot");
    }

    void TestSemanticNegatives()
    {
        Check(Has(*Analyze("class C\n{\n    fn F()\n    {\n        Float bad = 3\n    }\n}\n"), "JBC3003"), "Int to Float variables require explicit conversion");
        Check(Has(*Analyze("script S\n{\n    Transform2D bare\n}\n"), "JBC3009"), "handles require nullable syntax");
        Check(Has(*Analyze("script S\n{\n    ref Transform2D t\n    fn F()\n    {\n        t!.valid = true\n    }\n}\n"), "JBC3006"), "readonly API fields reject assignment");
        Check(Has(*Analyze("class C\n{\n    fn F() -> Int\n    {\n    }\n}\n"), "JBC3011"), "nonvoid functions require a return on every path");
        Check(Has(*Analyze("script S\n{\n    fn Wrong() callback\n    {\n    }\n}\n"), "JBC3012"), "callback signatures must match engine hooks");
        Check(Has(*Analyze("class C\n{\n    fn F() override\n    {\n    }\n}\n"), "JBC3013"), "override requires a matching base function");
        Check(Has(*Analyze("class A : B\n{\n}\nclass B : A\n{\n}\n"), "JBC3014"), "inheritance cycles are rejected");
        Check(Has(*Analyze("class C\n{\n    fn F()\n    {\n        Int x\n        Int y = x\n    }\n}\n"), "JBC3017"), "uninitialized local reads are rejected");
        Check(Has(*Analyze("class C\n{\n    fn Borrow(ref Int x)\n    {\n    }\n    fn F()\n    {\n        Int x = 1\n        Borrow(x)\n    }\n}\n"), "JBC3010"), "borrowed parameters require an explicit ref argument");
    }

    void TestLexicalScopesAndRecovery()
    {
        auto result = Analyze("class C\n{\n    fn F()\n    {\n        Int x = 1\n        if (true)\n        {\n            String x = \"nested\"\n        }\n        Int y = x\n    }\n}\n");
        Check(result->Documents[0]->Diagnostics.IsEmpty(), "inner block shadows a local without a duplicate error");
        Array<const Symbol*> locals;
        for (const Symbol& symbol : result->Symbols)
        {
            if (SymbolKind::Local == symbol.Kind && symbol.Name == "x")
            {
                locals.Add(&symbol);
            }
        }
        Check(2 == locals.Size() && locals[0]->ScopeRange.End.Offset > locals[1]->ScopeRange.End.Offset, "locals carry lexical block lifetimes");
        auto malformed = Analyze("class C\n{\n    Int broken =\n    Int good = 1\n}\n");
        Check(0 != malformed->Documents[0]->ParseDiagnostics.GetCount(), "broken code reports a parse error");
        Check(nullptr != malformed->Symbols.FindBy([](const Symbol& symbol) { return symbol.Name == "good"; }), "valid declarations after malformed code stay available");
    }

    void TestIncrementalDependenciesAndOwnership()
    {
        Array<DocumentInput> documents;
        documents.Add({"file:///one.jscript", "one.jscript", "class One\n{\npublic fn First() -> Int\n{\nreturn 1\n}\n}\n", 1});
        documents.Add({"file:///two.jscript", "two.jscript", "class Two\n{\nfn Second() -> Int\n{\nInt value = 2\nreturn value\n}\n}\n", 1});
        auto result = AnalyzeProject(documents.View(), Catalog());
        const DocumentAnalysis* stable = result->Documents[1].Get();
        const char* stableText = stable->Source.GetText().data();
        documents[0].Text.ReplaceAll("return 1", "return 3");
        documents[0].Version = 2;
        result = AnalyzeProjectIncremental(documents.View(), Catalog(), std::move(result));
        Check(1 == result->ParsedDocuments && 1 == result->CheckedDocuments, "a body edit does not parse or check an unrelated file");
        Check(result->Documents[1].Get() == stable && result->Documents[1]->Source.GetText().data() == stableText,
            "incremental reuse preserves SourceText and AST owner addresses");
        const Symbol* local = result->Symbols.FindBy([](const Symbol& symbol) { return symbol.Name == "value"; });
        Check(nullptr != local && result->Documents[1]->Occurrences.Last().Target == local->Id,
            "reused local occurrences are remapped to current symbol IDs");
        documents[1].Text = "class Two\n{\nOne first\nfn Second() -> Int\n{\nreturn first.First()\n}\n}\n";
        documents[1].Version = 2;
        result = AnalyzeProjectIncremental(documents.View(), Catalog(), std::move(result));
        Check(result->Documents[1]->Diagnostics.IsEmpty(), "new dependent body resolves the existing exported type");
        documents[0].Text.ReplaceAll("-> Int", "-> String").ReplaceAll("return 3", "return \"changed\"");
        documents[0].Version = 3;
        result = AnalyzeProjectIncremental(documents.View(), Catalog(), std::move(result));
        Check(1 == result->ParsedDocuments && 2 == result->CheckedDocuments, "export changes recheck dependent bodies without reparsing them");
        Check(Has(*result, "JBC3003"), "export type changes update dependent diagnostics");
        documents.RemoveAt(0);
        result = AnalyzeProjectIncremental(documents.View(), Catalog(), std::move(result));
        Check(0 == result->ParsedDocuments && 1 == result->CheckedDocuments && Has(*result, "JBC3001"),
            "deleting an exported type rechecks references to the removed file");
        documents.Add({"file:///one.jscript", "one.jscript", "class One\n{\npublic fn First() -> Int\n{\nreturn 4\n}\n}\n", 4});
        result = AnalyzeProjectIncremental(documents.View(), Catalog(), std::move(result));
        Check(2 == result->CheckedDocuments && result->Documents[0]->Diagnostics.IsEmpty(),
            "adding a previously unknown type clears old dependent errors");

        Array<DocumentInput> callbacks;
        callbacks.Add({"file:///a.jscript", "a.jscript", "script A\n{\nfn OnUpdate() callback\n{\nInt x = 1\n}\n}\n", 1});
        callbacks.Add({"file:///b.jscript", "b.jscript", "script B\n{\nfn OnUpdate() callback\n{\nInt y = 2\n}\n}\n", 1});
        auto callbackResult = AnalyzeProject(callbacks.View(), Catalog());
        callbacks[0].Text.ReplaceAll("x = 1", "x = 3");
        callbacks[0].Version = 2;
        callbackResult = AnalyzeProjectIncremental(callbacks.View(), Catalog(), std::move(callbackResult));
        Check(1 == callbackResult->ParsedDocuments && 1 == callbackResult->CheckedDocuments,
            "a callback body edit does not invalidate unrelated scripts with the same hook name");
        auto full = AnalyzeProject(callbacks.View(), Catalog());
        Check(callbackResult->Symbols.Size() == full->Symbols.Size(), "full and incremental snapshots have the same symbol count");
        for (std::size_t index = 0; index < full->Documents.Size(); ++index)
        {
            const DocumentAnalysis& expected = *full->Documents[index];
            const DocumentAnalysis& actual = *callbackResult->Documents[index];
            Check(expected.Diagnostics.Size() == actual.Diagnostics.Size() && expected.Occurrences.Size() == actual.Occurrences.Size(),
                "full and incremental callback results have the same diagnostics and occurrence counts");
            for (const Occurrence& occurrence : actual.Occurrences)
            {
                const Symbol& target = callbackResult->Symbols[occurrence.Target];
                const Occurrence* counterpart = expected.Occurrences.FindBy([&](const Occurrence& other)
                {
                    const Symbol& otherTarget = full->Symbols[other.Target];
                    return occurrence.Range.Begin.Offset == other.Range.Begin.Offset && target.Name == otherTarget.Name
                        && target.Kind == otherTarget.Kind && target.Type == otherTarget.Type;
                });
                Check(nullptr != counterpart, "incremental occurrences resolve to the same logical symbols as a full analysis");
            }
        }
    }

    void TestBorrowingContainersAndAbstractTypes()
    {
        auto readonlyBorrow = Analyze("class C\n{\nfn Read(ref const Int value) -> Int\n{\nreturn value\n}\nfn F() -> Int\n{\nconst Int x = 1\nreturn Read(ref x)\n}\n}\n");
        for (const SemanticDiagnostic& diagnostic : readonlyBorrow->Documents[0]->Diagnostics)
        {
            std::cerr << "readonly borrow diagnostic: " << diagnostic.Code << '\n';
        }
        Check(readonlyBorrow->Documents[0]->Diagnostics.IsEmpty(), "readonly borrowed parameters accept initialized const values");
        auto nullableBorrow = Analyze("class C\n{\nfn Write(ref Int value)\n{\nvalue = 1\n}\nfn F()\n{\nref Int maybe = 1\nif (maybe is not null)\n{\nWrite(ref maybe)\n}\n}\n}\n");
        Check(nullableBorrow->Documents[0]->Diagnostics.IsEmpty(), "a borrowed optional value keeps its declared ref type");
        auto mutation = Analyze("class C\n{\nArray<Int> values\nfn F()\n{\nfor (value in values)\n{\nvalues.Add(value)\n}\n}\n}\n");
        Check(Has(*mutation, "JBC3019"), "growing an iterated container is rejected");
        auto borrowedMutation = Analyze("class C\n{\nArray<Int> values\nfn F()\n{\nfor (ref value in values)\n{\nvalues.Add(value)\n}\n}\n}\n");
        Check(Has(*borrowedMutation, "JBC3019"),
            "a borrowed element cannot survive growth of its iterated container");
        auto defaults = Analyze("class C\n{\nfn F()\n{\nArray<Int> values\nvalues.Add(1)\n}\n}\n");
        Check(defaults->Documents[0]->Diagnostics.IsEmpty(), "a default constructed local container is initialized");
        auto invariant = Analyze("class C\n{\nfn F()\n{\nArray<Int> numbers\nArray<String> words\nnumbers = words\n}\n}\n");
        Check(Has(*invariant, "JBC3003"), "container element types are invariant during assignment");
        auto invalidAbstract = Analyze("class A\n{\nfn F() require\n}\nclass B\n{\nA stored\n}\n");
        Check(Has(*invalidAbstract, "JBC3016"), "abstract types cannot be stored in fields");
        auto nullableAbstract = Analyze("class A\n{\nfn Read() -> Int require\n}\n"
            "class C\n{\nref A stored\nfn F()\n{\nref A local = null\n}\nfn Make() -> ref A\n{\nreturn null\n}\n}\n");
        std::size_t abstractDiagnostics = 0;
        for (const SemanticDiagnostic& diagnostic : nullableAbstract->Documents[0]->Diagnostics)
        {
            abstractDiagnostics += diagnostic.Code == "JBC3016" ? 1u : 0u;
        }
        Check(abstractDiagnostics == 3, "abstract fields, locals, and returns cannot own nullable C++ values");
        auto nullableInterface = Analyze("interface I\n{\nfn Read() -> Int\n}\nclass C\n{\nref I stored\n}\n");
        Check(Has(*nullableInterface, "JBC3020"), "an interface cannot be stored as an owned nullable value");
        auto abstractParameter = Analyze("class A\n{\nfn Read() -> Int require\n}\n"
            "class C\n{\nfn Pass(A value)\n{\n}\n}\n");
        Check(Has(*abstractParameter, "JBC3016"), "an abstract class cannot be passed by value");
        auto interfaceParameter = Analyze("interface I\n{\nfn Read() -> Int\n}\n"
            "class C\n{\nfn Pass(I value)\n{\n}\n}\n");
        Check(Has(*interfaceParameter, "JBC3020"), "an interface cannot be passed by value");
        auto borrowedInterface = Analyze("interface I\n{\nfn Read() -> Int\n}\n"
            "class C\n{\nfn Borrow(ref I value) -> Int\n{\nreturn value.Read()\n}\n}\n");
        Check(borrowedInterface->Documents[0]->Diagnostics.IsEmpty(),
            "a borrowed interface parameter remains a valid non-owning reference");
        auto incompatibleBorrow = Analyze("interface I\n{\nfn Read() -> Int\n}\nclass Other\n{\n}\n"
            "class C\n{\nfn Borrow(ref I value)\n{\n}\nfn F()\n{\nOther other\nBorrow(ref other)\n}\n}\n");
        Check(Has(*incompatibleBorrow, "JBC3034"), "an unrelated value cannot satisfy an interface borrow");
        auto borrowedAbstract = Analyze("class A\n{\npublic fn Read() -> Int require\n}\n"
            "class C\n{\nfn Borrow(ref A value) -> Int\n{\nreturn value.Read()\n}\n}\n");
        Check(borrowedAbstract->Documents[0]->Diagnostics.IsEmpty(),
            "a borrowed abstract class parameter remains a valid non-owning reference");
        auto invalidGeneric = Analyze("script S\n{\nref GameObject owner\nfn F()\n{\nowner!.GetComponent<String>()\n}\n}\n");
        Check(Has(*invalidGeneric, "JBC3039"), "engine generic component APIs reject value type arguments");
        auto validGeneric = Analyze("script S\n{\nref GameObject owner\nfn F()\n{\nref Transform2D t = owner!.GetComponent<Transform2D>()\n}\n}\n");
        Check(validGeneric->Documents[0]->Diagnostics.IsEmpty(), "engine generic return types substitute a valid component descriptor");
    }

    void TestInheritanceAccessAndSwitchChecks()
    {
        Check(Has(*Analyze("class A\n{\n}\nclass B : A\n{\n}\nclass C : A\n{\n}\nclass D : B, C\n{\n}\n"), "JBC3015"),
            "noninterface diamonds are rejected");
        auto interfaces = Analyze("interface I\n{\n}\ninterface A : I\n{\n}\ninterface B : I\n{\n}\nclass C : A, B\n{\n}\n");
        Check(false == Has(*interfaces, "JBC3015"), "interface diamonds remain valid");
        Check(Has(*Analyze("class A\n{\nInt hidden = 1\n}\nclass B\n{\nA a\nfn F() -> Int\n{\nreturn a.hidden\n}\n}\n"), "JBC3005"),
            "private members are inaccessible through another instance");
        Check(Has(*Analyze("enum State\n{\nIdle\nDead\n}\nclass C\n{\nState state = State.Idle\nfn F()\n{\nswitch (state)\n{\ncase State.Idle\n{\n}\n}\n}\n}\n"), "JBC3018"),
            "enum switches require every value or default");
        Check(Has(*Analyze("class C\n{\nfn F()\n{\nBool value = true\nvalue += false\n}\n}\n"), "JBC3031"), "compound assignment validates operators");
        auto staticInstance = Analyze("class C\n{\nInt value = 1\npublic fn Read() -> Int\n{\nreturn value\n}\nstatic fn F(C other) -> Int\n{\nreturn other.Read()\n}\n}\n");
        Check(staticInstance->Documents[0]->Diagnostics.IsEmpty(), "a static method can access an explicitly supplied instance");
        Check(Has(*Analyze("class C\n{\nInt value = 1\nstatic fn F() -> Int\n{\nreturn value\n}\n}\n"), "JBC3028"),
            "a static method cannot implicitly access an instance field");
    }

    void TestCompletionReceiverTypesAndRenameSafety()
    {
        const char* source = "struct Data\n{\nInt count = 0\n}\nclass C\n{\nArray<Data> items\nData data\nfn Make() -> Data\n{\nreturn data\n}\nfn F()\n{\nitems[0].\nMake().\n}\n}\n";
        auto result = Analyze(source);
        const String text(source);
        for (const char* receiver : {"items[0].", "Make()."})
        {
            const UInt32 offset = static_cast<UInt32>(text.find(receiver) + std::char_traits<char>::length(receiver));
            const Array<SymbolId> completion = GetCompletionSymbols(*result, "file:///test.jscript", offset);
            Check(nullptr != completion.FindBy([&](SymbolId id) { return result->Symbols[id].Name == "count"; }),
                "incomplete member completion infers indexed and called receivers");
        }
        auto collision = Analyze("class C\n{\nInt value = 1\nfn F() -> Int\n{\nInt other = 2\nreturn value\n}\n}\n");
        const Symbol* value = collision->Symbols.FindBy([](const Symbol& symbol) { return symbol.Name == "value"; });
        String error;
        Check(false == CanRenameSymbol(*collision, value->Id, "other", error), "rename rejects capture by a local at a use site");
        Check(CanRenameSymbol(*collision, value->Id, "renamed", error), "rename permits an unambiguous new name");

        AnalysisSession session;
        Array<DocumentInput> documents;
        documents.Add({"file:///session.jscript", "session.jscript", "script S\n{\nref Transform2D t\nfn F()\n{\nt!.valid = true\n}\n}\n", 1});
        ApiCatalog api = Catalog();
        Check(Has(session.Update(documents.View(), api), "JBC3006"), "session analyzes the initial API readonly metadata");
        const DocumentAnalysis* oldDocument = session.GetResult()->Documents[0].Get();
        Check(0 == session.Update(documents.View(), api).ParsedDocuments && session.GetResult()->Documents[0].Get() == oldDocument,
            "session owns and reuses unchanged source snapshots");
        api.Types[0].Members[1].Writable = true;
        Check(false == Has(session.Update(documents.View(), api), "JBC3006"), "API metadata updates invalidate cached diagnostics");
    }
}

namespace
{
    void TestRenameContractGroups()
    {
        const auto valid = [](const AnalysisResult& result)
        {
            for (const auto& document : result.Documents)
            {
                Check(false == document->ParseDiagnostics.HasErrors() && document->Diagnostics.IsEmpty(),
                    "rename contract fixtures must be valid before any rename");
            }
        };
        const auto method = [](const AnalysisResult& result, std::string_view owner, std::string_view name, std::size_t count = 0) -> SymbolId
        {
            const Symbol* found = result.Symbols.FindBy([&](const Symbol& symbol)
            {
                return SymbolKind::Method == symbol.Kind && symbol.Name == name && symbol.Parameters.Size() == count
                    && InvalidSymbol != symbol.Parent && result.Symbols[symbol.Parent].Name == owner;
            });
            Check(nullptr != found, "rename regression resolves its declared method");
            return found->Id;
        };
        auto inherited = Analyze("class Base\n{\npublic fn Run()\n{\n}\npublic fn Run(Int n)\n{\n}\n}\n"
            "class Child : Base\n{\npublic fn Run() override\n{\n}\npublic fn Execute()\n{\n}\n}\n"
            "class Grand : Child\n{\npublic fn Run() override\n{\n}\n}\nclass Hidden : Base\n{\npublic fn Run()\n{\n}\n}\n");
        valid(*inherited);
        const SymbolId base = method(*inherited, "Base", "Run");
        const SymbolId child = method(*inherited, "Child", "Run");
        const SymbolId grand = method(*inherited, "Grand", "Run");
        for (SymbolId selected : {base, child, grand})
        {
            const Array<SymbolId> group = GetRenameSymbols(*inherited, selected);
            Check(group.Size() == 3 && group.Contains(base) && group.Contains(child) && group.Contains(grand),
                "override rename groups are transitive and independent of selection direction");
            Check(false == group.Contains(method(*inherited, "Hidden", "Run"))
                && false == group.Contains(method(*inherited, "Base", "Run", 1)),
                "method hiding and unrelated overloads stay outside the rename contract");
            String error;
            Check(false == CanRenameSymbol(*inherited, selected, "Execute", error),
                "rename collision checking includes every override owner");
            Check(CanRenameSymbol(*inherited, selected, "Perform", error), "whole override contract can receive an unambiguous name");
        }
        auto implemented = Analyze("interface I\n{\nfn Act()\n}\nclass Impl : I\n{\npublic fn Act()\n{\n}\n}\n"
            "class More : Impl\n{\npublic fn Act() override\n{\n}\n}\nclass Hidden : Impl\n{\npublic fn Act()\n{\n}\n}\n");
        valid(*implemented);
        const SymbolId requirement = method(*implemented, "I", "Act");
        const SymbolId implementation = method(*implemented, "Impl", "Act");
        const SymbolId additional = method(*implemented, "More", "Act");
        const Array<SymbolId> interfaces = GetRenameSymbols(*implemented, additional);
        Check(interfaces.Size() == 3 && interfaces.Contains(requirement) && interfaces.Contains(implementation),
            "interface requirement, implementation and further overrides rename together");
        auto required = Analyze("class Parent\n{\npublic fn Work() require\n}\nclass Concrete : Parent\n{\npublic fn Work()\n{\n}\n}\n");
        valid(*required);
        Check(GetRenameSymbols(*required, method(*required, "Concrete", "Work")).Size() == 2,
            "a require declaration and its implementation share the rename contract");
        auto callback = Analyze("script S\n{\nfn OnUpdate() callback\n{\n}\n}\n");
        valid(*callback);
        String error;
        Check(false == CanRenameSymbol(*callback, method(*callback, "S", "OnUpdate"), "Update", error),
            "callback names remain fixed by the engine API contract");
    }
}

JBro::Int32 RunScriptCompilerAnalysisTests()
{
    TestSingleRefSyntax();
    TestCppEmitterChecksEveryRefLink();
    TestCppEmitterCombinesProjectFiles();
    TestCppEmitterRejectsRecursiveOwnedRefs();
    TestCppEmitterClassConstructionAndStaticMembers();
    TestCppEmitterPreservesFieldVisibilityAndReflection();
    TestStaticPropertiesAreSharedAndNotSerialized();
    TestSwitchRejectsNonComparableValues();
    TestEqualityOperatorsNeedAnExplicitOverload();
    TestCppEmitterFloatLiteralMatchesEngineValueType();
    TestWitnessRegressions();
    TestJumpControlFlowAndContainerEffects();
    TestNullableGrammar();
    TestCrossFileSymbolsAndOverloads();
    TestSemanticNegatives();
    TestLexicalScopesAndRecovery();
    TestIncrementalDependenciesAndOwnership();
    TestBorrowingContainersAndAbstractTypes();
    TestInheritanceAccessAndSwitchChecks();
    TestCompletionReceiverTypesAndRenameSafety();
    TestRenameContractGroups();
    std::cout << "Script compiler analysis tests passed.\n";
    return 0;
}
