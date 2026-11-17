#include <JBro/ScriptCompiler/CppEmitter.h>
#include <JBro/ScriptCompiler/Analysis.h>
#include <JBro/ScriptCompiler/Summary.h>
#include <JBro/Core/StableTypeId.h>
#include <JBro/Types/Table.h>
#include "CppIdentifier.h"
#include "CppEmitterParts.h"
#include "QualifiedName.h"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>

namespace JBro::ScriptCompiler
{
    namespace
    {
        struct Type
        {
            String name;
            Bool ref = false;
        };

        struct VariableBinding
        {
            Type type;
            Bool field = false;

            VariableBinding() = default;
            VariableBinding(const Type& value, Bool isField = false) : type(value), field(isField) {}
        };

        struct Expression
        {
            String code;
            Type type;
            Bool lvalue = false;
            Bool property = false;
            String setter;
            Bool isType = false;
        };

        struct DeclarationRef
        {
            const SourceText* source = nullptr;
            const SyntaxTree* tree = nullptr;
            NodeIndex index = InvalidNode;
            SyntaxKind kind = SyntaxKind::CompilationUnit;
        };

        struct FunctionRef
        {
            DeclarationRef owner;
            NodeIndex index = InvalidNode;
        };

        String Escape(std::string_view text)
        {
            String result = "\"";
            for (unsigned char character : text)
            {
                switch (character)
                {
                case '\\': result += "\\\\"; break;
                case '"': result += "\\\""; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += static_cast<char>(character); break;
                }
            }
            result += '"';
            return result;
        }

        String Pascal(std::string_view name)
        {
            String result(name);
            if (!result.empty())
            {
                result[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(result[0])));
            }
            return result;
        }

        String Trim(std::string_view value)
        {
            while (!value.empty() && value.front() == ' ')
            {
                value.remove_prefix(1);
            }
            while (!value.empty() && value.back() == ' ')
            {
                value.remove_suffix(1);
            }
            return String(value);
        }

        Type TextType(std::string_view value)
        {
            if (value.starts_with("const "))
            {
                value.remove_prefix(6);
            }
            Bool ref = false;
            if (value.rfind("ref ", 0) == 0)
            {
                value.remove_prefix(4);
                ref = true;
            }
            if (value.starts_with("const "))
            {
                value.remove_prefix(6);
            }
            return {Trim(value), ref};
        }

        Array<String> TypeArguments(std::string_view name)
        {
            const std::size_t open = name.find('<');
            if (open == std::string_view::npos || name.back() != '>')
            {
                return {};
            }
            name = name.substr(open + 1, name.size() - open - 2);
            Array<String> result;
            Int32 depth = 0;
            std::size_t start = 0;
            for (std::size_t at = 0; at < name.size(); ++at)
            {
                if (name[at] == '<')
                {
                    ++depth;
                }
                else if (name[at] == '>')
                {
                    --depth;
                }
                else if (name[at] == ',' && depth == 0)
                {
                    result.Add(Trim(name.substr(start, at - start)));
                    start = at + 1;
                }
            }
            result.Add(Trim(name.substr(start)));
            return result;
        }

        class Generator final
        {
        public:
            Generator(ArrayView<const CppSource> sources, const ApiCatalog& api,
                Bool emitModuleEntry, const AnalysisResult* analysis, const CppSource* unit = nullptr)
                : m_sources(sources), m_api(api), m_emitModuleEntry(emitModuleEntry), m_analysis(analysis), m_unit(unit)
            {
            }

            CppEmitResult Run()
            {
                m_code = "#include <JBro/ScriptAPI.h>\n#include <JBro/Framework2D/Scripting/ScriptModule.h>\n"
                    "#include <JBro/Runtime/ScriptGuard.h>\n#include <JBro/Runtime/ScriptInitialization.h>\n"
                    "#include <JBro/Runtime/ScriptStatics.h>\n"
                    "#include <JBro/Reflection/EnumDescriptor.h>\n#include <type_traits>\n\n"
                    "template<typename Derived, typename Base, std::size_t Index>\n"
                    "JBro::PropertyInfo jbro_gen_inherited_property()\n"
                    "{\n"
                    "    JBro::PropertyInfo field = JBro::GetPropertyTable<Base>().properties[Index];\n"
                    "    field.Address = [](void* owner) noexcept -> void*\n"
                    "    {\n"
                    "        Base* base = static_cast<Base*>(static_cast<Derived*>(owner));\n"
                    "        return JBro::GetPropertyTable<Base>().properties[Index].Address(base);\n"
                    "    };\n"
                    "    field.ConstAddress = [](const void* owner) noexcept -> const void*\n"
                    "    {\n"
                    "        const Base* base = static_cast<const Base*>(static_cast<const Derived*>(owner));\n"
                    "        return JBro::GetPropertyTable<Base>().properties[Index].ConstAddress(base);\n"
                    "    };\n"
                    "    return field;\n"
                    "}\n\n";
                if (m_sources.IsEmpty())
                {
                    Error("no source files for C++ generation");
                    return Finish();
                }
                for (const CppSource& source : m_sources)
                {
                    if (source.Source == nullptr || source.Tree == nullptr
                        || source.Tree->GetRoot() == InvalidNode)
                    {
                        Error("empty syntax tree");
                        return Finish();
                    }
                    m_source = source.Source;
                    m_tree = source.Tree;
                    IndexDeclarations(m_tree->GetRoot());
                }
                for (const auto& item : m_declarations)
                {
                    m_source = item.MappedValue.source;
                    m_tree = item.MappedValue.tree;
                    m_owner = item.KeyValue;
                    for (NodeIndex member : m_tree->GetChildren(item.MappedValue.index))
                    {
                        if (member != InvalidNode && Node(member).Kind == SyntaxKind::FieldDeclaration)
                        {
                            m_fields[m_owner][String(Node(member).Text)] = ParseType(Child(member, 1));
                        }
                    }
                }
                Array<DeclarationRef> declarations;
                for (const CppSource& source : m_sources)
                {
                    m_source = source.Source;
                    m_tree = source.Tree;
                    OrderTree(m_tree->GetRoot(), declarations);
                }
                if (false == m_errors.IsEmpty())
                {
                    return Finish();
                }
                if (m_unit != nullptr)
                {
                    m_implementation = std::move(m_code);
                    m_code.Clear();
                }
                for (const DeclarationRef& item : declarations)
                {
                    if (m_unit != nullptr)
                    {
                        continue;
                    }
                    const SyntaxNode& declaration = item.tree->Get(item.index);
                    m_code += String(declaration.Kind == SyntaxKind::EnumDeclaration ? "enum class "
                        : declaration.Kind == SyntaxKind::StructDeclaration ? "struct " : "class ")
                        + CppIdentifier(DeclarationName(item.tree, item.index))
                        + (declaration.Kind == SyntaxKind::EnumDeclaration ? " : std::int64_t;\n" : ";\n");
                }
                m_code += '\n';
                for (const DeclarationRef& item : declarations)
                {
                    if (m_unit != nullptr)
                    {
                        continue;
                    }
                    const SyntaxNode& declaration = item.tree->Get(item.index);
                    if (declaration.Kind == SyntaxKind::ScriptDeclaration)
                    {
                        const String name = CppIdentifier(declaration.Text);
                        m_code += "template<> struct JBro::RefCategoryOf<" + name + ">\n"
                            "{\n    static constexpr JBro::RefCategory value = JBro::RefCategory::Script;\n};\n";
                    }
                }
                m_code += '\n';
                for (const DeclarationRef& item : declarations)
                {
                    if (m_unit != nullptr && item.tree != m_unit->Tree)
                    {
                        continue;
                    }
                    m_source = item.source;
                    m_tree = item.tree;
                    Declaration(item.index);
                    if (m_unit != nullptr)
                    {
                        m_parts.Add({DeclarationName(item.tree, item.index), std::move(m_code)});
                        m_code.Clear();
                    }
                }
                if (m_unit != nullptr)
                {
                    m_code = std::move(m_implementation);
                }
                for (const DeclarationRef& item : m_initializationOwners)
                {
                    m_source = item.source;
                    m_tree = item.tree;
                    m_owner = DeclarationName(item.tree, item.index);
                    StaticFields(item.index);
                }
                for (const DeclarationRef& item : m_defaultConstructors)
                {
                    m_source = item.source;
                    m_tree = item.tree;
                    m_owner = DeclarationName(item.tree, item.index);
                    const String owner = CppIdentifier(m_owner);
                    m_code += owner + "::" + owner + "() try";
                    ConstructorInitializers(item.index);
                    m_code += "\n{\n    " + FinishInitializationCode() + ";\n}\n";
                    ConstructorCatch();
                }
                for (const FunctionRef& item : m_functions)
                {
                    m_source = item.owner.source;
                    m_tree = item.owner.tree;
                    m_owner = DeclarationName(item.owner.tree, item.owner.index);
                    Function(item.index, item.owner.kind == SyntaxKind::ScriptDeclaration, false);
                }
                for (const String& script : m_scripts)
                {
                    m_code += "static JBro::ScriptTypeRegistration jbro_gen_script_registration_"
                        + std::to_string(m_temporary++) + "([]() -> JBro::Bool\n{\n"
                        "    return []<typename Script>() -> JBro::Bool\n"
                        "    {\n"
                        "        if constexpr (std::is_abstract_v<Script>)\n"
                        "        {\n"
                        "            return true;\n"
                        "        }\n"
                        "        else\n"
                        "        {\n"
                        "            return JBro::RegisterScriptType2D<Script>();\n"
                        "        }\n"
                        "    }.template operator()<" + script + ">();\n});\n";
                }
                if (!m_scripts.IsEmpty() || m_unit != nullptr)
                {
                    m_code += "namespace { const JBro::Bool jbro_gen_script_2d_marker = "
                        "JBro::Internal::MarkGeneratedScript2D(); }\n";
                    if (m_emitModuleEntry)
                    {
                        m_code += "JBRO_SCRIPT_MODULE_2D_REQUIRING_ERROR_CONTEXT()\n";
                    }
                }
                else if (m_unit == nullptr)
                {
                    Error("a game DLL needs at least one script declaration");
                }
                return Finish();
            }

            Internal::CppUnitParts RunParts()
            {
                CppEmitResult result = Run();
                return {std::move(m_parts), std::move(result.Code), std::move(result.Errors)};
            }

        private:
            const SyntaxNode& Node(NodeIndex index) const { return m_tree->Get(index); }
            NodeIndex Child(NodeIndex index, std::size_t slot) const { return m_tree->GetChild(index, slot); }

            String DeclarationName(const SyntaxTree* tree, NodeIndex index) const
            {
                for (const auto& declaration : m_declarations)
                {
                    if (declaration.MappedValue.tree == tree && declaration.MappedValue.index == index)
                    {
                        return declaration.KeyValue;
                    }
                }
                return String(tree->Get(index).Text);
            }

            static String SimpleName(const String& name)
            {
                const std::size_t dot = name.rfind('.');
                return dot == String::npos ? name : name.Substr(dot + 1);
            }

            String ResolveTypeName(std::string_view spelling, const String* owner = nullptr) const
            {
                const String name = Internal::QualifiedName(spelling);
                for (String scope = owner == nullptr ? m_owner : *owner; false == scope.IsEmpty();)
                {
                    const String candidate = scope + "." + name;
                    if (m_declarations.Contains(candidate))
                    {
                        return candidate;
                    }
                    const std::size_t dot = scope.rfind('.');
                    if (dot == String::npos)
                    {
                        break;
                    }
                    scope.resize(dot);
                }
                return name;
            }

            const Symbol* AnalyzedSymbol(NodeIndex index) const
            {
                if (m_analysis == nullptr || Node(index).Text.empty())
                {
                    return nullptr;
                }
                for (const auto& document : m_analysis->Documents)
                {
                    if (&document->Tree != m_tree)
                    {
                        continue;
                    }
                    const UInt32 begin = static_cast<UInt32>(Node(index).Text.data() - document->Source.GetText().data());
                    const UInt32 end = begin + static_cast<UInt32>(Node(index).Text.size());
                    const Symbol* found = nullptr;
                    UInt32 latest = 0;
                    for (const Occurrence& occurrence : document->Occurrences)
                    {
                        if (occurrence.Range.Begin.Offset >= begin && occurrence.Range.End.Offset <= end
                            && (found == nullptr || occurrence.Range.Begin.Offset >= latest))
                        {
                            found = FindSymbol(*m_analysis, occurrence.Target);
                            latest = occurrence.Range.Begin.Offset;
                        }
                    }
                    return found;
                }
                return nullptr;
            }

            String BaseTypeName(const DeclarationRef& declaration, NodeIndex base) const
            {
                const String owner = DeclarationName(declaration.tree, declaration.index);
                return ResolveTypeName(declaration.tree->Get(base).Text, &owner);
            }

            CppEmitResult Finish()
            {
                CppEmitResult result;
                result.Code = m_code;
                result.Errors = std::move(m_errors);
                return result;
            }

