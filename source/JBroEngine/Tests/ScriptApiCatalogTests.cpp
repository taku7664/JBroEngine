#include <JBro/ScriptApiCatalog2D/Catalog.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Int.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace JBro;
    using namespace JBro::ScriptCompiler;
    void Check(Bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }
    unsigned char Hex(char value)
    {
        if (value >= '0' && value <= '9')
        {
            return static_cast<unsigned char>(value-'0');
        }
        if (value >= 'A' && value <= 'F')
        {
            return static_cast<unsigned char>(value-'A'+10);
        }
        if (value >= 'a' && value <= 'f')
        {
            return static_cast<unsigned char>(value-'a'+10);
        }
        throw std::runtime_error("invalid percent encoding in SDK URI");
    }
    String PathFromUri(const String& uri)
    {
        Check(uri.StartsWith("file:///"), "SDK definitions use absolute file URIs");
        String path;
        for (std::size_t index = 8; index < uri.size(); ++index)
        {
            if (uri[index] == '%')
            {
                Check(index+2 < uri.size(), "complete percent encoded byte");
                path.push_back(static_cast<char>((Hex(uri[index+1]) << 4) | Hex(uri[index+2])));
                index += 2;
            }
            else
            {
                path.push_back(uri[index]);
            }
        }
        return path;
    }
    void CheckDefinition(const String& uri, const SourceRange& range, const String& identifier)
    {
        const String path = PathFromUri(uri);
        std::ifstream file(std::filesystem::path(reinterpret_cast<const char8_t*>(path.c_str())), std::ios::binary);
        Check(file.is_open(), "SDK definition URI must resolve in Korean workspace path");
        const String source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const auto begin = static_cast<std::size_t>(range.Begin.Offset);
        const auto end = static_cast<std::size_t>(range.End.Offset);
        if (begin > end || end > source.size() || source.Substr(begin,end-begin) != identifier)
        {
            throw std::runtime_error("SDK definition token moved or is incorrect: " + identifier.Std() + " in " + path.Std());
        }
        UInt32 line = 1;
        std::size_t lineStart = 0;
        for (std::size_t index = 0; index < begin; ++index)
        {
            if (source[index] == '\n')
            {
                ++line;
                lineStart = index+1;
            }
        }
        Check(range.Begin.Line == line && range.Begin.Column == static_cast<UInt32>(begin-lineStart+1), "SDK byte, line and column coordinates agree");
    }
    String MaskCommentsAndStrings(String source)
    {
        enum class Mode { Code, Line, Block, String, Character };
        Mode mode = Mode::Code;
        for (std::size_t index = 0; index < source.size(); ++index)
        {
            const char character = source[index];
            const char next = index+1 < source.size() ? source[index+1] : '\0';
            if (mode == Mode::Code)
            {
                if (character == '/' && next == '/')
                {
                    mode = Mode::Line;
                }
                else if (character == '/' && next == '*')
                {
                    mode = Mode::Block;
                }
                else if (character == '"')
                {
                    mode = Mode::String;
                }
                else if (character == '\'')
                {
                    mode = Mode::Character;
                }
                else
                {
                    continue;
                }
                source[index] = ' ';
            }
            else
            {
                source[index] = character == '\n' ? '\n' : ' ';
                if (mode == Mode::Line && character == '\n')
                {
                    mode = Mode::Code;
                }
                else if (mode == Mode::Block && character == '*' && next == '/')
                {
                    source[++index] = ' ';
                    mode = Mode::Code;
                }
                else if ((mode == Mode::String || mode == Mode::Character) && character == '\\' && index+1 < source.size())
                {
                    source[++index] = ' ';
                }
                else if ((mode == Mode::String && character == '"') || (mode == Mode::Character && character == '\''))
                {
                    mode = Mode::Code;
                }
            }
        }
        return source;
    }
    void CheckOwner(const ApiType& type, const ApiMember& member)
    {
        if (member.Kind == ApiMemberKind::Constructor)
        {
            return;
        }
        const String path = PathFromUri(member.DefinitionUri);
        String owner = type.NativeName;
        const auto generic = owner.find('<');
        if (generic != String::npos)
        {
            owner = owner.Substr(0,generic);
        }
        const auto scope = owner.rfind("::");
        if (scope != String::npos)
        {
            owner = owner.Substr(scope+2);
        }
        if (path.EndsWith("/ComponentHandle.h"))
        {
            owner = "ComponentHandle";
        }
        else if (path.EndsWith("/TextService.h"))
        {
            owner = "TextServiceBase";
        }
        else if (path.EndsWith("/Size.h"))
        {
            owner = "SizeT";
        }
        else if (path.EndsWith("/BitFlag.h"))
        {
            owner = "BitFlagT";
        }
        std::ifstream file(std::filesystem::path(reinterpret_cast<const char8_t*>(path.c_str())), std::ios::binary);
        String source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        source = MaskCommentsAndStrings(std::move(source));
        const std::regex declaration("\\b(?:class|struct|enum\\s+class)\\s+" + owner.Std() + "\\b[^;{}]*\\{");
        Bool found = false;
        for (std::sregex_iterator match(source.begin(),source.end(),declaration), end; match != end; ++match)
        {
            const auto begin = static_cast<std::size_t>(match->position()+match->length());
            std::size_t close = begin;
            std::size_t depth = 1;
            while (close < source.size() && depth != 0)
            {
                if (source[close] == '{')
                {
                    ++depth;
                }
                else if (source[close] == '}')
                {
                    --depth;
                }
                ++close;
            }
            if (member.DefinitionRange.Begin.Offset >= begin && member.DefinitionRange.End.Offset < close)
            {
                found = true;
            }
        }
        if (!found)
        {
            throw std::runtime_error("SDK definition points into a different native owner: " + type.Name.Std() + "::" + member.NativeName.Std());
        }
    }
    void CheckCoreMathOverload(const ApiMember& function)
    {
        const String path = PathFromUri(function.DefinitionUri);
        if (!path.EndsWith("/Math3D.h") && !path.EndsWith("/Matrix4x4.h"))
        {
            return;
        }
        std::ifstream file(std::filesystem::path(reinterpret_cast<const char8_t*>(path.c_str())), std::ios::binary);
        const String source((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
        const auto offset = static_cast<std::size_t>(function.DefinitionRange.Begin.Offset);
        const auto newline = source.rfind('\n',offset);
        const auto first = newline == String::npos ? 0 : newline+1;
        const String prefix = source.Substr(first,offset-first);
        Check(prefix.Contains(function.Type), "global math definition must have the actual resolved return type");
        const auto open = source.find('(',offset);
        const auto close = source.find(')',open);
        Check(open != String::npos && close != String::npos, "math declaration has argument list");
        const auto parameters = source.Substr(open+1,close-open-1).Split(',');
        Check(parameters.Size() == function.Parameters.Size(), "global math definition has resolved overload arity");
        for (std::size_t index = 0; index < parameters.Size(); ++index)
        {
            if (!parameters[index].Contains(function.Parameters[index].Type))
            {
                throw std::runtime_error("global math definition points to a different overload: " + function.Name.Std() + " with " + function.Parameters[index].Type.Std());
            }
        }
    }
    const ApiType& Type(const ApiCatalog& catalog, const char* name)
    {
        for (const auto& type : catalog.Types)
        {
            if (type.Name == name)
            {
                return type;
            }
        }
        throw std::runtime_error(name);
    }
    const ApiMember& Member(const ApiType& type, const char* name)
    {
        for (const auto& member : type.Members)
        {
            if (member.Name == name)
            {
                return member;
            }
        }
        throw std::runtime_error(name);
    }
}

JBro::Int32 RunScriptApiCatalogTests()
{
    const auto catalog = JBro::ScriptApiCatalog2D::BuildCatalog();
    for (const char* alias : {"Time","Random","Objects","Audio","Input","Save","Localization","Network","Session","Physics","Text","Screen","DebugDraw"})
    {
        std::size_t count = 0;
        for (const auto& global : catalog.Globals)
        {
            if (global.Name == alias)
            {
                ++count;
            }
        }
        Check(count == 1, "every required service alias exists exactly once");
    }
    for (const char* name : { "Transform2D", "Collider2D", "DistanceJoint2D", "HingeJoint2D", "Light2D", "ShadowCaster2D", "AudioSource", "GameScript2D", "InputService", "NetworkService" })
    {
        Check(!Type(catalog, name).Members.IsEmpty(), "public API type must have members");
    }
    Check(Member(Type(catalog, "Transform2D"), "position").Writable, "getter plus setter makes position writable");
    Check(!Member(Type(catalog, "Transform2D"), "world").Writable, "world cache has no setter");
    Check(!Member(Type(catalog, "Button2D"), "clicked").Writable, "clicked query has no setter");
    const auto& raycast = Member(Type(catalog, "Physics2DService"), "Raycast");
    Check(raycast.Type == "RaycastHit2D?" && raycast.Parameters.Size() == 4, "raycast out result is nullable return");
    Check(raycast.HasOutputProjection && raycast.Parameters.Last().HasDefault, "projection and default mask retained");
    Check(Member(Type(catalog, "ObjectsService"), "FindAll").Type == "Array<GameObject?>", "object query buffer is collection return");
    Check(Member(Type(catalog, "SaveService"), "ReadText").Type == "String?", "save read is optional text");
    Check(Member(Type(catalog, "GameScript2D"), "OnCollisionEnter").IsCallback, "callback metadata");
    Check(Member(Type(catalog, "Vector2"), "x").Type == "Float", "value fields derive from actual reflection");
    Check(Type(catalog, "BodyType2D").EnumValues.Size() == 3, "enum values derive from actual enum descriptor");
    const auto& capture = Member(Type(catalog,"InputService"),"CaptureBinding");
    Check(capture.Type == "InputCapture" && capture.NativeReturnField == "result", "capture preserves native status separately from output");
    Check(Member(Type(catalog,"InputCapture"),"result").Type == "InputCaptureResult", "capture status enum is retained");
    Check(Member(Type(catalog,"InputCapture"),"binding").Type == "InputBinding", "capture binding is retained");
    Check(Member(Type(catalog,"AssetHandle"),"index").Writable, "inspector readonly is not script mutability");
    std::size_t ranges = 0;
    std::size_t overloads = 0;
    for (const auto& type : catalog.Types)
    {
        Check(type.Name != "Transform3D" && type.Name != "GameScript3D", "2D catalog must not expose 3D");
        String nativeType = type.NativeName;
        const auto generic = nativeType.find('<');
        if (generic != String::npos)
        {
            nativeType = nativeType.Substr(0,generic);
        }
        const auto scope = nativeType.rfind("::");
        if (scope != String::npos)
        {
            nativeType = nativeType.Substr(scope+2);
        }
        CheckDefinition(type.DefinitionUri,type.DefinitionRange,nativeType);
        for (const auto& member : type.Members)
        {
            Check(!member.Type.IsEmpty(), "every declaration has an explicit mapped type");
            Check(member.HasOutputProjection == (member.Projection != ApiProjectionKind::None), "projection kind and normalized flag agree");
            for (UInt32 output : member.NativeOutputParameters)
            {
                Check(output < member.NativeParameterNames.Size(), "native output index retained inside original signature");
            }
            for (const auto& parameter : member.Parameters)
            {
                Check(!parameter.Type.IsEmpty() && !parameter.Name.IsEmpty(), "all parameters typed and named");
            }
            if (!member.DefinitionUri.IsEmpty())
            {
                Check(member.DefinitionRange.End.Offset > member.DefinitionRange.Begin.Offset, "native definition token range");
                CheckDefinition(member.DefinitionUri,member.DefinitionRange,member.NativeName);
                CheckOwner(type,member);
                ++ranges;
            }
            if (type.Name == "RandomService" && member.Name == "Range")
            {
                ++overloads;
            }
        }
    }
    Check(ranges > 200 && overloads == 2, "definition ranges and engine overload coverage");
    for (const char* name : {"Vector3","Vector4","Quaternion","Matrix4x4"})
    {
        const auto& type = Type(catalog,name);
        Check(type.Kind == ApiTypeKind::Value, "Core math values are dimension-independent language values");
        Check(!type.Members.IsEmpty(), "Core math fields and constructors are available in 2D projects");
    }
    Check(Member(Type(catalog,"Vector3"),"z").Type == "Float", "Vector3 reflected z");
    Check(Member(Type(catalog,"Vector4"),"w").Type == "Float", "Vector4 reflected w");
    Check(Member(Type(catalog,"Quaternion"),"w").Type == "Float", "Quaternion reflected w");
    Check(Member(Type(catalog,"Matrix4x4"),"values").Type == "Array<Float>", "Matrix4x4 actual fixed array projection");
    Check(catalog.Functions.Size() >= 50, "all common math and utility free callables are preserved");
    for (const auto& function : catalog.Functions)
    {
        CheckDefinition(function.DefinitionUri,function.DefinitionRange,function.NativeName);
        CheckCoreMathOverload(function);
        Check(function.HasOutputProjection == (function.Projection != ApiProjectionKind::None), "global projection consistency");
    }
    const auto second = JBro::ScriptApiCatalog2D::BuildCatalog();
    Check(second.Types.Size() == catalog.Types.Size(), "catalog is repeatable without host initialization");
    std::cout << "[pass] ScriptApiCatalogTests" << std::endl;
    return JBro::Int32(0);
}
