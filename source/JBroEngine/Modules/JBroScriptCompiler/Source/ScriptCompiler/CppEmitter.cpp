#include <JBro/ScriptCompiler/CppEmitter.h>
#include <JBro/Types/Table.h>

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
            bool ref = false;
        };

        struct Expression
        {
            String code;
            Type type;
            bool lvalue = false;
            bool property = false;
            String setter;
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
            while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
            while (!value.empty() && value.back() == ' ') value.remove_suffix(1);
            return String(value);
        }

        Type TextType(std::string_view value)
        {
            if (value.rfind("ref ", 0) == 0)
            {
                return {Trim(value.substr(4)), true};
            }
            return {Trim(value), false};
        }

        Array<String> TypeArguments(std::string_view name)
        {
            const std::size_t open = name.find('<');
            if (open == std::string_view::npos || name.back() != '>') return {};
            name = name.substr(open + 1, name.size() - open - 2);
            Array<String> result;
            int depth = 0;
            std::size_t start = 0;
            for (std::size_t at = 0; at < name.size(); ++at)
            {
                if (name[at] == '<') ++depth;
                else if (name[at] == '>') --depth;
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
                bool emitModuleEntry)
                : m_sources(sources), m_api(api), m_emitModuleEntry(emitModuleEntry)
            {
            }

            CppEmitResult Run()
            {
                m_code = "#include <JBro/ScriptAPI.h>\n#include <JBro/Framework2D/Scripting/ScriptModule.h>\n"
                    "#include <JBro/Runtime/ScriptGuard.h>\n"
                    "#include <JBro/Reflection/EnumDescriptor.h>\n\n";
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
                Array<DeclarationRef> declarations;
                for (const CppSource& source : m_sources)
                {
                    m_source = source.Source;
                    m_tree = source.Tree;
                    for (NodeIndex child : m_tree->GetChildren(m_tree->GetRoot()))
                    {
                        if (child != InvalidNode
                            && !OrderDeclaration(String(Node(child).Text), declarations)) break;
                    }
                }
                if (false == m_errors.IsEmpty())
                {
                    return Finish();
                }
                for (const DeclarationRef& item : declarations)
                {
                    const SyntaxNode& declaration = item.tree->Get(item.index);
                    m_code += String(declaration.Kind == SyntaxKind::EnumDeclaration ? "enum class "
                        : declaration.Kind == SyntaxKind::StructDeclaration ? "struct " : "class ")
                        + String(declaration.Text)
                        + (declaration.Kind == SyntaxKind::EnumDeclaration ? " : std::int64_t;\n" : ";\n");
                }
                m_code += '\n';
                for (const DeclarationRef& item : declarations)
                {
                    const SyntaxNode& declaration = item.tree->Get(item.index);
                    if (declaration.Kind == SyntaxKind::ScriptDeclaration)
                    {
                        const String name(declaration.Text);
                        m_code += "template<> struct JBro::RefCategoryOf<" + name + ">\n"
                            "{\n    static constexpr JBro::RefCategory value = JBro::RefCategory::Script;\n};\n";
                    }
                }
                m_code += '\n';
                for (const DeclarationRef& item : declarations)
                {
                    m_source = item.source;
                    m_tree = item.tree;
                    Declaration(item.index);
                }
                for (const FunctionRef& item : m_functions)
                {
                    m_source = item.owner.source;
                    m_tree = item.owner.tree;
                    m_owner = String(Node(item.owner.index).Text);
                    Function(item.index, item.owner.kind == SyntaxKind::ScriptDeclaration, false);
                }
                for (const String& script : m_scripts)
                {
                    m_code += "JBRO_REGISTER_SCRIPT_2D(" + script + ");\n";
                }
                if (!m_scripts.IsEmpty())
                {
                    m_code += "namespace { const bool JBroGeneratedScript2DMarker = "
                        "JBro::Internal::MarkGeneratedScript2D(); }\n";
                    if (m_emitModuleEntry)
                    {
                        m_code += "JBRO_SCRIPT_MODULE_2D_REQUIRING_ERROR_CONTEXT()\n";
                    }
                }
                else
                {
                    Error("a game DLL needs at least one script declaration");
                }
                return Finish();
            }

        private:
            const SyntaxNode& Node(NodeIndex index) const { return m_tree->Get(index); }
            NodeIndex Child(NodeIndex index, std::size_t slot) const { return m_tree->GetChild(index, slot); }

            CppEmitResult Finish()
            {
                CppEmitResult result;
                result.Code = m_code;
                result.Errors = std::move(m_errors);
                return result;
            }

            void Error(std::string_view message)
            {
                m_errors.Add(String(message));
            }

            Type ParseType(NodeIndex index) const
            {
                if (index == InvalidNode)
                {
                    return {"Void", false};
                }
                const SyntaxNode& node = Node(index);
                Type result{String(node.Text), (node.Flags & SyntaxFlagRef) != 0};
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
                if (generic != std::string_view::npos) name = name.substr(0, generic);
                for (const ApiType& type : m_api.Types)
                {
                    if (type.Name.View() == name)
                    {
                        return &type;
                    }
                }
                return nullptr;
            }

            bool IsHandle(const Type& type) const
            {
                const ApiType* api = Api(type.name);
                return api != nullptr && api->Kind == ApiTypeKind::Handle;
            }

            bool IsScript(const Type& type) const
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
                        if (index != 0) native += ", ";
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

            void IndexDeclarations(NodeIndex root)
            {
                for (NodeIndex child : m_tree->GetChildren(root))
                {
                    const SyntaxNode& node = Node(child);
                    if (node.Kind != SyntaxKind::ScriptDeclaration
                        && node.Kind != SyntaxKind::ClassDeclaration
                        && node.Kind != SyntaxKind::StructDeclaration
                        && node.Kind != SyntaxKind::EnumDeclaration)
                    {
                        continue;
                    }
                    const String name(node.Text);
                    if (m_declarations.Contains(name))
                    {
                        Error("duplicate C++ declaration across files: " + name);
                        continue;
                    }
                    m_declarations[name] = {m_source, m_tree, child, node.Kind};
                    auto& fields = m_fields[name];
                    for (NodeIndex member : m_tree->GetChildren(child))
                    {
                        if (member != InvalidNode && Node(member).Kind == SyntaxKind::FieldDeclaration)
                        {
                            fields[String(Node(member).Text)] = ParseType(Child(member, 1));
                        }
                    }
                }
            }

            bool OrderDeclaration(const String& name, Array<DeclarationRef>& ordered)
            {
                const DeclarationRef* found = m_declarations.Find(name);
                if (found == nullptr)
                {
                    Error("C++ emitter does not support this declaration yet");
                    return false;
                }
                int& state = m_declarationState[name];
                if (state == 2) return true;
                if (state == 1)
                {
                    Error("owned field cycle cannot be represented in C++: " + name);
                    return false;
                }
                state = 1;
                for (const auto& entry : m_fields[name])
                {
                    const Type& type = entry.MappedValue;
                    if (m_declarations.Contains(type.name) && !IsScript(type)
                        && !OrderDeclaration(type.name, ordered))
                    {
                        return false;
                    }
                }
                state = 2;
                ordered.Add(*found);
                return true;
            }

            void Declaration(NodeIndex index)
            {
                if (index == InvalidNode)
                {
                    return;
                }
                const SyntaxNode& node = Node(index);
                if (node.Kind != SyntaxKind::ScriptDeclaration
                    && node.Kind != SyntaxKind::ClassDeclaration
                    && node.Kind != SyntaxKind::StructDeclaration
                    && node.Kind != SyntaxKind::EnumDeclaration)
                {
                    Error("C++ emitter does not support this declaration yet");
                    return;
                }
                m_owner = String(node.Text);
                if (node.Kind == SyntaxKind::EnumDeclaration)
                {
                    if (node.ChildCount == 0)
                    {
                        Error("C++ emitter cannot reflect an enum without members: " + m_owner);
                        return;
                    }
                    m_code += "enum class " + m_owner + " : std::int64_t\n{\n";
                    std::size_t memberCount = 0;
                    for (NodeIndex member : m_tree->GetChildren(index))
                    {
                        if (memberCount++ != 0) m_code += ",\n";
                        m_code += "    " + String(Node(member).Text);
                    }
                    m_code += "\n};\nnamespace JBro { JBRO_DEFINE_ENUM_TYPE(::" + m_owner
                        + ", " + Escape(m_owner) + ", ";
                    memberCount = 0;
                    for (NodeIndex member : m_tree->GetChildren(index))
                    {
                        if (memberCount++ != 0) m_code += ", ";
                        const String name(Node(member).Text);
                        m_code += "{::" + m_owner + "::" + name + ", " + Escape(name) + "}";
                    }
                    m_code += "); }\n\n";
                    return;
                }
                const bool script = node.Kind == SyntaxKind::ScriptDeclaration;
                if (script)
                {
                    m_scripts.Add(m_owner);
                }
                m_code += String(node.Kind == SyntaxKind::StructDeclaration ? "struct " : "class ")
                    + m_owner + (script ? " : public JBro::GameScript2D\n" : "\n") + "{\npublic:\n";
                if (script)
                {
                    m_code += "    JBRO_SCRIPT_BODY(" + m_owner + ")\n";
                }
                else
                {
                    m_code += "    JBRO_REFLECT_BODY(" + m_owner + ")\n";
                }
                for (NodeIndex member : m_tree->GetChildren(index))
                {
                    if (member == InvalidNode)
                    {
                        continue;
                    }
                    if (Node(member).Kind == SyntaxKind::FieldDeclaration)
                    {
                        MemberAccess(member, node.Kind);
                        Field(member, node.Kind == SyntaxKind::StructDeclaration);
                    }
                    else if (Node(member).Kind == SyntaxKind::FunctionDeclaration)
                    {
                        MemberAccess(member, node.Kind);
                        Function(member, script, true);
                        m_functions.Add({{m_source, m_tree, index, node.Kind}, member});
                    }
                    else if (Node(member).Kind == SyntaxKind::BaseList)
                    {
                        if (Node(member).ChildCount != 0)
                        {
                            Error("C++ emitter does not support user-declared base types yet");
                        }
                    }
                    else
                    {
                        Error("C++ emitter does not support this member yet");
                    }
                }
                m_code += "};\n\n";
                if (!script)
                {
                    m_code += "template<> struct JBro::TypeDescriptorOf<" + m_owner + ">\n{\n"
                        "    static const JBro::TypeDescriptor& Get()\n    {\n"
                        "        static const JBro::TypeDescriptor descriptor = "
                        "JBro::MakeStructTypeDescriptor<" + m_owner + ">(" + Escape(m_owner)
                        + ", JBro::GetPropertyTable<" + m_owner + ">());\n"
                        "        return descriptor;\n    }\n};\n\n";
                }
            }

            void MemberAccess(NodeIndex index, SyntaxKind ownerKind)
            {
                const std::uint16_t flags = Node(index).Flags;
                const bool publicDefault = ownerKind == SyntaxKind::StructDeclaration;
                const char* access = (flags & SyntaxFlagPublic) ? "public"
                    : (flags & SyntaxFlagProtected) ? "protected"
                    : (flags & SyntaxFlagPrivate) ? "private"
                    : publicDefault ? "public" : "private";
                m_code += "    " + String(access) + ":\n";
            }

            void Field(NodeIndex index, bool isStruct)
            {
                const SyntaxNode& node = Node(index);
                if (node.Flags & (SyntaxFlagStatic | SyntaxFlagConst))
                {
                    Error("C++ emitter does not support static or const fields yet");
                }
                if (const NodeIndex typeNode = Child(index, 1); typeNode != InvalidNode
                    && (Node(typeNode).Flags & SyntaxFlagConst))
                {
                    Error("C++ emitter does not support const field types yet");
                }
                const Type type = ParseType(Child(index, 1));
                const String native = NativeType(type);
                const String name(node.Text);
                const NodeIndex attributes = Child(index, 0);
                const bool reflected = isStruct || (attributes != InvalidNode && Node(attributes).ChildCount != 0);
                if (attributes != InvalidNode)
                {
                    for (NodeIndex attribute : m_tree->GetChildren(attributes))
                    {
                        if (attribute == InvalidNode || Node(attribute).Text != "prop"
                            || Node(attribute).ChildCount != 0)
                        {
                            Error("C++ emitter supports only the parameterless prop field attribute yet");
                        }
                    }
                }
                if (reflected && (node.Flags & SyntaxFlagProtected))
                {
                    Error("C++ emitter does not support protected reflected fields yet");
                }
                if (reflected)
                {
                    const bool privateField = (node.Flags & SyntaxFlagPrivate)
                        || (!isStruct && 0 == (node.Flags & SyntaxFlagPublic));
                    m_code += "    " + String(privateField ? "JBRO_FIELD_PRIVATE(" : "JBRO_FIELD(")
                        + native + ", " + name + ")";
                }
                else
                {
                    m_code += "    " + native + " " + name;
                }
                if (Child(index, 2) != InvalidNode)
                {
                    const Expression initializer = Expr(Child(index, 2));
                    m_code += " = " + (initializer.type.name == "Null" ? "{}"
                        : AdaptValue(type, initializer, Child(index, 2)));
                }
                else
                {
                    m_code += " = {}";
                }
                m_code += ";\n";
            }

            void Function(NodeIndex index, bool script, bool declaration)
            {
                const SyntaxNode& node = Node(index);
                if (node.Flags & (SyntaxFlagStatic | SyntaxFlagRequire))
                {
                    Error("C++ emitter does not support static or abstract methods yet");
                }
                if (node.Text == m_owner)
                {
                    Error("C++ emitter does not support constructors yet");
                }
                const Type returns = ParseType(Child(index, 2));
                const bool callback = script && (node.Flags & (SyntaxFlagCallback | SyntaxFlagOverride)) != 0;
                m_variables = m_fields[m_owner];
                m_code += (declaration ? "    " : "") + NativeType(returns) + " "
                    + (declaration ? "" : m_owner + "::") + String(node.Text) + "(";
                const NodeIndex parameters = Child(index, 1);
                if (parameters != InvalidNode)
                {
                    std::size_t count = 0;
                    for (NodeIndex parameter : m_tree->GetChildren(parameters))
                    {
                        if (count++ != 0) m_code += ", ";
                        const Type type = ParseType(Child(parameter, 0));
                        m_variables[String(Node(parameter).Text)] = type.ref
                            ? Type{type.name, false} : type;
                        m_code += (type.ref ? NativeType({type.name, false}) + "&" : NativeType(type))
                            + " " + String(Node(parameter).Text);
                    }
                }
                m_code += ")";
                if (declaration && node.Flags & (SyntaxFlagCallback | SyntaxFlagOverride)) m_code += " override";
                const NodeIndex body = Child(index, 3);
                if (body == InvalidNode)
                {
                    if (!declaration) Error("C++ emitter does not support methods without a body yet");
                    m_code += ";\n";
                    return;
                }
                if (declaration)
                {
                    m_code += ";\n";
                    return;
                }
                m_code += "\n{\n";
                if (callback) m_code += "    try\n    {\n";
                for (NodeIndex statement : m_tree->GetChildren(body))
                {
                    Statement(statement, callback ? 8 : 4);
                }
                if (callback)
                {
                    m_code += "    }\n    catch (const JBro::ScriptGuard::AbortCallback&)\n    {\n        return;\n    }\n";
                }
                m_code += "}\n\n";
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
                if (index == InvalidNode) return "<expression>";
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

            String AdaptValue(const Type& target, const Expression& value, NodeIndex sourceIndex) const
            {
                if (!target.ref && value.type.ref && target.name == value.type.name
                    && !IsHandle(value.type) && !IsScript(value.type))
                {
                    const String site = SiteKey(Node(sourceIndex));
                    return "([&]() -> decltype(auto) { auto&& __jb_value = (" + value.code
                        + "); return JBro::ScriptGuard::Require(__jb_value, " + Escape(site)
                        + ", " + Escape(site + " invalid ref value: " + AccessPath(sourceIndex)) + "); }())";
                }
                return value.code;
            }

            Expression Expr(NodeIndex index)
            {
                if (index == InvalidNode) return {};
                const SyntaxNode& node = Node(index);
                switch (node.Kind)
                {
                case SyntaxKind::NameExpression:
                {
                    const String name(node.Text);
                    if (name == "this") return {"(*this)", {m_owner, false}, true};
                    const Type* found = m_variables.Find(name);
                    if (found != nullptr)
                    {
                        return {name, *found, true};
                    }
                    if (const DeclarationRef* owner = m_declarations.Find(m_owner))
                    {
                        for (NodeIndex member : owner->tree->GetChildren(owner->index))
                        {
                            if (member == InvalidNode) continue;
                            const SyntaxNode& item = owner->tree->Get(member);
                            if (item.Kind != SyntaxKind::FunctionDeclaration || item.Text != name)
                            {
                                continue;
                            }
                            const SyntaxTree* saved = m_tree;
                            m_tree = owner->tree;
                            const Type result = ParseType(Child(member, 2));
                            m_tree = saved;
                            return {name, result};
                        }
                    }
                    if (Api(name) != nullptr || m_declarations.Contains(name))
                    {
                        return {NativeType({name, false}), {name, false}};
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
                    return {NativeType(type), type};
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
                    if (const DeclarationRef* declaration = m_declarations.Find(receiver.type.name);
                        declaration != nullptr && declaration->kind == SyntaxKind::EnumDeclaration)
                    {
                        for (NodeIndex member : declaration->tree->GetChildren(declaration->index))
                        {
                            if (declaration->tree->Get(member).Text == node.Text)
                            {
                                return {receiver.code + "::" + String(node.Text), receiver.type};
                            }
                        }
                        Error("unresolved enum member in C++ emitter: " + String(node.Text));
                        return {};
                    }
                    Type memberType;
                    bool property = false;
                    String nativeMember(node.Text);
                    String setter;
                    if (const auto* fields = m_fields.Find(receiver.type.name); fields != nullptr)
                    {
                        if (const Type* field = fields->Find(String(node.Text)); field != nullptr)
                        {
                            memberType = *field;
                        }
                    }
                    if (const DeclarationRef* declaration = m_declarations.Find(receiver.type.name))
                    {
                        for (NodeIndex member : declaration->tree->GetChildren(declaration->index))
                        {
                            if (member == InvalidNode) continue;
                            const SyntaxNode& item = declaration->tree->Get(member);
                            if (item.Kind != SyntaxKind::FunctionDeclaration || item.Text != node.Text)
                            {
                                continue;
                            }
                            const SyntaxTree* saved = m_tree;
                            m_tree = declaration->tree;
                            memberType = ParseType(Child(member, 2));
                            m_tree = saved;
                            break;
                        }
                    }
                    if (const ApiType* type = Api(receiver.type.name))
                    {
                        for (const ApiMember& member : type->Members)
                        {
                            if (member.Name.View() != node.Text) continue;
                            const String declared(member.Type.c_str());
                            memberType = TextType(declared);
                            if (memberType.name == "T" || memberType.name == "V")
                            {
                                const Array<String> arguments = TypeArguments(receiver.type.name);
                                const std::size_t slot = memberType.name == "V" ? 1 : 0;
                                if (slot < arguments.Size()) memberType.name = TextType(arguments[slot]).name;
                            }
                            if (memberType.name == "T" && node.ChildCount > 1)
                            {
                                memberType = ParseType(Child(index, 1));
                                memberType.ref = TextType(declared).ref;
                            }
                            nativeMember = member.NativeName.empty() ? Pascal(node.Text) : member.NativeName.c_str();
                            property = type->Kind == ApiTypeKind::Handle
                                && member.Kind == ApiMemberKind::Field;
                            if (member.Writable && nativeMember.rfind("Get", 0) == 0)
                            {
                                setter = "Set" + nativeMember.substr(3);
                            }
                            else if (member.Writable && nativeMember.rfind("Is", 0) == 0)
                            {
                                setter = "Set" + nativeMember.substr(2);
                            }
                            break;
                        }
                    }
                    if (memberType.name.empty())
                    {
                        Error("unresolved member in C++ emitter: " + String(node.Text));
                    }
                    const String access = property ? "." + nativeMember + "()" : "." + nativeMember;
                    if (node.Flags & SyntaxFlagNullSafe)
                    {
                        if (memberType.name.empty()) return {};
                        const String bare = NativeType({memberType.name, false});
                        const bool alreadyNullable = memberType.ref || IsHandle(memberType);
                        const String resultType = alreadyNullable
                            ? NativeType(memberType) : "JBro::Nullable<" + bare + ">";
                        const String code = "([&]() -> " + resultType + " { auto&& __jb_receiver = ("
                            + receiver.code + "); if (!JBro::ScriptGuard::IsPresent(__jb_receiver)) return {}; "
                            "return " + (alreadyNullable ? "" : "JBro::Nullable<" + bare + ">(")
                            + "JBro::ScriptGuard::UnwrapPresent(__jb_receiver)" + access
                            + (alreadyNullable ? "" : ")") + "; }())";
                        memberType.ref = true;
                        return {code, memberType};
                    }
                    if (receiver.type.ref || IsHandle(receiver.type))
                    {
                        const String key = Escape(SiteKey(node));
                        const String message = Escape(SiteKey(node) + " invalid ref access: " + AccessPath(index));
                        const String code = "([&]() -> decltype(auto) { auto&& __jb_receiver = ("
                            + receiver.code + "); return JBro::ScriptGuard::Require(__jb_receiver, "
                            + key + ", " + message + ")" + access + "; }())";
                        return {code, memberType, !property, property, setter};
                    }
                    return {"(" + receiver.code + ")" + access, memberType, !property, property, setter};
                }
                case SyntaxKind::NullAssertExpression:
                {
                    Expression receiver = Expr(Child(index, 0));
                    if (!receiver.type.ref && !IsHandle(receiver.type)) return receiver;
                    const String key = Escape(SiteKey(node));
                    const String message = Escape(SiteKey(node) + " invalid ref assertion: " + AccessPath(index));
                    receiver.code = "([&]() -> decltype(auto) { auto&& __jb_receiver = (" + receiver.code
                        + "); return JBro::ScriptGuard::Require(__jb_receiver, " + key + ", " + message + "); }())";
                    receiver.type.ref = false;
                    return receiver;
                }
                case SyntaxKind::IsNullExpression:
                {
                    const Expression value = Expr(Child(index, 0));
                    const bool negated = (node.Flags & SyntaxFlagNegated) != 0;
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
                        if (left.type.name == "Null") return right;
                        const String bare = NativeType({left.type.name, false});
                        const String code = "([&]() -> " + bare + " { auto&& __jb_left = (" + left.code
                            + "); if (JBro::ScriptGuard::IsPresent(__jb_left)) return "
                            "JBro::ScriptGuard::UnwrapPresent(__jb_left); return " + bare
                            + "(" + right.code + "); }())";
                        return {code, {left.type.name, false}};
                    }
                    const String op = node.Text == "and" ? "&&" : node.Text == "or" ? "||" : String(node.Text);
                    return {"(" + left.code + " " + op + " " + right.code + ")", left.type};
                }
                case SyntaxKind::CallExpression:
                {
                    const NodeIndex calleeIndex = Child(index, 0);
                    const Expression callee = Expr(calleeIndex);
                    const SyntaxNode& calleeNode = Node(calleeIndex);
                    String arguments;
                    for (std::size_t slot = 1; slot < node.ChildCount; ++slot)
                    {
                        if (slot != 1) arguments += ", ";
                        arguments += Expr(Child(index, slot)).code;
                    }
                    if (calleeNode.Kind == SyntaxKind::MemberAccessExpression)
                    {
                        const Expression receiver = Expr(Child(calleeIndex, 0));
                        String method(calleeNode.Text);
                        if (const ApiType* type = Api(receiver.type.name))
                        {
                            for (const ApiMember& member : type->Members)
                            {
                                if (member.Name.View() == calleeNode.Text)
                                {
                                    method = member.NativeName.empty() ? Pascal(calleeNode.Text)
                                        : String(member.NativeName.c_str());
                                    break;
                                }
                            }
                        }
                        for (std::size_t slot = 1; slot < calleeNode.ChildCount; ++slot)
                        {
                            method += slot == 1 ? "<" : ", ";
                            method += NativeType(ParseType(Child(calleeIndex, slot)));
                        }
                        if (calleeNode.ChildCount > 1) method += '>';
                        if (calleeNode.Flags & SyntaxFlagNullSafe)
                        {
                            if (callee.type.name == "Void")
                            {
                                const String code = "([&]() { auto&& __jb_receiver = ("
                                    + receiver.code + "); if (!JBro::ScriptGuard::IsPresent(__jb_receiver)) "
                                    "return; JBro::ScriptGuard::UnwrapPresent(__jb_receiver)."
                                    + method + "(" + arguments + "); }())";
                                return {code, callee.type};
                            }
                            const String resultType = NativeType(callee.type);
                            const String code = "([&]() -> " + resultType + " { auto&& __jb_receiver = ("
                                + receiver.code + "); if (!JBro::ScriptGuard::IsPresent(__jb_receiver)) "
                                "return {}; return " + resultType + "(JBro::ScriptGuard::UnwrapPresent(__jb_receiver)."
                                + method + "(" + arguments + ")); }())";
                            return {code, callee.type};
                        }
                        if (receiver.type.ref || IsHandle(receiver.type))
                        {
                            const String site = SiteKey(calleeNode);
                            const String code = "([&]() -> decltype(auto) { auto&& __jb_receiver = ("
                                + receiver.code + "); return JBro::ScriptGuard::Require(__jb_receiver, "
                                + Escape(site) + ", " + Escape(site + " invalid ref access: "
                                    + AccessPath(calleeIndex)) + ")." + method + "(" + arguments + "); }())";
                            return {code, callee.type};
                        }
                        return {"(" + receiver.code + ")." + method + "(" + arguments + ")", callee.type};
                    }
                    return {callee.code + "(" + arguments + ")", callee.type};
                }
                case SyntaxKind::RefArgument:
                {
                    const NodeIndex source = Child(index, 0);
                    Expression borrowed = Expr(source);
                    if (borrowed.type.ref)
                    {
                        const String site = SiteKey(Node(source));
                        borrowed.code = "([&]() -> decltype(auto) { auto&& __jb_borrowed = ("
                            + borrowed.code + "); return JBro::ScriptGuard::Require(__jb_borrowed, "
                            + Escape(site) + ", " + Escape(site + " invalid borrowed ref: " + AccessPath(source)) + "); }())";
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
                    const bool table = receiver.type.name.rfind("Table<", 0) == 0;
                    const String nativeKey = table && arguments.Size() == 2
                        ? NativeType(TextType(arguments[0])) + "(" + key.code + ")"
                        : "static_cast<std::size_t>(" + key.code + ")";
                    if (receiver.type.ref)
                    {
                        const String site = SiteKey(node);
                        const String code = "([&]() -> decltype(auto) { auto&& __jb_receiver = ("
                            + receiver.code + "); auto&& __jb_checked = JBro::ScriptGuard::Require(__jb_receiver, "
                            + Escape(site) + ", " + Escape(site + " invalid ref index: " + AccessPath(index))
                            + "); return __jb_checked[" + nativeKey + "]; }())";
                        return {code, element, receiver.lvalue};
                    }
                    return {receiver.code + "[" + nativeKey + "]", element, true};
                }
                default:
                    Error("C++ emitter does not support this expression yet");
                    return {};
                }
            }

            void Statement(NodeIndex index, int indent)
            {
                const SyntaxNode& node = Node(index);
                const String pad(static_cast<std::size_t>(indent), ' ');
                m_code += "#line " + std::to_string(node.Range.Begin.Line.Get()) + " "
                    + Escape(m_source->GetPath().c_str()) + "\n";
                if (node.Kind == SyntaxKind::Block)
                {
                    const auto saved = m_variables;
                    m_code += pad + "{\n";
                    for (NodeIndex child : m_tree->GetChildren(index)) Statement(child, indent + 4);
                    m_code += pad + "}\n";
                    m_variables = saved;
                    return;
                }
                if (node.Kind == SyntaxKind::LocalDeclaration)
                {
                    const Type type = ParseType(Child(index, 0));
                    m_variables[String(node.Text)] = type;
                    m_code += pad + NativeType(type) + " " + String(node.Text);
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
                        m_code += pad + "([&]() { auto&& __jb_receiver = (" + receiver.code + "); "
                            "auto&& __jb_checked = JBro::ScriptGuard::Require(__jb_receiver, " + key + ", " + message + "); "
                            + (target.type.name == "String" ? "JBro::String __jb_value(" : "auto __jb_value = (")
                            + value.code + "); ";
                        if (node.Text != "=")
                        {
                            const String operation(node.Text);
                            m_code += NativeType(target.type) + " __jb_current(__jb_checked." + getter + "()); ";
                            if (target.type.name == "Vector2")
                            {
                                const String operand = operation.substr(0, operation.size() - 1);
                                const String rhs = value.type.name == "Vector2" ? "__jb_value.x" : "__jb_value";
                                m_code += "__jb_current.x " + operand + "= " + rhs + "; ";
                                m_code += "__jb_current.y " + operand + "= "
                                    + (value.type.name == "Vector2" ? "__jb_value.y" : "__jb_value") + "; ";
                            }
                            else
                            {
                                m_code += "__jb_current " + operation + " __jb_value; ";
                            }
                        }
                        const String assigned = node.Text == "=" ? "__jb_value" : "__jb_current";
                        m_code += "__jb_checked." + target.setter + "("
                            + assigned + (target.type.name == "String" ? ".c_str()" : "")
                            + "); }());\n";
                    }
                    else
                    {
                        m_code += pad + target.code + " " + String(node.Text) + " "
                            + AdaptValue(target.type, value, Child(index, 1)) + ";\n";
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
                        const String name(Node(variable).Text);
                        const String unique = std::to_string(m_temporary++);
                        const String first = "__jb_range_first_" + unique;
                        const String last = "__jb_range_last_" + unique;
                        const auto saved = m_variables;
                        m_variables[name] = {"Int", false};
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
                        const String key(Node(keyVariable).Text);
                        const String value(Node(valueVariable).Text);
                        const String entry = "__jb_table_entry_" + std::to_string(m_temporary++);
                        const auto saved = m_variables;
                        m_variables[key] = TextType(arguments[0]);
                        m_variables[value] = TextType(arguments[1]);
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
                    const String name(Node(variable).Text);
                    const auto saved = m_variables;
                    m_variables[name] = TextType(arguments[0]);
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
                    if (value.type.name != "Int" && (switchedType == nullptr
                        || switchedType->kind != SyntaxKind::EnumDeclaration))
                    {
                        Error("C++ emitter switch requires Int or a script enum");
                        return;
                    }
                    const String compared = AdaptValue({value.type.name, false}, value, valueIndex);
                    m_code += pad + "switch (static_cast<std::int64_t>(" + compared + "))\n"
                        + pad + "{\n";
                    for (std::size_t clauseSlot = 1; clauseSlot < node.ChildCount; ++clauseSlot)
                    {
                        const NodeIndex clause = Child(index, clauseSlot);
                        const SyntaxNode& clauseNode = Node(clause);
                        if (clauseNode.Kind == SyntaxKind::CaseClause)
                        {
                            for (std::size_t caseSlot = 0; caseSlot + 1 < clauseNode.ChildCount; ++caseSlot)
                            {
                                m_code += pad + "case static_cast<std::int64_t>("
                                    + Expr(Child(clause, caseSlot)).code + "):\n";
                            }
                        }
                        else if (clauseNode.Kind == SyntaxKind::DefaultClause)
                        {
                            m_code += pad + "default:\n";
                        }
                        else
                        {
                            Error("C++ emitter encountered an invalid switch clause");
                            return;
                        }
                        Statement(Child(clause, clauseNode.ChildCount - 1), indent + 4);
                        m_code += pad + "    break;\n";
                    }
                    m_code += pad + "}\n";
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
                    if (Child(index, 0) != InvalidNode) m_code += " " + Expr(Child(index, 0)).code;
                    m_code += ";\n";
                    return;
                }
                Error("C++ emitter does not support this statement yet");
            }

            ArrayView<const CppSource> m_sources;
            const SourceText* m_source = nullptr;
            const SyntaxTree* m_tree = nullptr;
            const ApiCatalog& m_api;
            bool m_emitModuleEntry;
            String m_code;
            Array<String> m_errors;
            Table<String, DeclarationRef> m_declarations;
            Table<String, int> m_declarationState;
            Table<String, Table<String, Type>> m_fields;
            Table<String, Type> m_variables;
            Array<String> m_scripts;
            Array<FunctionRef> m_functions;
            std::size_t m_temporary = 0;
            String m_owner;
        };
    }

    CppEmitResult EmitCpp(const SourceText& source, const SyntaxTree& tree,
        const ApiCatalog& api, bool emitModuleEntry)
    {
        const CppSource one{&source, &tree};
        return Generator(ArrayView<const CppSource>(&one, 1), api, emitModuleEntry).Run();
    }

    CppEmitResult EmitCpp(ArrayView<const CppSource> sources, const ApiCatalog& api,
        bool emitModuleEntry)
    {
        return Generator(sources, api, emitModuleEntry).Run();
    }
}