            // 생성 단계의 오류에도 원본 파일과 줄·열을 붙인다 - 전에는 위치 없는 영어 한 줄이라 고칠 곳을 알 수 없었다(Script QA 결함 4).
            // 자리는 마지막으로 들어간 선언·필드·함수·문장·식이다.
            void Error(std::string_view message)
            {
                if (m_source != nullptr && m_tree != nullptr && m_errorNode != InvalidNode && m_errorNode < m_tree->GetNodeCount())
                {
                    const auto& at = m_tree->Get(m_errorNode).Range.Begin;
                    m_errors.Add(String(m_source->GetPath()).Append("(").Append(std::to_string(at.Line.Get())).Append(",")
                        .Append(std::to_string(CharacterColumn(m_source->GetText(), at).Get())).Append("): ").Append(message));
                    return;
                }
                m_errors.Add(String(message));
            }

            Type ParseType(NodeIndex index) const
            {
                if (index == InvalidNode)
                {
                    return {"Void", false};
                }
                const SyntaxNode& node = Node(index);
                const Symbol* analyzed = AnalyzedSymbol(index);
                Type result{analyzed != nullptr ? analyzed->Type : ResolveTypeName(node.Text), (node.Flags & SyntaxFlagRef) != 0};
                for (NodeIndex child : m_tree->GetChildren(index))
                {
                    if (child == InvalidNode)
                    {
                        continue;
                    }
                    if (result.name.find('<') == String::npos)
                    {
                        result.name += '<';
                    }
                    else
                    {
                        result.name += ", ";
                    }
                    const Type argument = ParseType(child);
                    if (argument.ref)
                    {
                        result.name += "ref ";
                    }
                    result.name += argument.name;
                }
                if (result.name.find('<') != String::npos)
                {
                    result.name += '>';
                }
                return result;
            }

            const ApiType* Api(std::string_view name) const
            {
                const std::size_t generic = name.find('<');
                if (generic != std::string_view::npos)
                {
                    name = name.substr(0, generic);
                }
                for (const ApiType& type : m_api.Types)
                {
                    if (type.Name.View() == name)
                    {
                        return &type;
                    }
                }
                return nullptr;
            }

            const ApiMember* ApiMemberInHierarchy(std::string_view typeName,
                std::string_view memberName, std::size_t depth = 0) const
            {
                if (depth > 32)
                {
                    return nullptr;
                }
                if (const ApiType* api = Api(typeName))
                {
                    for (const ApiMember& member : api->Members)
                    {
                        if (member.Name.View() == memberName)
                        {
                            return &member;
                        }
                    }
                    for (const String& base : api->Bases)
                    {
                        if (const ApiMember* inherited = ApiMemberInHierarchy(base.View(), memberName, depth + 1))
                        {
                            return inherited;
                        }
                    }
                }
                if (const DeclarationRef* declaration = m_declarations.Find(String(typeName)))
                {
                    const NodeIndex bases = declaration->tree->GetChild(declaration->index, 0);
                    if (bases != InvalidNode && declaration->tree->Get(bases).Kind == SyntaxKind::BaseList)
                    {
                        for (NodeIndex base : declaration->tree->GetChildren(bases))
                        {
                            if (const ApiMember* inherited = ApiMemberInHierarchy(
                                BaseTypeName(*declaration, base).View(), memberName, depth + 1))
                            {
                                return inherited;
                            }
                        }
                    }
                    if (declaration->kind == SyntaxKind::ScriptDeclaration)
                    {
                        return ApiMemberInHierarchy("GameScript2D", memberName, depth + 1);
                    }
                }
                return nullptr;
            }

            Bool IsHandle(const Type& type) const
            {
                const ApiType* api = Api(type.name);
                return api != nullptr && api->Kind == ApiTypeKind::Handle;
            }

            Bool IsScript(const Type& type) const
            {
                const DeclarationRef* found = m_declarations.Find(type.name);
                return found != nullptr && found->kind == SyntaxKind::ScriptDeclaration;
            }

            String NativeType(const Type& type) const
            {
                if (type.name == "Void")
                {
                    return "void";
                }
                const std::size_t generic = type.name.find('<');
                if (generic != String::npos)
                {
                    const Array<String> arguments = TypeArguments(type.name);
                    String native = "JBro::" + type.name.substr(0, generic) + "<";
                    for (std::size_t index = 0; index < arguments.Size(); ++index)
                    {
                        if (index != 0)
                        {
                            native += ", ";
                        }
                        native += NativeType(TextType(arguments[index]));
                    }
                    native += '>';
                    return type.ref ? "JBro::Nullable<" + native + ">" : native;
                }
                String bare = type.name;
                if (const ApiType* api = Api(type.name); api != nullptr && !api->NativeName.empty())
                {
                    bare = api->NativeName.c_str();
                }
                else if (!m_declarations.Contains(type.name))
                {
                    bare = "JBro::" + bare;
                }
                else
                {
                    bare = CppIdentifier(type.name);
                }
                if (!type.ref || IsHandle(type))
                {
                    return bare;
                }
                if (IsScript(type))
                {
                    return "JBro::Ref<" + bare + ">";
                }
                return "JBro::Nullable<" + bare + ">";
            }

            void IndexDeclarations(NodeIndex root, const String& parent = {})
            {
                for (NodeIndex child : m_tree->GetChildren(root))
                {
                    if (child == InvalidNode)
                    {
                        continue;
                    }
                    const SyntaxNode& node = Node(child);
                    if (node.Kind != SyntaxKind::ScriptDeclaration
                        && node.Kind != SyntaxKind::ClassDeclaration
                        && node.Kind != SyntaxKind::StructDeclaration
                        && node.Kind != SyntaxKind::InterfaceDeclaration
                        && node.Kind != SyntaxKind::EnumDeclaration)
                    {
                        continue;
                    }
                    const String name = parent.IsEmpty() ? String(node.Text) : String(parent + "." + String(node.Text));
                    if (m_declarations.Contains(name))
                    {
                        Error("duplicate C++ declaration across files: " + name);
                        continue;
                    }
                    m_declarations[name] = {m_source, m_tree, child, node.Kind};
                    IndexDeclarations(child, name);
                }
            }

            void OrderTree(NodeIndex root, Array<DeclarationRef>& declarations)
            {
                for (NodeIndex child : m_tree->GetChildren(root))
                {
                    if (child == InvalidNode)
                    {
                        continue;
                    }
                    const String name = DeclarationName(m_tree, child);
                    const SyntaxKind kind = Node(child).Kind;
                    if (kind == SyntaxKind::ScriptDeclaration || kind == SyntaxKind::ClassDeclaration
                        || kind == SyntaxKind::StructDeclaration || kind == SyntaxKind::InterfaceDeclaration || kind == SyntaxKind::EnumDeclaration)
                    {
                        OrderDeclaration(name, declarations);
                        OrderTree(child, declarations);
                    }
                }
            }

            void AddFieldsFromBases(const String& name, Table<String, VariableBinding>& fields)
            {
                const DeclarationRef* declaration = m_declarations.Find(name);
                if (declaration == nullptr)
                {
                    return;
                }
                const NodeIndex bases = declaration->tree->GetChild(declaration->index, 0);
                if (bases != InvalidNode && declaration->tree->Get(bases).Kind == SyntaxKind::BaseList)
                {
                    for (NodeIndex base : declaration->tree->GetChildren(bases))
                    {
                        AddFieldsFromBases(BaseTypeName(*declaration, base), fields);
                    }
                }
                if (const auto* owned = m_fields.Find(name))
                {
                    for (const auto& field : *owned)
                    {
                        fields[field.KeyValue] = VariableBinding(field.MappedValue, true);
                    }
                }
            }

            Type MemberTypeInHierarchy(const String& name, std::string_view memberName)
            {
                const DeclarationRef* declaration = m_declarations.Find(name);
                if (declaration == nullptr)
                {
                    return {};
                }
                if (const auto* fields = m_fields.Find(name))
                {
                    if (const Type* field = fields->Find(String(memberName)))
                    {
                        return *field;
                    }
                }
                for (NodeIndex member : declaration->tree->GetChildren(declaration->index))
                {
                    if (member == InvalidNode)
                    {
                        continue;
                    }
                    const SyntaxNode& item = declaration->tree->Get(member);
                    if (item.Kind == SyntaxKind::FunctionDeclaration && item.Text == memberName)
                    {
                        const SyntaxTree* saved = m_tree;
                        m_tree = declaration->tree;
                        const Type result = ParseType(Child(member, 2));
                        m_tree = saved;
                        return result;
                    }
                }
                const NodeIndex bases = declaration->tree->GetChild(declaration->index, 0);
                if (bases != InvalidNode && declaration->tree->Get(bases).Kind == SyntaxKind::BaseList)
                {
                    for (NodeIndex base : declaration->tree->GetChildren(bases))
                    {
                        const Type result = MemberTypeInHierarchy(BaseTypeName(*declaration, base), memberName);
                        if (!result.name.empty())
                        {
                            return result;
                        }
                    }
                }
                return {};
            }

            struct FieldRef
            {
                const DeclarationRef* Owner = nullptr;
                NodeIndex Index = InvalidNode;
                std::size_t Ordinal = 0;
            };

            FieldRef FindField(const String& owner, std::string_view name) const
            {
                const DeclarationRef* declaration = m_declarations.Find(owner);
                if (declaration == nullptr)
                {
                    return {};
                }
                std::size_t ordinal = 0;
                for (NodeIndex index : declaration->tree->GetChildren(declaration->index))
                {
                    if (index == InvalidNode)
                    {
                        continue;
                    }
                    const SyntaxNode& member = declaration->tree->Get(index);
                    if (member.Text == name)
                    {
                        return member.Kind == SyntaxKind::FieldDeclaration
                            ? FieldRef{declaration, index, ordinal} : FieldRef{};
                    }
                    if (member.Kind == SyntaxKind::FieldDeclaration && !(member.Flags & SyntaxFlagStatic))
                    {
                        ++ordinal;
                    }
                }
                const NodeIndex bases = declaration->tree->GetChild(declaration->index, 0);
                if (bases != InvalidNode && declaration->tree->Get(bases).Kind == SyntaxKind::BaseList)
                {
                    for (NodeIndex base : declaration->tree->GetChildren(bases))
                    {
                        const FieldRef field = FindField(BaseTypeName(*declaration, base), name);
                        if (field.Owner != nullptr)
                        {
                            return field;
                        }
                    }
                }
                return {};
            }

            String InitializationType(const String& owner) const
            {
                return std::to_string(MakeStableTypeId(owner.c_str()).Get()) + "ULL";
            }

            String GuardField(const FieldRef& field, const String& receiver, const String& access, NodeIndex site) const
            {
                if (field.Owner == nullptr)
                {
                    return access;
                }
                const String owner = DeclarationName(field.Owner->tree, field.Owner->index);
                const String key = SiteKey(Node(site));
                if (field.Owner->tree->Get(field.Index).Flags & SyntaxFlagStatic)
                {
                    return CppIdentifier(owner) + "::" + StaticAccessor(field.Owner->tree->Get(field.Index).Text)
                        + "(" + Escape(key) + ", " + Escape(key + " uninitialized static field: " + AccessPath(site)) + ")";
                }
                return "(JBro::ScriptGuard::RequireInitializedField(static_cast<const " + CppIdentifier(owner)
                    + "*>(&" + receiver + "), " + InitializationType(owner) + ", " + std::to_string(field.Ordinal)
                    + ", " + Escape(key) + ", " + Escape(key + " uninitialized field: " + AccessPath(site))
                    + "), (" + access + "))";
            }

            std::size_t ReflectedFieldCount(const String& name) const
            {
                const DeclarationRef* declaration = m_declarations.Find(name);
                if (declaration == nullptr || declaration->kind == SyntaxKind::InterfaceDeclaration)
                {
                    return 0;
                }
                std::size_t count = 0;
                const NodeIndex bases = declaration->tree->GetChild(declaration->index, 0);
                if (bases != InvalidNode && declaration->tree->Get(bases).Kind == SyntaxKind::BaseList)
                {
                    for (NodeIndex base : declaration->tree->GetChildren(bases))
                    {
                        count += ReflectedFieldCount(BaseTypeName(*declaration, base));
                    }
                }
                for (NodeIndex field : declaration->tree->GetChildren(declaration->index))
                {
                    if (field == InvalidNode || declaration->tree->Get(field).Kind != SyntaxKind::FieldDeclaration)
                    {
                        continue;
                    }
                    const NodeIndex attributes = declaration->tree->GetChild(field, 0);
                    if (declaration->kind == SyntaxKind::StructDeclaration
                        || (attributes != InvalidNode && declaration->tree->Get(attributes).ChildCount != 0))
                    {
                        ++count;
                    }
                }
                return count;
            }

