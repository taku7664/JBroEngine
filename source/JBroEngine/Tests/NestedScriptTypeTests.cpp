#include <JBro/ScriptCompiler/Analysis.h>
#include <JBro/ScriptCompiler/CppEmitter.h>
#include <JBro/ScriptCompiler/Summary.h>
#include <JBro/Canvas/Canvas.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/ScriptProject.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Runtime/GameScriptBase.h>
#include <JBro/Types/NameTable.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>

namespace
{
    using namespace JBro;
    using namespace JBro::ScriptCompiler;
    namespace fs = std::filesystem;

    constexpr const char* Definitions = R"(class Outer
{
    private static Int secret = 7
    private Int outerValue = 3
    public struct Inner
    {
        Int local = 2
        fn Read(Outer owner) -> Int
        {
            return secret + owner.outerValue + local
        }
    }
    public interface Contract
    {
        fn Read() -> Int require
    }
    public class Impl : Contract
    {
        public fn Read() -> Int override
        {
            return 5
        }
    }
    public enum Mode
    {
        First
        Second
    }
    public class Sibling
    {
        public Inner data
        public fn Read() -> Int
        {
            return data.Read(Outer())
        }
    }
    public class Point
    {
        public Int value = 0
        public fn Point(Int number)
        {
            value = number
        }
    }
}
struct Other
{
    struct Inner
    {
        Int value = 9
    }
}
)";

    void Check(Bool condition, const char* message)
    {
        if (false == condition)
        {
            throw std::runtime_error(message);
        }
    }

    OwnerPtr<AnalysisResult> Analyze(std::string_view text)
    {
        const DocumentInput input{"file:///nested.jscript", "nested.jscript", String(text), 1};
        return AnalyzeProject({&input, 1}, {});
    }

    Bool HasError(const AnalysisResult& result, const char* code = nullptr)
    {
        for (const auto& document : result.Documents)
        {
            if (document->ParseDiagnostics.HasErrors())
            {
                return true;
            }
            for (const auto& diagnostic : document->Diagnostics)
            {
                if (false == diagnostic.Warning && (code == nullptr || diagnostic.Code == code))
                {
                    return true;
                }
            }
        }
        return false;
    }

    void TestNestedLanguageAndEmission()
    {
        const auto analysis = Analyze(String(Definitions) + "class Use\n{\nOuter.Inner first\nOther.Inner second\n"
            "fn Read() -> Int\n{\nOuter.Inner local\nOuter.Mode mode = Outer.Mode.First\nreturn local.Read(Outer()) + second.value\n}\n}\n");
        for (const auto& diagnostic : analysis->Documents.First()->Diagnostics)
        {
            if (false == diagnostic.Warning)
            {
                std::cout << "nested diagnostic: " << diagnostic.Code << " " << diagnostic.MessageKey;
                for (const String& argument : diagnostic.Arguments)
                {
                    std::cout << " " << argument;
                }
                std::cout << std::endl;
            }
        }
        Check(false == HasError(*analysis), "nested declarations, siblings, interfaces and qualified field/local types analyze");
        const Symbol* inner = nullptr;
        const Symbol* otherInner = nullptr;
        for (const Symbol& symbol : analysis->Symbols)
        {
            if (symbol.Type == "Outer.Inner" && symbol.Name == "Inner")
            {
                inner = &symbol;
            }
            if (symbol.Type == "Other.Inner" && symbol.Name == "Inner")
            {
                otherInner = &symbol;
            }
        }
        Check(inner != nullptr && otherInner != nullptr && inner->Id != otherInner->Id && inner->Parent != otherInner->Parent,
            "same-named nested types in different owners have distinct qualified identities");
        const ScriptSummary summary = SummarizeScript(*analysis, *analysis->Documents.First());
        Check(summary.Valid && summary.Types[0].Members[2].Kind == SummaryEntryKind::Type
            && summary.Types[0].Members[2].Members[0].Name == "local", "the summary recursively includes actual nested AST declarations");
        const auto& document = *analysis->Documents.First();
        const CppSource source{&document.Source, &document.Tree};
        const CppProjectResult project = EmitCppProject({&source, 1}, {}, false, analysis.Get());
        for (const String& error : project.Errors)
        {
            std::cout << "nested emission: " << error << std::endl;
        }
        Check(project.Errors.IsEmpty(), "nested types produce complete separate generated C++ artifacts");
        Bool qualifiedReflection = false;
        for (const CppArtifact& artifact : project.Files)
        {
            qualifiedReflection = qualifiedReflection || artifact.Code.Contains("\"Outer.Inner\"");
        }
        Check(qualifiedReflection, "reflection uses the source qualified type name");
        const auto spaced = Analyze("struct Outer { struct Inner { Int value } }\nclass Use\n{\n"
            "Outer . Inner field\nOuter // before dot\n . // after dot\n Inner commented\n"
            "fn Read()\n{\nOuter . Inner local\nOuter. // local comment\nInner another\n}\n}\n");
        Check(false == HasError(*spaced), "qualified types ignore whitespace and line-comment trivia around their dots");
        const auto& spacedDocument = *spaced->Documents.First();
        const CppSource spacedSource{&spacedDocument.Source, &spacedDocument.Tree};
        Check(EmitCppProject({&spacedSource, 1}, {}, false, spaced.Get()).Errors.IsEmpty(),
            "qualified type trivia preserves canonical identities through project emission");
        const auto containers = Analyze("script Carrier { public struct Data { Int value } }\n"
            "interface Catalog\n{\nstruct Node\n{\nclass Leaf { public Int value = 1 }\n}\n}\n"
            "class Use { Catalog.Node.Leaf leaf }\n");
        Check(false == HasError(*containers), "scripts and interfaces can own nested types through multiple lexical levels");
        const auto duplicate = Analyze("class Outer { public class Outer {} }\n");
        Check(HasError(*duplicate, "JBC3002"), "a nested type cannot reuse its enclosing class name");

        const auto inaccessible = Analyze("class Outer { private struct Hidden { Int value } }\nclass User { Outer.Hidden value }\n");
        Check(HasError(*inaccessible, "JBC3005"), "private nested types cannot be named outside their owner");
        const auto privateBase = Analyze("class Outer { private class Hidden { public class Base {} } }\n"
            "class Derived : Outer.Hidden.Base {}\n");
        Check(HasError(*privateBase, "JBC3005"), "qualified base types cannot bypass a private enclosing type");
        const auto reverseAccess = Analyze("class Outer\n{\npublic class Inner { private Int secret = 1 }\n"
            "fn Read(Inner value) -> Int { return value.secret }\n}\n");
        Check(HasError(*reverseAccess, "JBC3005"), "the outer class cannot access nested private members");
        const auto capture = Analyze("class Outer\n{\nprivate Int value = 1\npublic class Inner\n{\n"
            "public fn Read() -> Int { return value }\n}\n}\n");
        Check(HasError(*capture, "JBC3028"), "a nested method does not capture an outer instance");
        const auto mismatch = Analyze(String(Definitions) + "class User\n{\nfn Read()\n{\nOuter.Inner first\nOther.Inner second\nfirst = second\n}\n}\n");
        Check(HasError(*mismatch, "JBC3003"), "different outer owners do not make their Inner values interchangeable");
        const auto unknown = Analyze("class Outer { public struct Inner {} }\nclass User { Outer.Missing field }\n");
        Check(HasError(*unknown, "JBC3001"), "an unknown nested type is a language diagnostic");
        const auto nestedScript = Analyze("class Outer { script Inner {} }\n");
        Check(nestedScript->Documents.First()->ParseDiagnostics.HasErrors(), "engine-attached scripts remain top-level declarations");
    }

    String Utf8(const fs::path& path)
    {
        const auto bytes = path.generic_u8string();
        return String(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    void TestNestedGeneratedGameDll()
    {
        const fs::path root = fs::temp_directory_path() / ("JBroNestedTypes-"
            + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root / "Contents/Scripts");
        fs::create_directories(root / "Assets");
        std::ofstream(root / "Game.jproject", std::ios::binary) << "Version: 1\nEngineVersion: 0.1.0\nFramework: 2D\nRootPath: .\n"
            "AssetDirectory: Assets\nScriptSourceDirectory: Contents\nScriptOutputLibraryPath: x64/Debug/GameScript.dll\n";
        std::ofstream(root / "Contents/Scripts/Outer.jscript", std::ios::binary) << Definitions;
        std::ofstream(root / "Contents/Scripts/Probe.jscript", std::ios::binary) << R"(script NestedProbe
{
    [prop] public Int result = 0
    public fn OnUpdate() callback
    {
        Outer . Sibling sibling
        Outer . Impl implementation
        Other. // qualified type comment
            Inner other
        Outer.Point point = Outer.Point(4)
        result = sibling.Read() + implementation.Read() + other.value + point.value
    }
}
)";
        std::cout << "  NestedScriptType fixture: " << Utf8(root) << std::endl;
        EditorApplication editor;
        EditorApplicationConfig config;
        config.windowVisible = false;
        config.windowWidth = 640;
        config.windowHeight = 480;
        const String preferences = Utf8(root / "EditorPreferences.yaml");
        config.preferencesPath = preferences.c_str();
        Check(editor.Initialize(config), "nested-type compilation initializes a real editor");
        String failure;
        Check(ScriptProject::EnsureProject(editor.GetPlatform(), Utf8(root / "Contents").c_str(),
            FrameworkKind::Framework2D, failure), "nested-type compilation has a real game DLL project");
        ProjectFileError projectError;
        Check(editor.OpenProjectFile(Utf8(root / "Game.jproject").c_str(), projectError), "the nested-type project opens");
        Check(editor.BuildScripts(), "the real nested JBroScript DLL build starts");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
        while (editor.GetScriptBuildState() == EditorApplication::ScriptBuildState::Running
            && std::chrono::steady_clock::now() < deadline)
        {
            Check(editor.Tick(1.0f / 60.0f), "the editor ticks during nested-type C++ compilation");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        Check(editor.GetScriptBuildState() == EditorApplication::ScriptBuildState::Succeeded && editor.IsScriptModuleLoaded(),
            "cross-file nested classes, structs, enum, interface and private outer access compile into a loadable game DLL");
        Object::GameObject* object = editor.GetCanvas()->CreateObject("NestedProbe");
        GameScriptBase* script = editor.GetCanvas()->AttachScript(object, "NestedProbe");
        Check(script != nullptr && editor.StartSimulation() && editor.Tick(1.0f / 60.0f), "the real generated nested methods execute");
        Array<GameScriptBase*> scripts;
        editor.GetCanvas()->CollectScripts(scripts);
        const PropertyTable* table = PropertyRegistry::Lookup("NestedProbe");
        Bool found = false;
        for (UInt32 index = 0; table != nullptr && index < table->count; ++index)
        {
            const PropertyInfo& property = table->properties[index];
            if (std::strcmp(NameTable::Get().Resolve(property.name), "result") == 0 && scripts.Size() == 1)
            {
                found = *static_cast<const Int*>(property.ConstAddress(scripts[0])) == 30;
            }
        }
        Check(found, "nested sibling and interface methods return the expected value through the actual game DLL");
        editor.StopSimulation();
        editor.Shutdown();
    }
}

JBro::Int32 RunNestedScriptTypeTests()
{
    TestNestedLanguageAndEmission();
    TestNestedGeneratedGameDll();
    std::cout << "  NestedScriptType language, C++ compile and native game DLL tests passed\n";
    return 0;
}
