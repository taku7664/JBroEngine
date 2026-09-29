#include <JBro/Editor/ScriptProject.h>

#include <JBro/Canvas/ComponentRegistry.h>
#include <JBro/Editor/EditorPaths.h>
#include <JBro/Platform/Platform.h>
#include <JBro/Types/Uuid.h>

#include <cstdio>
#include <cstring>

namespace JBro::ScriptProject
{
    namespace
    {
        constexpr const char* ProjectFileName = "GameScript.vcxproj";
        constexpr const char* SolutionFileName = "GameScript.sln";
        constexpr const char* EnginePropsFileName = "JBroEngine.props";
        constexpr const char* ScriptsFolderName = "Scripts";
        constexpr const char* EngineMarkerFileName = "JBro.GameScript.props";

        // C++ 예약어와, 스크립트 기반 타입·매크로가 이미 쓰는 이름이다. 스크립트 이름과 필드 이름에 쓰면 컴파일이 깨지거나
        // 기반의 함수를 가린다(기존 엔진 `IsReservedScriptName` 을 넓혔다).
        constexpr const char* ReservedNames[] =
        {
            "alignas", "alignof", "and", "asm", "auto", "bool", "break", "case", "catch", "char", "class", "const",
            "consteval", "constexpr", "constinit", "const_cast", "continue", "co_await", "co_return", "co_yield",
            "decltype", "default", "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit", "export",
            "extern", "false", "float", "for", "friend", "goto", "if", "inline", "int", "long", "mutable", "namespace",
            "new", "noexcept", "not", "nullptr", "operator", "or", "private", "protected", "public", "register",
            "reinterpret_cast", "requires", "return", "short", "signed", "sizeof", "static", "static_assert",
            "static_cast", "struct", "switch", "template", "this", "thread_local", "throw", "true", "try", "typedef",
            "typeid", "typename", "union", "unsigned", "using", "virtual", "void", "volatile", "wchar_t", "while",
            "OnCreate", "OnStart", "OnUpdate", "OnFixedUpdate", "OnDestroy", "OnAttached", "OnDetached",
            "OnEnabled", "OnDisabled", "GetOwner", "GetTypeId", "StaticTypeName", "IsEnabled", "SetEnabled",
            "JBroSelf", "JBroFieldBase", "JBroFieldAt", "JBro", "GameScript2D", "GameScriptBase", "ScriptModule",
        };

        bool IsIdentifier(const char* text)
        {
            if (text == nullptr || text[0] == '\0')
            {
                return false;
            }
            const auto isLetter = [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
            };
            if (false == isLetter(text[0]))
            {
                return false;
            }
            for (const char* cursor = text + 1; *cursor != '\0'; ++cursor)
            {
                if (false == isLetter(*cursor) && (*cursor < '0' || *cursor > '9'))
                {
                    return false;
                }
            }
            return true;
        }