            void EmitPropertyTable(NodeIndex index)
            {
                const SyntaxNode& declaration = Node(index);
                const NodeIndex bases = Child(index, 0);
                std::size_t inherited = 0;
                if (bases != InvalidNode && Node(bases).Kind == SyntaxKind::BaseList)
                {
                    for (NodeIndex base : m_tree->GetChildren(bases))
                    {
                        inherited += ReflectedFieldCount(ResolveTypeName(Node(base).Text));
                    }
                }
                const String scriptName = DeclarationName(m_tree, index);
                const String name = CppIdentifier(scriptName);
                const std::size_t total = ReflectedFieldCount(scriptName);
                const std::size_t own = total - inherited;
                if (total == 0)
                {
                    m_code += "template<> const JBro::PropertyTable& JBro::GetPropertyTable<" + name
                        + ">()\n{\n    static const JBro::PropertyTable table{nullptr, 0};\n"
                        "    return table;\n}\n\n";
                    return;
                }
                m_code += "template<> const JBro::PropertyTable& JBro::GetPropertyTable<" + name
                    + ">()\n{\n    static JBro::PropertyInfo fields[" + std::to_string(total)
                    + "]{};\n    static const JBro::PropertyTable table{fields, " + std::to_string(total)
                    + "};\n    return table;\n}\n\n"
                    "void " + name + "::jbro_gen_initialize_properties()\n{\n"
                    "    static JBro::ScriptStaticState state = JBro::ScriptStaticState::Uninitialized;\n"
                    "    if (state == JBro::ScriptStaticState::Ready)\n    {\n        return;\n    }\n"
                    "    state = JBro::ScriptStaticState::Initializing;\n"
                    "    auto* fields = const_cast<JBro::PropertyInfo*>(JBro::GetPropertyTable<" + name + ">().properties);\n"
                    "    static JBro::PropertyEditInfo edits[" + std::to_string(own == 0 ? 1 : own) + "]{};\n";
                std::size_t destination = 0;
                if (bases != InvalidNode && Node(bases).Kind == SyntaxKind::BaseList)
                {
                    for (NodeIndex base : m_tree->GetChildren(bases))
                    {
                        const String baseName = ResolveTypeName(Node(base).Text);
                        const std::size_t count = ReflectedFieldCount(baseName);
                        if (count != 0)
                        {
                            m_code += "    " + CppIdentifier(baseName) + "::jbro_gen_initialize_properties();\n";
                        }
                        for (std::size_t field = 0; field < count; ++field)
                        {
                            m_code += "        fields[" + std::to_string(destination++)
                                + "] = jbro_gen_inherited_property<" + name + ", "
                                + CppIdentifier(baseName) + ", " + std::to_string(field) + ">();\n";
                        }
                    }
                }
                for (std::size_t field = 0; field < own; ++field)
                {
                    if (m_unit != nullptr)
                    {
                        m_code += "        " + name + "::jbro_gen_field_" + std::to_string(field)
                            + "(fields[" + std::to_string(destination++) + "], edits["
                            + std::to_string(field) + "]);\n";
                        continue;
                    }
                    m_code += "        JBro::Detail::ApplyFieldEntry(" + name
                        + "::JBroFieldAt(JBro::FieldIndex<" + std::to_string(field)
                        + ">{ }), fields[" + std::to_string(destination++)
                        + "], edits[" + std::to_string(field) + "]);\n";
                }
                m_code += "    state = JBro::ScriptStaticState::Ready;\n}\n"
                    "static JBro::ScriptSchemaRegistration jbro_gen_schema_" + name + "(&" + name
                    + "::jbro_gen_initialize_properties);\n\n";
            }

            Bool OrderDeclaration(const String& name, Array<DeclarationRef>& ordered)
            {
                const DeclarationRef* found = m_declarations.Find(name);
                if (found == nullptr)
                {
                    Error("C++ emitter does not support this declaration yet");
                    return false;
                }
                // 표 원소의 참조를 쥔 채 재귀하지 않는다. 재귀가 새 이름을 넣어 표가 다시 잡히면 참조가 끊겨, 끝의 "다 했음" 이
                // 엉뚱한 곳에 쓰이고 다이아몬드 상속이 순환으로 보였다(Script QA, 선언 수에 따라 나타남).
                const Int32 state = m_declarationState[name];
                if (state == 2)
                {
                    return true;
                }
                if (state == 1)
                {
                    Error("owned field cycle cannot be represented in C++: " + name);
                    return false;
                }
                m_declarationState[name] = 1;
                const NodeIndex bases = found->tree->GetChild(found->index, 0);
                if (bases != InvalidNode && found->tree->Get(bases).Kind == SyntaxKind::BaseList)
                {
                    for (NodeIndex base : found->tree->GetChildren(bases))
                    {
                        const String previousOwner = m_owner;
                        m_owner = name;
                        const String baseName = ResolveTypeName(found->tree->Get(base).Text);
                        m_owner = previousOwner;
                        if (m_declarations.Contains(baseName) && !OrderDeclaration(baseName, ordered))
                        {
                            return false;
                        }
                    }
                }
                for (NodeIndex field : found->tree->GetChildren(found->index))
                {
                    if (field == InvalidNode || found->tree->Get(field).Kind != SyntaxKind::FieldDeclaration
                        || found->tree->Get(field).Flags & SyntaxFlagStatic)
                    {
                        continue;
                    }
                    const Type& type = m_fields[name][String(found->tree->Get(field).Text)];
                    if (m_declarations.Contains(type.name) && !IsScript(type)
                        && !OrderDeclaration(type.name, ordered))
                    {
                        return false;
                    }
                }
                m_declarationState[name] = 2;
                ordered.Add(*found);
                return true;
            }

            void Declaration(NodeIndex index)
            {
                if (index == InvalidNode)
                {
                    return;
                }
                m_errorNode = index;
                const SyntaxNode& node = Node(index);
                if (node.Kind != SyntaxKind::ScriptDeclaration
                    && node.Kind != SyntaxKind::ClassDeclaration
                    && node.Kind != SyntaxKind::StructDeclaration
                    && node.Kind != SyntaxKind::InterfaceDeclaration
                    && node.Kind != SyntaxKind::EnumDeclaration)
                {
                    Error("C++ emitter does not support this declaration yet");
                    return;
                }
                m_owner = DeclarationName(m_tree, index);
                const String nativeOwner = CppIdentifier(m_owner);
                if (node.Kind == SyntaxKind::EnumDeclaration)
                {
                    if (node.ChildCount == 0)
                    {
                        // 멤버 없는 enum 은 C++ 처럼 받는다(D-377). 이름 붙은 값이 없는 설명서를 낸다.
                        m_code += "enum class " + nativeOwner + " : std::int64_t\n{\n};\n";
                        if (m_unit != nullptr)
                        {
                            m_code.swap(m_implementation);
                            m_code += "const JBro::TypeDescriptor& JBro::TypeDescriptorOf<" + nativeOwner
                                + ">::Get()\n{\n    return JBro::DefineEmptyEnumTypeDescriptor<" + nativeOwner + ">("
                                + Escape(m_owner) + ");\n}\n\n";
                            m_code.swap(m_implementation);
                            return;
                        }
                        m_code += "namespace JBro { template <> struct TypeDescriptorOf<::" + nativeOwner
                            + "> { static const TypeDescriptor& Get() { return ::JBro::DefineEmptyEnumTypeDescriptor<::" + nativeOwner
                            + ">(" + Escape(m_owner) + "); } }; }\n\n";
                        return;
                    }
                    m_code += "enum class " + nativeOwner + " : std::int64_t\n{\n";
                    std::size_t memberCount = 0;
                    for (NodeIndex member : m_tree->GetChildren(index))
                    {
                        if (memberCount++ != 0)
                        {
                            m_code += ",\n";
                        }
                        m_code += "    " + CppIdentifier(Node(member).Text);
                    }
                    m_code += "\n};\n";
                    if (m_unit != nullptr)
                    {
                        m_code.swap(m_implementation);
                    }
                    m_code += m_unit != nullptr
                        ? "const JBro::TypeDescriptor& JBro::TypeDescriptorOf<" + nativeOwner
                            + ">::Get()\n{\n    static const JBro::EnumEntry<" + nativeOwner + "> entries[] = {"
                        : "namespace JBro { JBRO_DEFINE_ENUM_TYPE(::" + nativeOwner + ", " + Escape(m_owner) + ", ";
                    memberCount = 0;
                    for (NodeIndex member : m_tree->GetChildren(index))
                    {
                        if (memberCount++ != 0)
                        {
                            m_code += ", ";
                        }
                        const String name(Node(member).Text);
                        m_code += "{::" + nativeOwner + "::" + CppIdentifier(name)
                            + ", " + Escape(name) + "}";
                    }
                    m_code += m_unit != nullptr
                        ? "};\n    return JBro::DefineEnumTypeDescriptor<" + nativeOwner + ">(" + Escape(m_owner)
                            + ", entries);\n}\n\n"
                        : "); }\n\n";
                    if (m_unit != nullptr)
                    {
                        m_code.swap(m_implementation);
                    }
                    return;
                }
                const Bool script = node.Kind == SyntaxKind::ScriptDeclaration;
                const Bool interfaceType = node.Kind == SyntaxKind::InterfaceDeclaration;
                if (script)
                {
                    m_scripts.Add(nativeOwner);
                }
                String bases;
                Bool hasScriptBase = false;
                const NodeIndex baseList = Child(index, 0);
                if (baseList != InvalidNode && Node(baseList).Kind == SyntaxKind::BaseList)
                {
                    for (NodeIndex base : m_tree->GetChildren(baseList))
                    {
                        const Type type = ParseType(base);
                        const DeclarationRef* declared = m_declarations.Find(type.name);
                        if (declared != nullptr && declared->kind == SyntaxKind::ScriptDeclaration)
                        {
                            hasScriptBase = true;
                        }
                    }
                }
                if (script && !hasScriptBase)
                {
                    bases = "public JBro::GameScript2D";
                }
                if (baseList != InvalidNode && Node(baseList).Kind == SyntaxKind::BaseList)
                {
                    for (NodeIndex base : m_tree->GetChildren(baseList))
                    {
                        const Type type = ParseType(base);
                        const DeclarationRef* declared = m_declarations.Find(type.name);
                        if (!bases.empty())
                        {
                            bases += ", ";
                        }
                        bases += String(declared != nullptr && declared->kind == SyntaxKind::InterfaceDeclaration
                            ? "virtual public " : "public ") + NativeType(type);
                    }
                }
                m_code += String(node.Kind == SyntaxKind::StructDeclaration ? "struct " : "class ")
                    + nativeOwner + (bases.empty() ? "\n" : " : " + bases + "\n") + "{\npublic:\n";
                for (const auto& nested : m_declarations)
                {
                    if (nested.KeyValue.StartsWith(m_owner + ".") && nested.MappedValue.kind != SyntaxKind::EnumDeclaration)
                    {
                        m_code += String(nested.MappedValue.kind == SyntaxKind::StructDeclaration ? "    friend struct " : "    friend class ")
                            + CppIdentifier(nested.KeyValue) + ";\n";
                    }
                }
                if (script)
                {
                    m_code += "    static constexpr const char* StaticTypeName() { return "
                        + Escape(m_owner) + "; }\n"
                        "    JBro::ComponentTypeId GetTypeId() const override\n"
                        "    { return JBro::MakeStableTypeId(StaticTypeName()); }\n";
                }
                else if (interfaceType)
                {
                    m_code += "    virtual ~" + nativeOwner + "() = default;\n";
                }
                std::size_t reflectedIndex = 0;
                Bool hasConstructor = false;
                for (NodeIndex member : m_tree->GetChildren(index))
                {
                    if (member == InvalidNode)
                    {
                        continue;
                    }
                    if (Node(member).Kind == SyntaxKind::FieldDeclaration)
                    {
                        MemberAccess(member, node.Kind);
                        Field(member, node.Kind == SyntaxKind::StructDeclaration, reflectedIndex);
                        const NodeIndex attributes = Child(member, 0);
                        if (node.Kind == SyntaxKind::StructDeclaration
                            || (attributes != InvalidNode && Node(attributes).ChildCount != 0))
                        {
                            ++reflectedIndex;
                        }
                    }
                    else if (Node(member).Kind == SyntaxKind::FunctionDeclaration)
                    {
                        hasConstructor = hasConstructor || FunctionName(Node(member)) == SimpleName(m_owner);
                        MemberAccess(member, node.Kind);
                        Function(member, script, true);
                        if (Child(member, 3) != InvalidNode)
                        {
                            m_functions.Add({{m_source, m_tree, index, node.Kind}, member});
                        }
                    }
                    else if (Node(member).Kind == SyntaxKind::BaseList)
                    {
                        continue;
                    }
                    else if (Node(member).Kind == SyntaxKind::ClassDeclaration || Node(member).Kind == SyntaxKind::StructDeclaration
                        || Node(member).Kind == SyntaxKind::InterfaceDeclaration || Node(member).Kind == SyntaxKind::EnumDeclaration)
                    {
                        MemberAccess(member, node.Kind);
                        m_code += "    using " + CppIdentifier(Node(member).Text) + " = ::"
                            + CppIdentifier(DeclarationName(m_tree, member)) + ";\n";
                    }
                    else
                    {
                        Error("C++ emitter does not support this member yet");
                    }
                }
                if (!interfaceType)
                {
                    if (ReflectedFieldCount(m_owner) != 0)
                    {
                        m_code += "    public:\n    static void jbro_gen_initialize_properties();\n";
                    }
                    if (HasStaticFields(index))
                    {
                        m_code += "    public:\n    static void jbro_gen_initialize_statics();\n"
                            "    static void jbro_gen_destroy_statics() noexcept;\n";
                    }
                    const DeclarationRef owner{m_source, m_tree, index, node.Kind};
                    m_initializationOwners.Add(owner);
                    if (!hasConstructor)
                    {
                        m_code += "    public:\n    " + nativeOwner + "();\n";
                        m_defaultConstructors.Add(owner);
                    }
                }
                m_code += "};\n\n";
                if (m_unit != nullptr)
                {
                    m_code.swap(m_implementation);
                }
                if (!interfaceType)
                {
                    EmitPropertyTable(index);
                }
                if (!script && !interfaceType)
                {
                    m_code += (m_unit != nullptr
                        ? "const JBro::TypeDescriptor& JBro::TypeDescriptorOf<" + nativeOwner + ">::Get()\n{\n"
                        : "template<> struct JBro::TypeDescriptorOf<" + nativeOwner + ">\n{\n"
                            "    static const JBro::TypeDescriptor& Get()\n    {\n")
                        + "        static const JBro::TypeDescriptor descriptor = "
                        "JBro::MakeStructTypeDescriptor<" + nativeOwner + ">(" + Escape(m_owner)
                        + ", JBro::GetPropertyTable<" + nativeOwner + ">());\n"
                        "        return descriptor;\n" + String(m_unit != nullptr ? "}\n\n" : "    }\n};\n\n");
                }
                if (m_unit != nullptr)
                {
                    m_code.swap(m_implementation);
                }
            }

            void MemberAccess(NodeIndex index, SyntaxKind ownerKind)
            {
                const std::uint16_t flags = Node(index).Flags;
                const Bool publicDefault = ownerKind == SyntaxKind::StructDeclaration
                    || ownerKind == SyntaxKind::InterfaceDeclaration;
                const char* access = (flags & SyntaxFlagPublic) ? "public"
                    : (flags & SyntaxFlagProtected) ? "protected"
                    : (flags & SyntaxFlagPrivate) ? "private"
                    : publicDefault ? "public" : "private";
                m_code += "    " + String(access) + ":\n";
            }

            String StaticAccessor(std::string_view name) const
            {
                return "jbro_gen_static_" + CppIdentifier(name);
            }

            Bool HasStaticFields(NodeIndex owner) const
            {
                for (NodeIndex field : m_tree->GetChildren(owner))
                {
                    if (field != InvalidNode && Node(field).Kind == SyntaxKind::FieldDeclaration
                        && Node(field).Flags & SyntaxFlagStatic)
                    {
                        return true;
                    }
                }
                return false;
            }

            String FieldDeclaration(NodeIndex index, Bool constant, Bool reflected) const
            {
                const SyntaxNode& node = Node(index);
                const String native = NativeType(ParseType(Child(index, 1)));
                if (node.Flags & SyntaxFlagStatic)
                {
                    const String key = SiteKey(node);
                    return "static " + String(constant && !reflected ? "const " : "") + native + "& "
                        + StaticAccessor(node.Text) + "(const char* key = " + Escape(key)
                        + ", const char* message = " + Escape(key + " uninitialized static field: " + String(node.Text)) + ")";
                }
                return String(constant && !reflected ? "const " : "") + native + " " + CppIdentifier(node.Text);
            }

            void Field(NodeIndex index, Bool isStruct, std::size_t reflectedIndex)
            {
                m_errorNode = index;
                const SyntaxNode& node = Node(index);
                const NodeIndex typeNode = Child(index, 1);
                const Bool constant = typeNode != InvalidNode && (Node(typeNode).Flags & SyntaxFlagConst);
                const Bool staticField = (node.Flags & SyntaxFlagStatic) != 0;
                const String name = CppIdentifier(node.Text);
                const NodeIndex attributes = Child(index, 0);
                const Bool reflected = isStruct || (attributes != InvalidNode && Node(attributes).ChildCount != 0);
                String attributeCode;
                Bool explicitNoSerialize = false;
                if (attributes != InvalidNode)
                {
                    for (NodeIndex attribute : m_tree->GetChildren(attributes))
                    {
                        if (attribute == InvalidNode)
                        {
                            continue;
                        }
                        const SyntaxNode& declaration = Node(attribute);
                        const std::string_view text = declaration.Text;
                        if (text == "prop")
                        {
                            if (declaration.ChildCount != 0)
                            {
                                Error("prop attribute takes no arguments");
                            }
                            continue;
                        }
                        const FieldAttributeDefinition* definition = FindFieldAttributeDefinition(text);
                        if (nullptr == definition)
                        {
                            Error("unknown reflected field attribute: " + String(text));
                            continue;
                        }
                        const String native = definition->NativeName;
                        const std::size_t arity = definition->ArgumentCount;
                        if (text == "noserialize")
                        {
                            explicitNoSerialize = true;
                        }
                        if (declaration.ChildCount != arity)
                        {
                            Error("reflected field attribute argument count: " + String(text));
                            continue;
                        }
                        if (!attributeCode.empty())
                        {
                            attributeCode += " | ";
                        }
                        attributeCode += native + "(";
                        // `Range`·`Clamp` 는 `Float` 둘을 받는다. `[range(1, 100)]` 의 정수는 `Float` 로 저절로 바뀌지 않으므로(D-290) 명시해 넘긴다.
                        const Bool floatArguments = text == "range" || text == "clamp";
                        for (std::size_t argument = 0; argument < arity; ++argument)
                        {
                            if (argument != 0)
                            {
                                attributeCode += ", ";
                            }
                            const String value = Expr(Child(attribute, argument)).code;
                            attributeCode += floatArguments ? String(NativeType({"Float", false}) + "(" + value + ")") : value;
                        }
                        attributeCode += ")";
                    }
                }
                if (reflected && (constant || staticField))
                {
                    if (constant)
                    {
                        if (!attributeCode.empty())
                        {
                            attributeCode += " | ";
                        }
                        attributeCode += "ReadOnly()";
                    }
                    if (!explicitNoSerialize)
                    {
                        if (!attributeCode.empty())
                        {
                            attributeCode += " | ";
                        }
                        attributeCode += "NoSerialize()";
                    }
                }
                if (reflected)
                {
                    const Bool privateField = (node.Flags & SyntaxFlagPrivate)
                        || (!isStruct && 0 == (node.Flags & SyntaxFlagPublic));
                    const String memberAccess = node.Flags & SyntaxFlagProtected ? "protected"
                        : privateField ? "private" : "public";
                    if (m_unit != nullptr)
                    {
                        const String factory = "jbro_gen_field_" + std::to_string(reflectedIndex);
                        m_code += "    public:\n    static void " + factory
                            + "(JBro::PropertyInfo& property, JBro::PropertyEditInfo& edit);\n    "
                            + memberAccess + ":\n    " + FieldDeclaration(index, constant, true);
                        m_implementation += "void " + CppIdentifier(m_owner) + "::" + factory
                            + "(JBro::PropertyInfo& property, JBro::PropertyEditInfo& edit)\n{\n"
                            "    using namespace JBro::Attribute;\n    auto field = JBro::"
                            + String(staticField ? "MakeStaticAccessorFieldEntry<&" : "MakeFieldEntry<&")
                            + CppIdentifier(m_owner) + "::" + (staticField ? StaticAccessor(node.Text) : name)
                            + ">(" + (staticField ? Escape(node.Text) + ", " : String()) + attributeCode + ");\n"
                            "    field.name = " + Escape(node.Text) + ";\n"
                            "    JBro::Detail::ApplyFieldEntry(field, property, edit);\n}\n\n";
                    }
                    else
                    {
                        m_code += "    public:\n"
                        "    static constexpr auto JBroFieldAt(JBro::FieldIndex<"
                        + std::to_string(reflectedIndex) + ">)\n    {\n"
                        "        using namespace JBro::Attribute;\n"
                        "        auto field = JBro::"
                        + String(staticField ? "MakeStaticAccessorFieldEntry<&" : "MakeFieldEntry<&")
                        + CppIdentifier(m_owner) + "::" + (staticField ? StaticAccessor(node.Text) : name)
                        + ">(" + (staticField ? Escape(node.Text) + ", " : String()) + attributeCode + ");\n"
                        "        field.name = " + Escape(node.Text) + ";\n"
                        "        return field;\n    }\n    " + memberAccess + ":\n"
                        "    " + FieldDeclaration(index, constant, true);
                    }
                }
                else
                {
                    m_code += "    " + FieldDeclaration(index, constant, false);
                }
                m_code += ";\n";
            }

            String FieldInitializer(NodeIndex field)
            {
                const NodeIndex initializer = Child(field, 2);
                if (initializer == InvalidNode)
                {
                    return "{}";
                }
                const Expression value = Expr(initializer);
                return value.type.name == "Null" ? String("{}")
                    : AdaptValue(ParseType(Child(field, 1)), value, initializer);
            }

            void ConstructorInitializers(NodeIndex owner)
            {
                Table<String, VariableBinding> parameters = std::move(m_variables);
                m_variables.Clear();
                AddFieldsFromBases(m_owner, m_variables);
                m_fieldInitializer = true;
                Bool first = true;
                std::size_t ordinal = 0;
                for (NodeIndex field : m_tree->GetChildren(owner))
                {
                    if (field == InvalidNode || Node(field).Kind != SyntaxKind::FieldDeclaration
                        || Node(field).Flags & SyntaxFlagStatic)
                    {
                        continue;
                    }
                    m_code += first ? "\n    : " : ",\n      ";
                    first = false;
                    const String initializer = FieldInitializer(field);
                    const String value = initializer == "{}" ? String(NativeType(ParseType(Child(field, 1))) + "{}") : initializer;
                    const String key = SiteKey(Node(field));
                    const String begin = "JBro::ScriptGuard::BeginFieldInitialization(this, " + InitializationType(m_owner)
                        + ", " + std::to_string(ordinal++) + ", " + Escape(key) + ", "
                        + Escape(key + " construction nesting limit exceeded") + ")";
                    m_code += CppIdentifier(Node(field).Text)
                        + "((" + begin + ", " + value + "))";
                }
                m_fieldInitializer = false;
                m_variables = std::move(parameters);
            }

            String FinishInitializationCode() const
            {
                return "JBro::ScriptGuard::FinishInitialization(this, " + InitializationType(m_owner) + ")";
            }

            void ConstructorCatch()
            {
                m_code += "catch (...)\n{\n    " + FinishInitializationCode() + ";\n    throw;\n}\n\n";
            }