        bool IsReserved(const char* text)
        {
            for (const char* reserved : ReservedNames)
            {
                if (std::strcmp(reserved, text) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        // 한 줄씩 CRLF 로 붙인다.
        void Line(String& out, const char* text = "")
        {
            out.append(text);
            out.append("\r\n");
        }

        void LineF(String& out, const char* format, const char* a, const char* b = "", const char* c = "")
        {
            char buffer[512] = {};
            std::snprintf(buffer, sizeof(buffer), format, a, b, c);
            Line(out, buffer);
        }

        // Visual Studio 가 쓰는 모양(`{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}`, 대문자)이다.
        String FormatGuid(const Uuid& id)
        {
            char hex[Uuid::TextCapacity] = {};
            id.ToText(hex, sizeof(hex));
            char formatted[40] = {};
            std::snprintf(formatted, sizeof(formatted), "{%.8s-%.4s-%.4s-%.4s-%.12s}",
                hex, hex + 8, hex + 12, hex + 16, hex + 20);
            for (char& c : formatted)
            {
                if (c >= 'a' && c <= 'f')
                {
                    c = static_cast<char>(c - 'a' + 'A');
                }
            }
            return String(formatted);
        }

        const char* DefaultValue(FieldType type)
        {
            switch (type)
            {
            case FieldType::Bool:
                return " = false";
            case FieldType::Int:
                return " = 0";
            case FieldType::Float:
                return " = 0.0f";
            default:
                return " = {}";
            }
        }

        bool WriteText(IPlatform& platform, const String& path, const String& text)
        {
            return platform.WriteWholeFile(path.c_str(),
                {reinterpret_cast<const std::byte*>(text.c_str()), static_cast<std::uint32_t>(text.size())});
        }

        // 없을 때만 쓴다. 이미 있으면 사용자가 고쳤을 수 있다 - 건드리지 않는다.
        bool WriteIfMissing(IPlatform& platform, const String& path, const String& text, String& error)
        {
            if (platform.FileExists(path.c_str()))
            {
                return true;
            }
            if (false == WriteText(platform, path, text))
            {
                error = "could not write ";
                error.append(path);
                return false;
            }
            return true;
        }
    }

    const char* FieldCppType(FieldType type)
    {
        switch (type)
        {
        case FieldType::Bool:
            return "bool";
        case FieldType::Int:
            return "int";
        case FieldType::Float:
            return "float";
        case FieldType::String:
            return "String";
        case FieldType::Vector2:
            return "Vector2";
        case FieldType::Vector3:
            return "Vector3";
        case FieldType::Color:
            return "Color";
        case FieldType::GameObject:
            return "GameObject";
        case FieldType::Asset:
            return "AssetHandle";
        default:
            return "float";
        }
    }

    NameProblem CheckScriptName(IPlatform& platform, const char* folder, const char* name)
    {
        if (name == nullptr || name[0] == '\0')
        {
            return NameProblem::Empty;
        }
        if (false == IsIdentifier(name))
        {
            return NameProblem::NotIdentifier;
        }
        if (IsReserved(name))
        {
            return NameProblem::Reserved;
        }
        ComponentTypeInfo existing;
        if (ComponentRegistry::Get().FindAttachable(MakeNameId(name), existing))
        {
            return NameProblem::TakenByType;
        }
        const String stem = EditorPaths::JoinPath(folder, name);
        if (platform.FileExists((stem + ".h").c_str()) || platform.FileExists((stem + ".cpp").c_str()))
        {
            return NameProblem::TakenByFile;
        }
        return NameProblem::None;
    }

    NameProblem CheckFieldName(const Array<FieldSpec>& fields, std::size_t index)
    {
        const char* name = fields[index].name.c_str();
        if (name[0] == '\0')
        {
            return NameProblem::Empty;
        }
        if (false == IsIdentifier(name))
        {
            return NameProblem::NotIdentifier;
        }
        if (IsReserved(name))
        {
            return NameProblem::Reserved;
        }
        for (std::size_t other = 0; other < fields.Size(); ++other)
        {
            if (other != index && fields[other].name == fields[index].name)
            {
                return NameProblem::Duplicate;
            }
        }
        return NameProblem::None;
    }

    String MakeScriptHeader(const char* className, const Array<FieldSpec>& fields, FrameworkKind framework)
    {
        String out;
        Line(out, "#pragma once");
        Line(out);
        Line(out, "#include <JBro/ScriptAPI.h>");
        // 2D 프렐류드는 3D 수학 타입의 리플렉션 설명자를 들이지 않는다. `Vector3` 필드가 있으면 그것만 더 들인다(MSBuild 로 확인했다).
        bool needsMath3D = false;
        for (const FieldSpec& field : fields)
        {
            needsMath3D = needsMath3D || field.type == FieldType::Vector3;
        }
        if (needsMath3D)
        {
            Line(out, "#include <JBro/Reflection/Math3DReflection.h>");
        }
        Line(out);
        LineF(out, "class %s final : public %s", className,
            framework == FrameworkKind::Framework3D ? "GameScriptBase" : "GameScript2D");
        Line(out, "{");
        LineF(out, "    JBRO_SCRIPT_BODY(%s)", className);
        Line(out, "public:");
        for (const FieldSpec& field : fields)
        {
            LineF(out, "    JBRO_FIELD(%s, %s)%s;", FieldCppType(field.type), field.name.c_str(), DefaultValue(field.type));
        }
        if (false == fields.IsEmpty())
        {
            Line(out);
        }
        Line(out, "protected:");
        Line(out, "    void OnCreate() override;");
        Line(out, "    void OnStart() override;");
        Line(out, "    void OnUpdate() override;");
        Line(out, "    void OnFixedUpdate() override;");
        Line(out, "    void OnDestroy() override;");
        Line(out, "};");
        return out;
    }

    String MakeScriptSource(const char* className, FrameworkKind framework)
    {
        String out;
        LineF(out, "#include \"%s.h\"", className);
        Line(out);
        LineF(out, "JBRO_REGISTER_SCRIPT_%s(%s);", framework == FrameworkKind::Framework3D ? "3D" : "2D", className);
        const char* const hooks[] = {"OnCreate", "OnStart", "OnUpdate", "OnFixedUpdate", "OnDestroy"};
        for (const char* hook : hooks)
        {
            Line(out);
            LineF(out, "void %s::%s()", className, hook);
            Line(out, "{");
            Line(out, "}");
        }
        return out;
    }

    String MakeModuleSource(FrameworkKind framework)
    {
        const char* dimension = framework == FrameworkKind::Framework3D ? "3D" : "2D";
        String out;
        Line(out, "// This file is the script module's entry point. Keep it in the project; the engine does the rest.");
        LineF(out, "#include <JBro/Framework%s/Scripting/ScriptModule.h>", dimension);
        Line(out);
        LineF(out, "JBRO_SCRIPT_MODULE_%s()", dimension);
        return out;
    }

    String MakeProjectFile(FrameworkKind framework, const Uuid& projectGuid)
    {
        const String guid = FormatGuid(projectGuid);
        String out;
        Line(out, "<?xml version=\"1.0\" encoding=\"utf-8\"?>");
        Line(out, "<Project DefaultTargets=\"Build\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">");
        Line(out, "  <ItemGroup Label=\"ProjectConfigurations\">");
        const char* const configurations[] = {"Debug", "Release"};
        for (const char* configuration : configurations)
        {
            LineF(out, "    <ProjectConfiguration Include=\"%s|x64\">", configuration);
            LineF(out, "      <Configuration>%s</Configuration>", configuration);
            Line(out, "      <Platform>x64</Platform>");
            Line(out, "    </ProjectConfiguration>");
        }
        Line(out, "  </ItemGroup>");
        Line(out, "  <PropertyGroup Label=\"Globals\">");
        Line(out, "    <VCProjectVersion>18.0</VCProjectVersion>");
        LineF(out, "    <ProjectGuid>%s</ProjectGuid>", guid.c_str());
        Line(out, "    <RootNamespace>GameScript</RootNamespace>");
        Line(out, "    <ProjectName>GameScript</ProjectName>");
        LineF(out, "    <JBroScriptDimension>%s</JBroScriptDimension>", framework == FrameworkKind::Framework3D ? "3D" : "2D");
        Line(out, "  </PropertyGroup>");
        Line(out, "  <!-- The engine location: JBRO_ENGINE_ROOT first (build servers), then JBroEngine.props, which the JBro editor rewrites whenever it opens this project. -->");
        Line(out, "  <PropertyGroup>");
        Line(out, "    <JBroEngineRoot Condition=\"'$(JBRO_ENGINE_ROOT)' != ''\">$([MSBuild]::EnsureTrailingSlash('$(JBRO_ENGINE_ROOT)'))</JBroEngineRoot>");
        Line(out, "  </PropertyGroup>");
        Line(out, "  <Import Project=\"$(MSBuildThisFileDirectory)JBroEngine.props\" Condition=\"'$(JBroEngineRoot)' == '' and Exists('$(MSBuildThisFileDirectory)JBroEngine.props')\" />");
        Line(out, "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.Default.props\" />");
        Line(out, "  <Import Project=\"$(JBroEngineRoot)JBro.GameScript.props\" Condition=\"Exists('$(JBroEngineRoot)JBro.GameScript.props')\" />");
        Line(out, "  <PropertyGroup Label=\"Configuration\">");
        Line(out, "    <ConfigurationType>DynamicLibrary</ConfigurationType>");
        Line(out, "  </PropertyGroup>");
        Line(out, "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.props\" />");
        Line(out, "  <!-- Every script under Scripts\\ is built. Adding a script never rewrites this file. -->");
        Line(out, "  <ItemGroup>");
        Line(out, "    <ClInclude Include=\"Scripts\\**\\*.h\" />");
        Line(out, "    <ClCompile Include=\"Scripts\\**\\*.cpp\" />");
        Line(out, "  </ItemGroup>");
        Line(out, "  <Import Project=\"$(JBroEngineRoot)JBro.GameScript.targets\" Condition=\"Exists('$(JBroEngineRoot)JBro.GameScript.targets')\" />");
        Line(out, "  <Target Name=\"JBroGameScriptFindEngine\" BeforeTargets=\"PrepareForBuild\">");
        Line(out, "    <Error Condition=\"!Exists('$(JBroEngineRoot)JBro.GameScript.props')\"");
        Line(out, "           Text=\"Cannot find the JBro engine. Open this project in the JBro editor once (it writes JBroEngine.props), or set JBRO_ENGINE_ROOT.\" />");
        Line(out, "  </Target>");
        Line(out, "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\" />");
        Line(out, "</Project>");
        return out;
    }

    String MakeSolutionFile(const Uuid& projectGuid)
    {
        const String guid = FormatGuid(projectGuid);
        const char* id = guid.c_str();
        String out;
        Line(out);
        Line(out, "Microsoft Visual Studio Solution File, Format Version 12.00");
        Line(out, "# Visual Studio Version 17");
        Line(out, "VisualStudioVersion = 17.0.31903.59");
        Line(out, "MinimumVisualStudioVersion = 10.0.40219.1");
        LineF(out, "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"GameScript\", \"GameScript.vcxproj\", \"%s\"", id);
        Line(out, "EndProject");
        Line(out, "Global");
        Line(out, "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution");
        Line(out, "\t\tDebug|x64 = Debug|x64");
        Line(out, "\t\tRelease|x64 = Release|x64");
        Line(out, "\tEndGlobalSection");
        Line(out, "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution");
        LineF(out, "\t\t%s.Debug|x64.ActiveCfg = Debug|x64", id);
        LineF(out, "\t\t%s.Debug|x64.Build.0 = Debug|x64", id);
        LineF(out, "\t\t%s.Release|x64.ActiveCfg = Release|x64", id);
        LineF(out, "\t\t%s.Release|x64.Build.0 = Release|x64", id);
        Line(out, "\tEndGlobalSection");
        Line(out, "\tGlobalSection(SolutionProperties) = preSolution");
        Line(out, "\t\tHideSolutionNode = FALSE");
        Line(out, "\tEndGlobalSection");
        Line(out, "EndGlobal");
        return out;
    }

    String MakeEngineProps(const char* engineRoot)
    {
        String root(engineRoot != nullptr ? engineRoot : "");
        for (char& c : root)
        {
            if (c == '/')
            {
                c = '\\';
            }
        }
        if (false == root.empty() && root.back() != '\\')
        {
            root.push_back('\\');
        }
        String out;
        Line(out, "<?xml version=\"1.0\" encoding=\"utf-8\"?>");
        Line(out, "<!-- Written by the JBro editor each time it opens this project: where the engine is on this PC. Not for version control. -->");
        Line(out, "<Project xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">");
        Line(out, "  <PropertyGroup>");
        LineF(out, "    <JBroEngineRoot Condition=\"'$(JBroEngineRoot)' == ''\">%s</JBroEngineRoot>", root.c_str());
        Line(out, "  </PropertyGroup>");
        Line(out, "</Project>");
        return out;
    }

    String MakeGitIgnore()
    {
        String out;
        Line(out, "# Where the engine is on this PC. The JBro editor rewrites it when it opens the project.");
        Line(out, "JBroEngine.props");
        Line(out, ".vs/");
        Line(out, "*.vcxproj.user");
        return out;
    }

    String FindEngineRoot(IPlatform& platform, const char* startFolder)
    {
        String folder(startFolder != nullptr ? startFolder : "");
        for (int depth = 0; depth < 6 && false == folder.empty(); ++depth)
        {
            if (platform.FileExists(EditorPaths::JoinPath(folder.c_str(), EngineMarkerFileName).c_str()))
            {
                return folder;
            }
            folder = EditorPaths::FolderOf(folder.c_str());
        }
        return String();
    }

    bool HasProject(IPlatform& platform, const char* contentsFolder)
    {
        return platform.FileExists(EditorPaths::JoinPath(contentsFolder, ProjectFileName).c_str());
    }

    bool EnsureProject(IPlatform& platform, const char* contentsFolder, FrameworkKind framework, String& error)
    {
        const String scripts = EditorPaths::JoinPath(contentsFolder, ScriptsFolderName);
        platform.CreateDirectoryAt(contentsFolder);
        platform.CreateDirectoryAt(scripts.c_str());
        // 솔루션은 프로젝트의 번호를 가리킨다. **프로젝트 파일이 없을 때만 둘을 함께 쓴다** - 프로젝트만 남아 있는데 솔루션을
        // 새 번호로 쓰면 둘이 어긋난다. 사용자가 솔루션을 지웠다면 그대로 둔다.
        if (false == HasProject(platform, contentsFolder))
        {
            const Uuid projectGuid = Uuid::Generate();
            if (false == WriteIfMissing(platform, EditorPaths::JoinPath(contentsFolder, ProjectFileName), MakeProjectFile(framework, projectGuid), error)
                || false == WriteIfMissing(platform, EditorPaths::JoinPath(contentsFolder, SolutionFileName), MakeSolutionFile(projectGuid), error))
            {
                return false;
            }
        }
        return WriteIfMissing(platform, EditorPaths::JoinPath(scripts.c_str(), "ScriptModule.cpp"), MakeModuleSource(framework), error)
            && WriteIfMissing(platform, EditorPaths::JoinPath(contentsFolder, ".gitignore"), MakeGitIgnore(), error);
    }

    bool RefreshEngineProps(IPlatform& platform, const char* contentsFolder, const char* engineRoot)
    {
        const String path = EditorPaths::JoinPath(contentsFolder, EnginePropsFileName);
        const String wanted = MakeEngineProps(engineRoot);
        Array<std::byte> existing;
        if (platform.ReadWholeFile(path.c_str(), existing) && existing.Size() == wanted.size()
            && std::memcmp(existing.Data(), wanted.c_str(), wanted.size()) == 0)
        {
            return true;
        }
        return WriteText(platform, path, wanted);
    }

    bool CreateScript(IPlatform& platform, const char* folder, const char* className,
        const Array<FieldSpec>& fields, FrameworkKind framework, String& error)
    {
        if (CheckScriptName(platform, folder, className) != NameProblem::None)
        {
            error = "the script name cannot be used";
            return false;
        }
        for (std::size_t index = 0; index < fields.Size(); ++index)
        {
            if (CheckFieldName(fields, index) != NameProblem::None)
            {
                error = "a field name cannot be used";
                return false;
            }
        }
        platform.CreateDirectoryAt(folder);
        const String stem = EditorPaths::JoinPath(folder, className);
        if (false == WriteText(platform, stem + ".h", MakeScriptHeader(className, fields, framework)))
        {
            error = "could not write the header";
            return false;
        }
        if (false == WriteText(platform, stem + ".cpp", MakeScriptSource(className, framework)))
        {
            // 반쪽을 남기지 않는다. 헤더만 있으면 다음 시도가 "파일이 있다" 로 막힌다.
            platform.DeleteFileAt((stem + ".h").c_str());
            error = "could not write the source";
            return false;
        }
        return true;
    }
}