            void StaticFields(NodeIndex owner)
            {
                Array<NodeIndex> fields;
                for (NodeIndex field : m_tree->GetChildren(owner))
                {
                    if (field != InvalidNode && Node(field).Kind == SyntaxKind::FieldDeclaration
                        && Node(field).Flags & SyntaxFlagStatic)
                    {
                        fields.Add(field);
                    }
                }
                if (fields.IsEmpty())
                {
                    return;
                }
                m_variables.Clear();
                AddFieldsFromBases(m_owner, m_variables);
                m_fieldInitializer = true;
                m_staticInitializer = true;
                const String nativeOwner = CppIdentifier(m_owner);
                const String prefix = "jbro_gen_statics_" + nativeOwner;
                m_code += "namespace\n{\n    unsigned char " + prefix + "_owner = 0;\n"
                    "    JBro::ScriptStaticState " + prefix + "_state = JBro::ScriptStaticState::Uninitialized;\n";
                for (NodeIndex field : fields)
                {
                    m_code += "    JBro::ScriptStaticValue<" + NativeType(ParseType(Child(field, 1))) + "> "
                        + prefix + "_" + CppIdentifier(Node(field).Text) + ";\n";
                }
                m_code += "    JBro::ScriptStaticRegistration " + prefix + "_registration(&" + nativeOwner
                    + "::jbro_gen_initialize_statics, &" + nativeOwner + "::jbro_gen_destroy_statics);\n}\n\n";
                m_code += "void " + nativeOwner + "::jbro_gen_initialize_statics()\n{\n"
                    "    if (" + prefix + "_state != JBro::ScriptStaticState::Uninitialized)\n    {\n        return;\n    }\n"
                    "    " + prefix + "_state = JBro::ScriptStaticState::Initializing;\n    try\n    {\n";
                std::size_t ordinal = 0;
                for (NodeIndex field : fields)
                {
                    const String key = SiteKey(Node(field));
                    const String native = NativeType(ParseType(Child(field, 1)));
                    const String initializer = FieldInitializer(field);
                    m_code += "        JBro::ScriptGuard::BeginFieldInitialization(&" + prefix + "_owner, "
                        + InitializationType(m_owner) + ", " + std::to_string(ordinal++) + ", " + Escape(key)
                        + ", " + Escape(key + " static initialization nesting limit exceeded") + ");\n"
                        "        " + prefix + "_" + CppIdentifier(Node(field).Text) + ".Construct([&]() -> " + native
                        + " { return " + (initializer == "{}" ? String(native + "{}") : initializer) + "; });\n";
                }
                m_code += "        JBro::ScriptGuard::FinishInitialization(&" + prefix + "_owner, " + InitializationType(m_owner)
                    + ");\n        " + prefix + "_state = JBro::ScriptStaticState::Ready;\n        " + prefix
                    + "_registration.MarkInitialized();\n    }\n    catch (...)\n    {\n"
                    "        JBro::ScriptGuard::FinishInitialization(&" + prefix + "_owner, " + InitializationType(m_owner)
                    + ");\n        jbro_gen_destroy_statics();\n        throw;\n    }\n}\n\n";
                m_code += "void " + nativeOwner + "::jbro_gen_destroy_statics() noexcept\n{\n";
                for (std::size_t at = fields.Size(); at > 0; --at)
                {
                    m_code += "    " + prefix + "_" + CppIdentifier(Node(fields[at - 1]).Text) + ".Destroy();\n";
                }
                m_code += "    " + prefix + "_state = JBro::ScriptStaticState::Uninitialized;\n}\n\n";
                ordinal = 0;
                for (NodeIndex field : fields)
                {
                    const NodeIndex attributes = Child(field, 0);
                    const Bool reflected = Node(owner).Kind == SyntaxKind::StructDeclaration
                        || (attributes != InvalidNode && Node(attributes).ChildCount != 0);
                    const Bool constant = (Node(Child(field, 1)).Flags & SyntaxFlagConst) && !reflected;
                    const String key = SiteKey(Node(field));
                    m_code += String(constant ? "const " : "") + NativeType(ParseType(Child(field, 1))) + "& "
                        + nativeOwner + "::" + StaticAccessor(Node(field).Text) + "(const char* key, const char* message)\n{\n"
                        "    jbro_gen_initialize_statics();\n"
                        "    JBro::ScriptGuard::RequireInitializedField(&" + prefix + "_owner, " + InitializationType(m_owner)
                        + ", " + std::to_string(ordinal++) + ", key != nullptr ? key : " + Escape(key)
                        + ", message != nullptr ? message : " + Escape(key + " uninitialized static field: " + String(Node(field).Text))
                        + ");\n    return " + prefix + "_" + CppIdentifier(Node(field).Text) + ".Get();\n}\n\n";
                }
                m_fieldInitializer = false;
                m_staticInitializer = false;
            }

            void Function(NodeIndex index, Bool script, Bool declaration)
            {
                m_errorNode = index;
                const SyntaxNode& node = Node(index);
                const Bool constructor = FunctionName(node) == SimpleName(m_owner);
                const Bool staticMethod = (node.Flags & SyntaxFlagStatic) != 0;
                const DeclarationRef* owner = m_declarations.Find(m_owner);
                const Bool abstractMethod = (node.Flags & SyntaxFlagRequire) != 0
                    || (owner != nullptr && owner->kind == SyntaxKind::InterfaceDeclaration);
                const Type returns = ParseType(Child(index, 2));
                m_returnType = returns;
                const ApiMember* apiMember = script ? ApiMemberInHierarchy(m_owner.View(), FunctionName(node)) : nullptr;
                const Bool callback = script && ((node.Flags & SyntaxFlagCallback) != 0
                    || (apiMember != nullptr && apiMember->IsCallback));
                const String nativeFunction = constructor ? CppIdentifier(m_owner)
                    : (node.Flags & (SyntaxFlagOperatorEqual | SyntaxFlagOperatorNotEqual))
                        ? String(FunctionName(node)) : CppIdentifier(FunctionName(node));
                m_variables.Clear();
                AddFieldsFromBases(m_owner, m_variables);
                m_code += String(declaration ? "    " : "")
                    + (declaration && !constructor && !staticMethod ? "virtual " : "")
                    + (declaration && staticMethod ? "static " : "")
                    + (constructor ? "" : NativeType(returns) + " ")
                    + (declaration ? "" : CppIdentifier(m_owner) + "::") + nativeFunction + "(";
                const NodeIndex parameters = Child(index, 1);
                if (parameters != InvalidNode)
                {
                    std::size_t count = 0;
                    for (NodeIndex parameter : m_tree->GetChildren(parameters))
                    {
                        if (count++ != 0)
                        {
                            m_code += ", ";
                        }
                        const Type type = ParseType(Child(parameter, 0));
                        const Bool constant = (Node(Child(parameter, 0)).Flags & SyntaxFlagConst) != 0;
                        m_variables[String(Node(parameter).Text)] = type.ref
                            ? Type{type.name, false} : type;
                        m_code += "[[maybe_unused]] " + String(constant ? "const " : "")
                            + (type.ref ? NativeType({type.name, false}) + "&" : NativeType(type))
                            + " " + CppIdentifier(Node(parameter).Text);
                    }
                }
                m_code += ")";
                if (declaration && node.Flags & (SyntaxFlagCallback | SyntaxFlagOverride))
                {
                    m_code += " override";
                }
                if (declaration && abstractMethod)
                {
                    m_code += " = 0";
                }
                const NodeIndex body = Child(index, 3);
                if (body == InvalidNode)
                {
                    if (!declaration && !abstractMethod)
                    {
                        Error("C++ emitter does not support methods without a body yet");
                    }
                    m_code += ";\n";
                    return;
                }
                if (declaration)
                {
                    m_code += ";\n";
                    return;
                }
                if (constructor && owner != nullptr)
                {
                    m_code += " try";
                    ConstructorInitializers(owner->index);
                }
                m_code += "\n{\n";
                if (constructor)
                {
                    m_code += "    " + FinishInitializationCode() + ";\n";
                }
                if (callback)
                {
                    m_code += "    try\n    {\n";
                }
                for (NodeIndex statement : m_tree->GetChildren(body))
                {
                    Statement(statement, callback ? 8 : 4);
                }
                if (callback)
                {
                    m_code += "    }\n    catch (const JBro::ScriptGuard::AbortCallback&)\n    {\n        return;\n    }\n";
                }
                m_code += "}\n\n";
                if (constructor)
                {
                    ConstructorCatch();
                }
            }

            String SiteKey(const SyntaxNode& node) const
            {
                const auto line = node.Range.End.Line.Get();
                const auto end = node.Range.End.Column.Get();
                const auto column = end > node.Text.size() ? end - node.Text.size() : end;
                return String(m_source->GetPath().c_str()) + ':' + std::to_string(line) + ':'
                    + std::to_string(column) + ':' + String(node.Text);
            }

            String AccessPath(NodeIndex index) const
            {
                if (index == InvalidNode)
                {
                    return "<expression>";
                }
                const SyntaxNode& node = Node(index);
                if (node.Kind == SyntaxKind::NameExpression
                    || node.Kind == SyntaxKind::GenericNameExpression)
                {
                    return String(node.Text);
                }
                if (node.Kind == SyntaxKind::MemberAccessExpression)
                {
                    return AccessPath(Child(index, 0)) + '.' + String(node.Text);
                }
                if (node.Kind == SyntaxKind::NullAssertExpression)
                {
                    return AccessPath(Child(index, 0)) + '!';
                }
                if (node.Kind == SyntaxKind::IndexExpression)
                {
                    return AccessPath(Child(index, 0)) + "[...]";
                }
                if (node.Kind == SyntaxKind::CallExpression)
                {
                    return AccessPath(Child(index, 0)) + "()";
                }
                if (node.Kind == SyntaxKind::ParenthesizedExpression)
                {
                    return AccessPath(Child(index, 0));
                }
                return "<expression>";
            }

            String GuardValueCopy(const Type& target, const String& code, NodeIndex sourceIndex) const
            {
                const DeclarationRef* owner = m_declarations.Find(target.name);
                if (owner == nullptr || (owner->kind != SyntaxKind::ClassDeclaration && owner->kind != SyntaxKind::StructDeclaration)
                    || Node(sourceIndex).Kind == SyntaxKind::RefArgument)
                {
                    return code;
                }
                const String key = SiteKey(Node(sourceIndex));
                return "JBro::ScriptGuard::RequireInitializedValue<" + CppIdentifier(target.name) + ">((" + code
                    + "), " + InitializationType(target.name) + ", " + Escape(key) + ", "
                    + Escape(key + " incomplete object value: " + AccessPath(sourceIndex)) + ")";
            }

            String WithReceiver(const String& receiver, const String& returns, const String& body,
                const char* parameter = "jbro_gen_receiver") const
            {
                // A temporary passed as a parameter lives to the end of the
                // enclosing expression, including calls borrowing its fields.
                return "([&](auto&& " + String(parameter) + ") -> " + returns + " { " + body + " }((" + receiver + ")))";
            }

            String AdaptValue(const Type& target, const Expression& value, NodeIndex sourceIndex) const
            {
                if (target.ref && !value.type.ref && target.name == value.type.name
                    && !IsHandle(value.type) && !IsScript(value.type))
                {
                    return NativeType(target) + "(" + GuardValueCopy(target, value.code, sourceIndex) + ")";
                }
                if (!target.ref && value.type.ref && target.name == value.type.name
                    && !IsHandle(value.type) && !IsScript(value.type))
                {
                    const String site = SiteKey(Node(sourceIndex));
                    return WithReceiver(value.code, NativeType(target), "return "
                        + GuardValueCopy(target, "JBro::ScriptGuard::Require(jbro_gen_value, " + Escape(site)
                            + ", " + Escape(site + " invalid ref value: " + AccessPath(sourceIndex)) + ")", sourceIndex)
                        + ";", "jbro_gen_value");
                }
                return !target.ref && !value.type.ref ? GuardValueCopy(target, value.code, sourceIndex) : value.code;
            }

            Type AnalyzedType(NodeIndex index) const
            {
                if (m_analysis == nullptr)
                {
                    return {};
                }
                for (const auto& document : m_analysis->Documents)
                {
                    if (&document->Tree == m_tree)
                    {
                        return TextType(GetExpressionType(*m_analysis, *document, index));
                    }
                }
                return {};
            }

            // `a?.b.c` 에서 `?.` 는 뒤의 `.c` 까지 건너뛴다 - 분석이 받는 쪽의 null 건너뛰기를 사슬 끝까지 넘기는 것과 같게 본다
            // (`Analysis.cpp` 의 `receiver.NullSafe`). 넘기지 않으면 `.c` 가 `Require` 로 내려가 `a` 가 없을 때 콜백을 멈추고, 결과가 값이 되어
            // 이어지는 `??` 가 값에 `IsPresent` 를 불렀다.
            // 받는 쪽(첫 자식)으로만 내려가므로 반드시 끝난다. 상한은 두지 않는다 - 식은 100 겹을 넘지 않고(D-374), 상한이 있으면
            // 그 너머의 `?.` 를 놓쳐 `Require` 로 떨어진다(REVIEW R2-3).
            Bool InNullSafeChain(NodeIndex index) const
            {
                while (index != InvalidNode)
                {
                    const SyntaxNode& node = Node(index);
                    if (node.Kind == SyntaxKind::MemberAccessExpression)
                    {
                        if (node.Flags & SyntaxFlagNullSafe)
                        {
                            return true;
                        }
                        index = Child(index, 0);
                    }
                    else if (node.Kind == SyntaxKind::CallExpression && Node(Child(index, 0)).Kind == SyntaxKind::MemberAccessExpression)
                    {
                        index = Child(index, 0);
                    }
                    else
                    {
                        return false;
                    }
                }
                return false;
            }

            // 출력 인자로 투영한 엔진 함수(lsp-plan §5 - 스크립트는 반환값으로 받는다)를 원래 C++ 호출로 되돌린다. `target` 은 받는 쪽과
            // 구분자까지(`(x).`·`T::`)이고, 돌려주는 글자는 `callType` 을 돌려주는 람다의 몸이다. 출력 하나를 성공 여부나 모음으로 받는 모양과
            // 찾은 값의 포인터를 돌려주는 모양을 내린다. 버퍼와 길이(`Count`)·반환값의 필드를 섞는 모양은 아직 내리지 못한다고 알린다.
            String ProjectedCallBody(const ApiMember& member, const String& target, const String& method,
                const Array<String>& arguments, const Type& callType)
            {
                const String result = NativeType(callType);
                String joined;
                for (std::size_t index = 0; index < arguments.Size(); ++index)
                {
                    joined += index == 0 ? "" : ", ";
                    joined += arguments[index];
                }
                if (member.NativeOutputParameters.IsEmpty())
                {
                    if (member.Projection != ApiProjectionKind::NullableReturn)
                    {
                        Error("C++ emitter does not support the output projection of " + String(member.Name));
                        return {};
                    }
                    return "auto* jbro_gen_found = " + target + method + "(" + joined + "); if (jbro_gen_found == nullptr) { return {}; } "
                        "return " + result + "(*jbro_gen_found);";
                }
                const Bool supported = member.NativeOutputParameters.Size() == 1 && member.NativeReturnField.IsEmpty()
                    && (member.Projection == ApiProjectionKind::NullableReturn || member.Projection == ApiProjectionKind::CollectionReturn);
                String native;
                std::size_t next = 0;
                Bool outputPlaced = false;
                Bool countParameter = false;
                for (std::size_t index = 0; supported && index < member.NativeParameterNames.Size(); ++index)
                {
                    const Bool output = member.NativeOutputParameters[0] == index;
                    const Bool input = next < member.Parameters.Size() && member.Parameters[next].Name == member.NativeParameterNames[index];
                    if (!output && !input)
                    {
                        countParameter = true;
                        break;
                    }
                    if (input && next >= arguments.Size())
                    {
                        // 뒤의 입력은 C++ 기본 인자로 둔다. 출력이 아직 뒤에 있으면 그 자리를 채울 수 없다.
                        break;
                    }
                    native += native.empty() ? "" : ", ";
                    native += output ? String("jbro_gen_out") : arguments[next];
                    next += output ? 0 : 1;
                    outputPlaced = outputPlaced || output;
                }
                if (!supported || countParameter || !outputPlaced)
                {
                    Error("C++ emitter does not support the output projection of " + String(member.Name));
                    return {};
                }
                const String call = target + method + "(" + native + ")";
                const String body = NativeType({callType.name, false}) + " jbro_gen_out{}; ";
                if (member.Projection == ApiProjectionKind::NullableReturn && member.SuccessFromNativeReturn)
                {
                    return body + "if (" + call + ") { return " + result + "(std::move(jbro_gen_out)); } return {};";
                }
                return body + call + "; return " + result + "(std::move(jbro_gen_out));";
            }

            const CallSite* AnalyzedCall(NodeIndex index) const
            {
                if (m_analysis == nullptr)
                {
                    return nullptr;
                }
                const SourceRange& range = Node(index).Range;
                for (const auto& document : m_analysis->Documents)
                {
                    if (&document->Tree != m_tree)
                    {
                        continue;
                    }
                    return document->Calls.FindBy([&](const CallSite& call)
                    {
                        return call.Range.Begin.Offset == range.Begin.Offset && call.Range.End.Offset == range.End.Offset;
                    });
                }
                return nullptr;
            }

            Bool IsFieldName(NodeIndex index) const
            {
                const VariableBinding* variable = m_variables.Find(String(Node(index).Text));
                return variable != nullptr && variable->field;
            }

            Expression Expr(NodeIndex index)
            {
                if (index == InvalidNode)
                {
                    return {};
                }
                m_errorNode = index;
                const SyntaxNode& node = Node(index);
                switch (node.Kind)
                {
                case SyntaxKind::NameExpression:
                {
                    const String name(node.Text);
                    if (name == "this")
                    {
                        return {"(*this)", {m_owner, false}, true};
                    }
                    if (const Symbol* symbol = AnalyzedSymbol(index); symbol != nullptr)
                    {
                        if (symbol->Kind == SymbolKind::Class || symbol->Kind == SymbolKind::Struct
                            || symbol->Kind == SymbolKind::Interface || symbol->Kind == SymbolKind::Enum || symbol->Kind == SymbolKind::Type)
                        {
                            const String type = ResolveTypeName(symbol->Type.View());
                            return {NativeType({type, false}), {type, false}, false, false, {}, true};
                        }
                        if (!symbol->Api && symbol->Parent != InvalidSymbol && symbol->IsStatic
                            && m_analysis->Symbols[symbol->Parent].Type != m_owner)
                        {
                            const String owner = m_analysis->Symbols[symbol->Parent].Type;
                            const String code = CppIdentifier(owner) + "::" + CppIdentifier(name);
                            return {GuardField(FindField(owner, name), {}, code, index), TextType(symbol->Type), true};
                        }
                    }
                    const VariableBinding* found = m_variables.Find(name);
                    if (found != nullptr)
                    {
                        const String receiver = m_fieldInitializer
                            ? (m_staticInitializer ? CppIdentifier(m_owner) + "::" : String("this->")) : String();
                        const String code = receiver + CppIdentifier(name);
                        return {IsFieldName(index) ? GuardField(FindField(m_owner, name), "(*this)", code, index) : code,
                            found->type, true};
                    }
                    if (m_declarations.Contains(m_owner))
                    {
                        const Type result = MemberTypeInHierarchy(m_owner, name);
                        if (!result.name.empty())
                        {
                            const String receiver = m_fieldInitializer
                                ? (m_staticInitializer ? CppIdentifier(m_owner) + "::" : String("this->")) : String();
                            return {receiver + CppIdentifier(name), result};
                        }
                    }
                    for (const ApiGlobal& global : m_api.Globals)
                    {
                        if (global.Name == name && !global.NativeExpression.empty())
                        {
                            return {"JBro::" + global.NativeExpression, TextType(global.Type), true};
                        }
                    }
                    if (name == "Int" || name == "Float" || name == "Bool" || name == "String"
                        || Api(name) != nullptr || m_declarations.Contains(ResolveTypeName(name.View())))
                    {
                        const String type = ResolveTypeName(name.View());
                        return {NativeType({type, false}), {type, false}, false, false, {}, true};
                    }
                    if (IsScript({m_owner, false}))
                    {
                        if (const ApiType* base = Api("GameScriptBase"))
                        {
                            for (const ApiMember& member : base->Members)
                            {
                                if (member.Name.View() == name)
                                {
                                    return {member.NativeName.empty() ? name : String(member.NativeName.c_str()),
                                        TextType(member.Type.c_str())};
                                }
                            }
                        }
                    }
                    Error("unresolved name in C++ emitter: " + name);
                    return {name, {}, true};
                }
                case SyntaxKind::GenericNameExpression:
                {
                    const Type type = ParseType(index);
                    if (Api(type.name) == nullptr && !m_declarations.Contains(type.name))
                    {
                        Error("unresolved generic name in C++ emitter: " + type.name);
                        return {};
                    }
                    return {NativeType(type), type, false, false, {}, true};
                }
                case SyntaxKind::IntegerLiteral: return {String(node.Text), {"Int", false}};
                case SyntaxKind::FloatLiteral:
                {
                    String literal(node.Text);
                    if (literal.empty() || (literal.back() != 'f' && literal.back() != 'F'))
                    {
                        literal += 'f';
                    }
                    return {literal, {"Float", false}};
                }
                case SyntaxKind::StringLiteral: return {String(node.Text), {"String", false}};
                case SyntaxKind::BooleanLiteral: return {String(node.Text), {"Bool", false}};
                case SyntaxKind::NullLiteral: return {"{}", {"Null", true}};
                case SyntaxKind::ParenthesizedExpression:
                {
                    Expression inner = Expr(Child(index, 0));
                    inner.code = "(" + inner.code + ")";
                    return inner;
                }
                case SyntaxKind::MemberAccessExpression:
                {
                    Expression receiver = Expr(Child(index, 0));
                    if (receiver.isType)
                    {
                        const Symbol* symbol = AnalyzedSymbol(index);
                        const String nested = symbol != nullptr && (symbol->Kind == SymbolKind::Class || symbol->Kind == SymbolKind::Struct
                            || symbol->Kind == SymbolKind::Interface || symbol->Kind == SymbolKind::Enum)
                            ? symbol->Type : String(receiver.type.name + "." + String(node.Text));
                        if (m_declarations.Contains(nested))
                        {
                            return {NativeType({nested, false}), {nested, false}, false, false, {}, true};
                        }
                    }
                    if (const DeclarationRef* declaration = m_declarations.Find(receiver.type.name);
                        declaration != nullptr && declaration->kind == SyntaxKind::EnumDeclaration)
                    {
                        for (NodeIndex member : declaration->tree->GetChildren(declaration->index))
                        {
                            if (declaration->tree->Get(member).Text == node.Text)
                            {
                                return {receiver.code + "::" + CppIdentifier(node.Text), receiver.type};
                            }
                        }
                        Error("unresolved enum member in C++ emitter: " + String(node.Text));
                        return {};
                    }
                    Type memberType;
                    Bool property = false;
                    String nativeMember = CppIdentifier(node.Text);
                    String setter;
                    memberType = MemberTypeInHierarchy(receiver.type.name, node.Text);
                    if (memberType.name.empty())
                    {
                        if (const ApiMember* member = ApiMemberInHierarchy(receiver.type.name, node.Text))
                        {
                            const String declared(member->Type.c_str());
                            memberType = TextType(declared);
                            if (memberType.name == "T" || memberType.name == "V")
                            {
                                const Array<String> arguments = TypeArguments(receiver.type.name);
                                const std::size_t slot = memberType.name == "V" ? 1 : 0;
                                if (slot < arguments.Size())
                                {
                                    memberType.name = TextType(arguments[slot]).name;
                                }
                            }
                            if (memberType.name == "T" && node.ChildCount > 1)
                            {
                                memberType = ParseType(Child(index, 1));
                                memberType.ref = TextType(declared).ref;
                            }
                            nativeMember = member->NativeName.empty() ? Pascal(node.Text) : member->NativeName.c_str();
                            const ApiType* receiverApi = Api(receiver.type.name);
                            property = receiverApi != nullptr && receiverApi->Kind == ApiTypeKind::Handle
                                && member->Kind == ApiMemberKind::Field;
                            if (member->Writable && nativeMember.rfind("Get", 0) == 0)
                            {
                                setter = "Set" + nativeMember.substr(3);
                            }
                            else if (member->Writable && nativeMember.rfind("Is", 0) == 0)
                            {
                                setter = "Set" + nativeMember.substr(2);
                            }
                        }
                    }
                    if (memberType.name.empty())
                    {
                        Error("unresolved member in C++ emitter: " + String(node.Text));
                    }
                    const String access = receiver.isType ? "::" + nativeMember
                        : property ? "." + nativeMember + "()" : "." + nativeMember;
                    const FieldRef field = FindField(receiver.type.name, node.Text);
                    if (receiver.isType)
                    {
                        return {GuardField(field, String(), receiver.code + access, index), memberType, field.Owner != nullptr};
                    }
                    if ((node.Flags & SyntaxFlagNullSafe) || (receiver.type.ref && InNullSafeChain(Child(index, 0))))
                    {
                        if (memberType.name.empty())
                        {
                            return {};
                        }
                        const String bare = NativeType({memberType.name, false});
                        const Bool alreadyNullable = memberType.ref || IsHandle(memberType);
                        const String resultType = alreadyNullable
                            ? NativeType(memberType) : "JBro::Nullable<" + bare + ">";
                        const String code = WithReceiver(receiver.code, resultType,
                            "if (!JBro::ScriptGuard::IsPresent(jbro_gen_receiver)) return {}; "
                            "return " + (alreadyNullable ? "" : "JBro::Nullable<" + bare + ">(")
                            + GuardField(field, "(JBro::ScriptGuard::UnwrapPresent(jbro_gen_receiver))",
                                "JBro::ScriptGuard::UnwrapPresent(jbro_gen_receiver)" + access, index)
                            + (alreadyNullable ? "" : ")") + ";");
                        memberType.ref = true;
                        return {code, memberType};
                    }
                    if (receiver.type.ref || IsHandle(receiver.type))
                    {
                        const String key = Escape(SiteKey(node));
                        const String message = Escape(SiteKey(node) + " invalid ref access: " + AccessPath(index));
                        const String checked = "JBro::ScriptGuard::Require(jbro_gen_receiver, " + key + ", " + message + ")";
                        const String code = WithReceiver(receiver.code, "decltype(auto)", "auto&& jbro_gen_checked = "
                            + checked + "; return (" + GuardField(field, "(jbro_gen_checked)", "jbro_gen_checked" + access, index)
                            + ");");
                        return {code, memberType, !property, property, setter};
                    }
                    if (field.Owner != nullptr && !receiver.isType)
                    {
                        const Bool lvalue = receiver.lvalue || (field.Owner->tree->Get(field.Index).Flags & SyntaxFlagStatic);
                        const String code = WithReceiver(receiver.code, "decltype(auto)", "return ("
                            + GuardField(field, "(jbro_gen_receiver)",
                                "(std::forward<decltype(jbro_gen_receiver)>(jbro_gen_receiver))" + access, index) + ");");
                        return {code, memberType, lvalue};
                    }
                    return {"(" + receiver.code + ")" + access, memberType, !property, property, setter};
                }
                case SyntaxKind::NullAssertExpression:
                {
                    Expression receiver = Expr(Child(index, 0));
                    if (!receiver.type.ref && !IsHandle(receiver.type))
                    {
                        return receiver;
                    }
                    const String key = Escape(SiteKey(node));
                    const String message = Escape(SiteKey(node) + " invalid ref assertion: " + AccessPath(index));
                    receiver.code = WithReceiver(receiver.code, "decltype(auto)",
                        "return JBro::ScriptGuard::Require(jbro_gen_receiver, " + key + ", " + message + ");");
                    receiver.type.ref = false;
                    receiver.lvalue = true;
                    return receiver;
                }
                case SyntaxKind::IsNullExpression:
                {
                    const Expression value = Expr(Child(index, 0));
                    const Bool negated = (node.Flags & SyntaxFlagNegated) != 0;
                    return {String(negated ? "JBro::ScriptGuard::IsPresent(" : "!JBro::ScriptGuard::IsPresent(")
                        + value.code + ")", {"Bool", false}};
                }
                case SyntaxKind::UnaryExpression:
                {
                    const Expression value = Expr(Child(index, 0));
                    return {String(node.Text == "not" ? "!" : "-") + "(" + value.code + ")",
                        node.Text == "not" ? Type{"Bool", false} : value.type};
                }
                case SyntaxKind::BinaryExpression:
                {
                    const Expression left = Expr(Child(index, 0));
                    const Expression right = Expr(Child(index, 1));
                    if (node.Text == "??")
                    {
                        if (left.type.name == "Null")
                        {
                            return right;
                        }
                        const String bare = NativeType({left.type.name, false});
                        const String code = WithReceiver(left.code, bare, "if (JBro::ScriptGuard::IsPresent(jbro_gen_left)) return "
                            + GuardValueCopy({left.type.name, false}, "JBro::ScriptGuard::UnwrapPresent(jbro_gen_left)", Child(index, 0))
                            + "; return " + AdaptValue({left.type.name, false}, right, Child(index, 1)) + ";", "jbro_gen_left");
                        return {code, {left.type.name, false}};
                    }
                    if (node.Text == "==" || node.Text == "!=")
                    {
                        const Bool leftNull = left.type.name == "Null";
                        const Bool rightNull = right.type.name == "Null";
                        if (leftNull && rightNull)
                        {
                            return {node.Text == "==" ? "true" : "false", {"Bool", false}};
                        }
                        if (leftNull || rightNull)
                        {
                            const Expression& handle = leftNull ? right : left;
                            if (IsHandle(handle.type) || (handle.type.ref && IsScript(handle.type)))
                            {
                                return {String(node.Text == "==" ? "!" : "")
                                    + "JBro::ScriptGuard::IsPresent(" + handle.code + ")", {"Bool", false}};
                            }
                        }
                        if (node.Text == "!=" && (IsHandle(left.type)
                            || (left.type.ref && IsScript(left.type))))
                        {
                            // Engine handles return JBro::Bool from operator==, so C++20 does
                            // not synthesize operator!= from that result type.
                            return {"!(" + left.code + " == " + right.code + ")", {"Bool", false}};
                        }
                        const CallSite* call = AnalyzedCall(index);
                        if (call != nullptr && call->ParameterTypes.Size() == 1)
                        {
                            return {"(" + left.code + " " + String(node.Text) + " "
                                + AdaptValue(TextType(call->ParameterTypes[0]), right, Child(index, 1)) + ")", {"Bool", false}};
                        }
                    }
                    const Bool vectorPair = left.type.name == "Vector2" && right.type.name == "Vector2"
                        && (node.Text == "+" || node.Text == "-");
                    const Bool vectorScale = left.type.name == "Vector2" && right.type.name == "Float"
                        && (node.Text == "*" || node.Text == "/");
                    const Bool reverseVectorScale = left.type.name == "Float" && right.type.name == "Vector2"
                        && node.Text == "*";
                    if (vectorPair || vectorScale || reverseVectorScale)
                    {
                        const String lhsX = left.type.name == "Vector2" ? "jbro_gen_left.x" : "jbro_gen_left";
                        const String lhsY = left.type.name == "Vector2" ? "jbro_gen_left.y" : "jbro_gen_left";
                        const String rhsX = right.type.name == "Vector2" ? "jbro_gen_right.x" : "jbro_gen_right";
                        const String rhsY = right.type.name == "Vector2" ? "jbro_gen_right.y" : "jbro_gen_right";
                        const String operation(node.Text);
                        const String code = "([&]() -> JBro::Vector2 { const auto jbro_gen_left = (" + left.code
                            + "); const auto jbro_gen_right = (" + right.code + "); return { "
                            + lhsX + " " + operation + " " + rhsX + ", "
                            + lhsY + " " + operation + " " + rhsY + " }; }())";
                        return {code, {"Vector2", false}};
                    }
                    const String op = node.Text == "and" ? "&&" : node.Text == "or" ? "||" : String(node.Text);
                    const Bool booleanResult = node.Text == "==" || node.Text == "!=" || node.Text == "<"
                        || node.Text == "<=" || node.Text == ">" || node.Text == ">="
                        || node.Text == "and" || node.Text == "or";
                    return {"(" + left.code + " " + op + " " + right.code + ")",
                        booleanResult ? Type{"Bool", false} : left.type};
                }
                case SyntaxKind::CallExpression:
                {
                    const NodeIndex calleeIndex = Child(index, 0);
                    const Expression callee = Expr(calleeIndex);
                    const Type analyzedType = AnalyzedType(index);
                    const Type callType = analyzedType.name.empty() ? callee.type : analyzedType;
                    const SyntaxNode& calleeNode = Node(calleeIndex);
                    const CallSite* analyzedCall = AnalyzedCall(index);
                    String arguments;
                    Array<String> argumentList;
                    for (std::size_t slot = 1; slot < node.ChildCount; ++slot)
                    {
                        if (slot != 1)
                        {
                            arguments += ", ";
                        }
                        const NodeIndex argumentIndex = Child(index, slot);
                        const Expression argument = Expr(argumentIndex);
                        argumentList.Add(analyzedCall != nullptr && slot - 1 < analyzedCall->ParameterTypes.Size()
                            ? AdaptValue(TextType(analyzedCall->ParameterTypes[slot - 1]), argument, argumentIndex)
                            : argument.code);
                        arguments += argumentList.Last();
                    }
                    if (calleeNode.Kind == SyntaxKind::MemberAccessExpression)
                    {
                        const Expression receiver = Expr(Child(calleeIndex, 0));
                        String method = CppIdentifier(calleeNode.Text);
                        const ApiMember* apiMember = ApiMemberInHierarchy(receiver.type.name, calleeNode.Text);
                        if (apiMember != nullptr)
                        {
                            method = apiMember->NativeName.empty() ? Pascal(calleeNode.Text)
                                : String(apiMember->NativeName.c_str());
                        }
                        for (std::size_t slot = 1; slot < calleeNode.ChildCount; ++slot)
                        {
                            method += slot == 1 ? "<" : ", ";
                            method += NativeType(ParseType(Child(calleeIndex, slot)));
                        }
                        if (calleeNode.ChildCount > 1)
                        {
                            method += '>';
                        }
                        const Bool nullSafe = (calleeNode.Flags & SyntaxFlagNullSafe) || (receiver.type.ref && InNullSafeChain(Child(calleeIndex, 0)));
                        if (apiMember != nullptr && apiMember->HasOutputProjection)
                        {
                            const String resultType = NativeType(callType);
                            if (nullSafe)
                            {
                                return {WithReceiver(receiver.code, resultType, "if (!JBro::ScriptGuard::IsPresent(jbro_gen_receiver)) return {}; "
                                    + ProjectedCallBody(*apiMember, "JBro::ScriptGuard::UnwrapPresent(jbro_gen_receiver).", method, argumentList, callType)),
                                    callType};
                            }
                            if (receiver.type.ref || IsHandle(receiver.type))
                            {
                                const String site = SiteKey(calleeNode);
                                return {WithReceiver(receiver.code, resultType, "auto&& jbro_gen_target = JBro::ScriptGuard::Require(jbro_gen_receiver, "
                                    + Escape(site) + ", " + Escape(site + " invalid ref access: " + AccessPath(calleeIndex)) + "); "
                                    + ProjectedCallBody(*apiMember, "jbro_gen_target.", method, argumentList, callType)), callType};
                            }
                            const String target = receiver.isType ? receiver.code + "::" : "(" + receiver.code + ").";
                            return {"[&]() -> " + resultType + " { " + ProjectedCallBody(*apiMember, target, method, argumentList, callType) + " }()",
                                callType};
                        }
                        if (nullSafe)
                        {
                            if (callType.name == "Void")
                            {
                                const String code = WithReceiver(receiver.code, "void",
                                    "if (!JBro::ScriptGuard::IsPresent(jbro_gen_receiver)) "
                                    "return; JBro::ScriptGuard::UnwrapPresent(jbro_gen_receiver)."
                                    + method + "(" + arguments + ");");
                                return {code, callType};
                            }
                            const String resultType = NativeType(callType);
                            const String code = WithReceiver(receiver.code, resultType,
                                "if (!JBro::ScriptGuard::IsPresent(jbro_gen_receiver)) "
                                "return {}; return " + resultType + "(JBro::ScriptGuard::UnwrapPresent(jbro_gen_receiver)."
                                + method + "(" + arguments + "));");
                            return {code, callType};
                        }
                        if (receiver.type.ref || IsHandle(receiver.type))
                        {
                            const String site = SiteKey(calleeNode);
                            const String code = WithReceiver(receiver.code, "decltype(auto)",
                                "return JBro::ScriptGuard::Require(jbro_gen_receiver, "
                                + Escape(site) + ", " + Escape(site + " invalid ref access: "
                                    + AccessPath(calleeIndex)) + ")." + method + "(" + arguments + ");");
                            return {code, callType};
                        }
                        return {receiver.isType ? receiver.code + "::" + method + "(" + arguments + ")"
                            : "(" + receiver.code + ")." + method + "(" + arguments + ")", callType};
                    }
                    return {callee.code + "(" + arguments + ")", callType};
                }
                case SyntaxKind::RefArgument:
                {
                    const NodeIndex source = Child(index, 0);
                    Expression borrowed = Expr(source);
                    if (borrowed.type.ref)
                    {
                        const String site = SiteKey(Node(source));
                        borrowed.code = WithReceiver(borrowed.code, "decltype(auto)",
                            "return JBro::ScriptGuard::Require(jbro_gen_borrowed, " + Escape(site) + ", "
                            + Escape(site + " invalid borrowed ref: " + AccessPath(source)) + ");", "jbro_gen_borrowed");
                        borrowed.type.ref = false;
                    }
                    return borrowed;
                }
                case SyntaxKind::IndexExpression:
                {
                    const Expression receiver = Expr(Child(index, 0));
                    const Expression key = Expr(Child(index, 1));
                    const Array<String> arguments = TypeArguments(receiver.type.name);
                    const Type element = arguments.IsEmpty() ? Type{} : TextType(arguments[arguments.Size() - 1]);
                    const Bool table = receiver.type.name.rfind("Table<", 0) == 0;
                    const String nativeKey = table && arguments.Size() == 2
                        ? NativeType(TextType(arguments[0])) + "(" + key.code + ")"
                        : "static_cast<std::size_t>(" + key.code + ")";
                    if (receiver.type.ref)
                    {
                        const String site = SiteKey(node);
                        const String code = WithReceiver(receiver.code, "decltype(auto)",
                            "auto&& jbro_gen_checked = JBro::ScriptGuard::Require(jbro_gen_receiver, "
                            + Escape(site) + ", " + Escape(site + " invalid ref index: " + AccessPath(index))
                            + "); return jbro_gen_checked[" + nativeKey + "];");
                        return {code, element, true};
                    }
                    return {receiver.code + "[" + nativeKey + "]", element, true};
                }
                default:
                    Error("C++ emitter does not support this expression yet");
                    return {};
                }
            }

            void Statement(NodeIndex index, Int32 indent)
            {
                m_errorNode = index;
                const SyntaxNode& node = Node(index);
                const String pad(static_cast<std::size_t>(indent), ' ');
                m_code += "#line " + std::to_string(node.Range.Begin.Line.Get()) + " "
                    + Escape(m_source->GetPath().c_str()) + "\n";
                if (node.Kind == SyntaxKind::Block)
                {
                    const auto saved = m_variables;
                    m_code += pad + "{\n";
                    for (NodeIndex child : m_tree->GetChildren(index))
                    {
                        Statement(child, indent + 4);
                    }
                    m_code += pad + "}\n";
                    m_variables = saved;
                    return;
                }
                if (node.Kind == SyntaxKind::LocalDeclaration)
                {
                    const NodeIndex typeIndex = Child(index, 0);
                    const Type type = ParseType(typeIndex);
                    const Bool constant = typeIndex != InvalidNode
                        && (Node(typeIndex).Flags & SyntaxFlagConst) != 0;
                    m_variables[String(node.Text)] = type;
                    m_code += pad + (constant ? "const " : "")
                        + NativeType(type) + " " + CppIdentifier(node.Text);
                    if (Child(index, 1) != InvalidNode)
                    {
                        const Expression value = Expr(Child(index, 1));
                        m_code += " = " + (value.type.name == "Null" ? "{}"
                            : AdaptValue(type, value, Child(index, 1)));
                    }
                    else m_code += " = {}";
                    m_code += ";\n";
                    return;
                }
                if (node.Kind == SyntaxKind::AssignmentStatement)
                {
                    const NodeIndex targetIndex = Child(index, 0);
                    const Expression target = Expr(targetIndex);
                    const Expression value = Expr(Child(index, 1));
                    if (target.property && !target.setter.empty()
                        && Node(targetIndex).Kind == SyntaxKind::MemberAccessExpression)
                    {
                        const Expression receiver = Expr(Child(targetIndex, 0));
                        const SyntaxNode& member = Node(targetIndex);
                        const String key = Escape(SiteKey(member));
                        const String message = Escape(SiteKey(member) + " invalid ref access: " + AccessPath(targetIndex));
                        String getter;
                        if (const ApiType* api = Api(receiver.type.name))
                        {
                            for (const ApiMember& candidate : api->Members)
                            {
                                if (candidate.Name.View() == member.Text)
                                {
                                    getter = candidate.NativeName.empty() ? Pascal(member.Text)
                                        : String(candidate.NativeName.c_str());
                                    break;
                                }
                            }
                        }
                        if (node.Text != "=" && getter.empty())
                        {
                            Error("compound property assignment has no getter");
                            return;
                        }
                        m_code += pad + "([&]() { auto&& jbro_gen_receiver = (" + receiver.code + "); "
                            "auto&& jbro_gen_checked = JBro::ScriptGuard::Require(jbro_gen_receiver, " + key + ", " + message + "); "
                            + (target.type.name == "String" ? "JBro::String jbro_gen_value(" : "auto jbro_gen_value = (")
                            + value.code + "); ";
                        if (node.Text != "=")
                        {
                            const String operation(node.Text);
                            m_code += NativeType(target.type) + " jbro_gen_current(jbro_gen_checked." + getter + "()); ";
                            if (target.type.name == "Vector2")
                            {
                                const String operand = operation.substr(0, operation.size() - 1);
                                const String rhs = value.type.name == "Vector2" ? "jbro_gen_value.x" : "jbro_gen_value";
                                m_code += "jbro_gen_current.x " + operand + "= " + rhs + "; ";
                                m_code += "jbro_gen_current.y " + operand + "= "
                                    + (value.type.name == "Vector2" ? "jbro_gen_value.y" : "jbro_gen_value") + "; ";
                            }
                            else
                            {
                                m_code += "jbro_gen_current " + operation + " jbro_gen_value; ";
                            }
                        }
                        const String assigned = node.Text == "=" ? "jbro_gen_value" : "jbro_gen_current";
                        m_code += "jbro_gen_checked." + target.setter + "("
                            + assigned + (target.type.name == "String" ? ".c_str()" : "")
                            + "); }());\n";
                    }
                    else
                    {
                        if (target.type.name == "Vector2" && node.Text != "=")
                        {
                            const String operation(node.Text.substr(0, node.Text.size() - 1));
                            const String rhsX = value.type.name == "Vector2" ? "jbro_gen_value.x" : "jbro_gen_value";
                            const String rhsY = value.type.name == "Vector2" ? "jbro_gen_value.y" : "jbro_gen_value";
                            m_code += pad + "([&]() { auto&& jbro_gen_target = (" + target.code
                                + "); const auto jbro_gen_value = (" + value.code + "); jbro_gen_target.x "
                                + operation + "= " + rhsX + "; jbro_gen_target.y " + operation + "= "
                                + rhsY + "; }());\n";
                        }
                        else
                        {
                            m_code += pad + target.code + " " + String(node.Text) + " "
                                + AdaptValue(target.type, value, Child(index, 1)) + ";\n";
                        }
                    }
                    return;
                }
                if (node.Kind == SyntaxKind::IfStatement)
                {
                    m_code += pad + "if (" + Expr(Child(index, 0)).code + ")\n";
                    Statement(Child(index, 1), indent);
                    if (Child(index, 2) != InvalidNode)
                    {
                        m_code += pad + "else\n";
                        Statement(Child(index, 2), indent);
                    }
                    return;
                }
                if (node.Kind == SyntaxKind::WhileStatement)
                {
                    m_code += pad + "while (" + Expr(Child(index, 0)).code + ")\n";
                    Statement(Child(index, 1), indent);
                    return;
                }
                if (node.Kind == SyntaxKind::ForStatement)
                {
                    if (Node(Child(index, 2)).Kind == SyntaxKind::RangeExpression)
                    {
                        const NodeIndex variable = Child(index, 0);
                        if (Node(variable).Flags & SyntaxFlagRef)
                        {
                            Error("a range loop variable cannot borrow an element");
                            return;
                        }
                        const NodeIndex range = Child(index, 2);
                        const Expression start = Expr(Child(range, 0));
                        const Expression end = Expr(Child(range, 1));
                        const String name = CppIdentifier(Node(variable).Text);
                        const String unique = std::to_string(m_temporary++);
                        const String first = "jbro_gen_range_first_" + unique;
                        const String last = "jbro_gen_range_last_" + unique;
                        const auto saved = m_variables;
                        m_variables[String(Node(variable).Text)] = Type{"Int", false};
                        m_code += pad + "{\n" + pad + "    const JBro::Int " + first + " = (" + start.code + ");\n"
                            + pad + "    const JBro::Int " + last + " = (" + end.code + ");\n"
                            + pad + "    for (JBro::Int " + name + " = " + first + "; " + name
                            + " < " + last + "; ++" + name + ")\n";
                        Statement(Child(index, 3), indent + 4);
                        m_code += pad + "}\n";
                        m_variables = saved;
                        return;
                    }
                    const Expression iterable = Expr(Child(index, 2));
                    const Array<String> arguments = TypeArguments(iterable.type.name);
                    if (Child(index, 1) != InvalidNode)
                    {
                        if (iterable.type.name.rfind("Table<", 0) != 0 || arguments.Size() != 2)
                        {
                            Error("two-variable C++ emitter for requires a Table<K, V>");
                            return;
                        }
                        const NodeIndex keyVariable = Child(index, 0);
                        const NodeIndex valueVariable = Child(index, 1);
                        const String key = CppIdentifier(Node(keyVariable).Text);
                        const String value = CppIdentifier(Node(valueVariable).Text);
                        const String entry = "jbro_gen_table_entry_" + std::to_string(m_temporary++);
                        const auto saved = m_variables;
                        m_variables[String(Node(keyVariable).Text)] = TextType(arguments[0]);
                        m_variables[String(Node(valueVariable).Text)] = TextType(arguments[1]);
                        m_code += pad + "for (auto&& " + entry + " : " + iterable.code + ")\n"
                            + pad + "{\n"
                            + pad + "    " + String((Node(keyVariable).Flags & SyntaxFlagRef) ? "const auto& " : "auto ")
                            + key + " = " + entry + ".KeyValue;\n"
                            + pad + "    " + String((Node(valueVariable).Flags & SyntaxFlagRef) ? "auto& " : "auto ")
                            + value + " = " + entry + ".MappedValue;\n";
                        Statement(Child(index, 3), indent + 4);
                        m_code += pad + "}\n";
                        m_variables = saved;
                        return;
                    }
                    if (iterable.type.name.rfind("Array<", 0) != 0 || arguments.Size() != 1)
                    {
                        Error("C++ emitter for requires an Array<T> iterable");
                        return;
                    }
                    const NodeIndex variable = Child(index, 0);
                    const String name = CppIdentifier(Node(variable).Text);
                    const auto saved = m_variables;
                    m_variables[String(Node(variable).Text)] = TextType(arguments[0]);
                    m_code += pad + "for (" + String((Node(variable).Flags & SyntaxFlagRef) ? "auto& " : "auto ")
                        + name + " : " + iterable.code + ")\n";
                    Statement(Child(index, 3), indent);
                    m_variables = saved;
                    return;
                }
                if (node.Kind == SyntaxKind::SwitchStatement)
                {
                    const NodeIndex valueIndex = Child(index, 0);
                    const Expression value = Expr(valueIndex);
                    const DeclarationRef* switchedType = m_declarations.Find(value.type.name);
                    if (value.type.name != "Int" && value.type.name != "Float"
                        && value.type.name != "Bool" && value.type.name != "String"
                        && (switchedType == nullptr || switchedType->kind != SyntaxKind::EnumDeclaration))
                    {
                        Error("C++ emitter switch requires a comparable scalar or script enum");
                        return;
                    }
                    const String compared = AdaptValue({value.type.name, false}, value, valueIndex);
                    const String suffix = std::to_string(m_temporary++);
                    const String switchValue = "jbro_gen_switch_value_" + suffix;
                    const String branch = "jbro_gen_switch_branch_" + suffix;
                    m_code += pad + "{\n" + pad + "    const auto " + switchValue + " = (" + compared + ");\n"
                        + pad + "    JBro::Int64 " + branch + " = -1;\n";
                    Bool firstCase = true;
                    for (std::size_t clauseSlot = 1; clauseSlot < node.ChildCount; ++clauseSlot)
                    {
                        const NodeIndex clause = Child(index, clauseSlot);
                        const SyntaxNode& clauseNode = Node(clause);
                        if (clauseNode.Kind != SyntaxKind::CaseClause)
                        {
                            continue;
                        }
                        for (std::size_t caseSlot = 0; caseSlot + 1 < clauseNode.ChildCount; ++caseSlot)
                        {
                            m_code += pad + "    " + String(firstCase ? "if (" : "else if (")
                                + switchValue + " == (" + Expr(Child(clause, caseSlot)).code + "))\n"
                                + pad + "    {\n" + pad + "        " + branch + " = "
                                + std::to_string(clauseSlot - 1) + ";\n" + pad + "    }\n";
                            firstCase = false;
                        }
                    }
                    m_code += pad + "    switch (" + branch + ")\n" + pad + "    {\n";
                    for (std::size_t clauseSlot = 1; clauseSlot < node.ChildCount; ++clauseSlot)
                    {
                        const NodeIndex clause = Child(index, clauseSlot);
                        const SyntaxNode& clauseNode = Node(clause);
                        if (clauseNode.Kind == SyntaxKind::CaseClause)
                        {
                            m_code += pad + "    case " + std::to_string(clauseSlot - 1) + ":\n";
                        }
                        else if (clauseNode.Kind == SyntaxKind::DefaultClause)
                        {
                            m_code += pad + "    default:\n";
                        }
                        else
                        {
                            Error("C++ emitter encountered an invalid switch clause");
                            return;
                        }
                        Statement(Child(clause, clauseNode.ChildCount - 1), indent + 4);
                        m_code += pad + "        break;\n";
                    }
                    m_code += pad + "    }\n" + pad + "}\n";
                    return;
                }
                if (node.Kind == SyntaxKind::BreakStatement || node.Kind == SyntaxKind::ContinueStatement)
                {
                    m_code += pad + (node.Kind == SyntaxKind::BreakStatement ? "break;\n" : "continue;\n");
                    return;
                }
                if (node.Kind == SyntaxKind::ExpressionStatement)
                {
                    m_code += pad + Expr(Child(index, 0)).code + ";\n";
                    return;
                }
                if (node.Kind == SyntaxKind::ReturnStatement)
                {
                    m_code += pad + "return";
                    if (Child(index, 0) != InvalidNode)
                    {
                        const NodeIndex valueIndex = Child(index, 0);
                        m_code += " " + AdaptValue(m_returnType, Expr(valueIndex), valueIndex);
                    }
                    m_code += ";\n";
                    return;
                }
                Error("C++ emitter does not support this statement yet");
            }

            ArrayView<const CppSource> m_sources;
            const SourceText* m_source = nullptr;
            const SyntaxTree* m_tree = nullptr;
            NodeIndex m_errorNode = InvalidNode;
            const ApiCatalog& m_api;
            Bool m_emitModuleEntry;
            const AnalysisResult* m_analysis;
            const CppSource* m_unit;
            String m_implementation;
            Array<Internal::CppDeclarationPart> m_parts;
            String m_code;
            Array<String> m_errors;
            Table<String, DeclarationRef> m_declarations;
            Table<String, Int32> m_declarationState;
            Table<String, Table<String, Type>> m_fields;
            Table<String, VariableBinding> m_variables;
            Array<String> m_scripts;
            Array<FunctionRef> m_functions;
            Array<DeclarationRef> m_initializationOwners;
            Array<DeclarationRef> m_defaultConstructors;
            Bool m_fieldInitializer = false;
            Bool m_staticInitializer = false;
            std::size_t m_temporary = 0;
            String m_owner;
            Type m_returnType;
        };
    }

    CppEmitResult EmitCpp(const SourceText& source, const SyntaxTree& tree,
        const ApiCatalog& api, Bool emitModuleEntry, const AnalysisResult* analysis)
    {
        const CppSource one{&source, &tree};
        return Generator(ArrayView<const CppSource>(&one, 1), api, emitModuleEntry, analysis).Run();
    }

    CppEmitResult EmitCpp(ArrayView<const CppSource> sources, const ApiCatalog& api,
        Bool emitModuleEntry, const AnalysisResult* analysis)
    {
        return Generator(sources, api, emitModuleEntry, analysis).Run();
    }

    Internal::CppUnitParts Internal::EmitCppParts(ArrayView<const CppSource> sources, const ApiCatalog& api,
        const CppSource& unit, Bool emitModuleEntry, const AnalysisResult* analysis)
    {
        return Generator(sources, api, emitModuleEntry, analysis, &unit).RunParts();
    }
}
