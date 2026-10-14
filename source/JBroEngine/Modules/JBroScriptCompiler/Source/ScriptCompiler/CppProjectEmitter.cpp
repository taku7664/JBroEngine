#include <JBro/ScriptCompiler/Analysis.h>
#include <JBro/Types/Table.h>
#include "CppEmitterParts.h"
#include "CppIdentifier.h"

#include <algorithm>
#include <cstdint>
#include <string_view>

namespace JBro::ScriptCompiler
{
    namespace
    {
        struct Declaration
        {
            const CppSource* Source = nullptr;
            NodeIndex Index = InvalidNode;
            String Name;
            String File;
        };

        String FileId(std::string_view text)
        {
            std::uint64_t hash = 14695981039346656037ull;
            for (const unsigned char byte : text)
            {
                hash = (hash ^ byte) * 1099511628211ull;
            }
            String result(16, '0');
            constexpr char digits[] = "0123456789abcdef";
            for (std::size_t index = 16; index > 0; --index)
            {
                result[index - 1] = digits[hash & 15];
                hash >>= 4;
            }
            return result;
        }

        class ProjectEmitter final
        {
        public:
            ProjectEmitter(ArrayView<const CppSource> sources, const ApiCatalog& api,
                bool moduleEntry, const AnalysisResult* analysis)
                : m_sources(sources), m_api(api), m_moduleEntry(moduleEntry), m_analysis(analysis)
            {
            }

            CppProjectResult Run()
            {
                bool hasScript = false;
                String allHeaders = "#pragma once\n";
                for (const CppSource& source : m_sources)
                {
                    if (source.Source == nullptr || source.Tree == nullptr || source.Tree->GetRoot() == InvalidNode)
                    {
                        m_result.Errors.Add("empty syntax tree");
                        return std::move(m_result);
                    }
                    for (NodeIndex child : source.Tree->GetChildren(source.Tree->GetRoot()))
                    {
                        if (child == InvalidNode)
                        {
                            continue;
                        }
                        const SyntaxNode& node = source.Tree->Get(child);
                        const String name(node.Text);
                        if (m_declarations.Contains(name))
                        {
                            m_result.Errors.Add("duplicate project type: " + name);
                            return std::move(m_result);
                        }
                        m_declarations[name] = {&source, child, name, "jbro_gen_type_" + FileId(name.View())};
                        hasScript = hasScript || node.Kind == SyntaxKind::ScriptDeclaration;
                    }
                }
                if (m_moduleEntry && !hasScript)
                {
                    m_result.Errors.Add("a game DLL needs at least one script declaration");
                    return std::move(m_result);
                }
                for (const CppSource& source : m_sources)
                {
                    Internal::CppUnitParts parts = Internal::EmitCppParts(m_sources, m_api, source, false, m_analysis);
                    for (const String& error : parts.Errors)
                    {
                        m_result.Errors.Add(error);
                    }
                    if (!m_result.Errors.IsEmpty())
                    {
                        return std::move(m_result);
                    }
                    Array<String> sourceTypes;
                    Collect(source, source.Tree->GetRoot(), sourceTypes);
                    const String sourceFile = "jbro_gen_source_" + FileId(source.Source->GetPath().View());
                    String sourceIncludes;
                    String publicHeader = "#pragma once\n";
                    for (const Internal::CppDeclarationPart& part : parts.Declarations)
                    {
                        const Declaration& declaration = *m_declarations.Find(part.Name);
                        Add(sourceTypes, declaration.Name);
                        publicHeader += Include(declaration.File + ".h");
                        Array<String> used;
                        Array<String> complete;
                        Collect(source, declaration.Index, used);
                        const NodeIndex bases = source.Tree->GetChild(declaration.Index, 0);
                        if (bases != InvalidNode && source.Tree->Get(bases).Kind == SyntaxKind::BaseList)
                        {
                            Collect(source, bases, complete);
                        }
                        for (NodeIndex member : source.Tree->GetChildren(declaration.Index))
                        {
                            if (member == InvalidNode || source.Tree->Get(member).Kind != SyntaxKind::FieldDeclaration)
                            {
                                continue;
                            }
                            CollectValueType(source, source.Tree->GetChild(member, 1), complete);
                            // Inline member initializers need definitions for calls and enum members.
                            const NodeIndex initializer = source.Tree->GetChild(member, 2);
                            if (initializer != InvalidNode)
                            {
                                Collect(source, initializer, complete);
                            }
                        }
                        String header = "#pragma once\n" + Include(declaration.File + ".fwd.h");
                        Sort(used);
                        Sort(complete);
                        for (const String& name : used)
                        {
                            if (name != declaration.Name)
                            {
                                header += Include(m_declarations[name].File + ".fwd.h");
                            }
                        }
                        for (const String& name : complete)
                        {
                            if (name != declaration.Name)
                            {
                                header += Include(m_declarations[name].File + ".h");
                            }
                        }
                        Artifact(declaration.File + ".fwd.h", Forward(declaration));
                        Artifact(declaration.File + ".h", header + '\n' + part.Code);
                        allHeaders += Include(declaration.File + ".h");
                    }
                    Sort(sourceTypes);
                    for (const String& name : sourceTypes)
                    {
                        sourceIncludes += Include(m_declarations[name].File + ".h");
                    }
                    Artifact(sourceFile + ".h", publicHeader);
                    Artifact(sourceFile + ".cpp", sourceIncludes + '\n' + parts.Code);
                }
                if (m_moduleEntry)
                {
                    Artifact("jbro_gen_module.cpp", "#include <JBro/Framework2D/Scripting/ScriptModule.h>\n"
                        "JBRO_SCRIPT_MODULE_2D_REQUIRING_ERROR_CONTEXT()\n");
                }
                Artifact("JBroGenerated.h", allHeaders);
                return std::move(m_result);
            }

        private:
            static String Include(const String& file) { return "#include \"" + file + "\"\n"; }
            static void Sort(Array<String>& names) { std::sort(names.begin(), names.end()); }

            void Add(Array<String>& names, const String& name)
            {
                if (!m_declarations.Contains(name))
                {
                    return;
                }
                for (const String& present : names)
                {
                    if (present == name)
                    {
                        return;
                    }
                }
                names.Add(name);
            }

            void AddType(Array<String>& names, std::string_view type)
            {
                std::size_t start = 0;
                for (std::size_t at = 0; at <= type.size(); ++at)
                {
                    if (at == type.size() || type[at] == '<' || type[at] == '>' || type[at] == ',' || type[at] == ' ')
                    {
                        if (at > start)
                        {
                            Add(names, String(type.substr(start, at - start)));
                        }
                        start = at + 1;
                    }
                }
            }

            void Collect(const CppSource& source, NodeIndex index, Array<String>& names)
            {
                if (index == InvalidNode)
                {
                    return;
                }
                const DocumentAnalysis* document = nullptr;
                if (m_analysis != nullptr)
                {
                    for (const auto& item : m_analysis->Documents)
                    {
                        if (&item->Tree == source.Tree)
                        {
                            document = item.Get();
                            break;
                        }
                    }
                }
                Walk(source, index, names, document);
                if (document == nullptr)
                {
                    return;
                }
                const SourceRange& range = source.Tree->Get(index).Range;
                for (const Occurrence& occurrence : document->Occurrences)
                {
                    if (occurrence.Range.Begin.Offset < range.Begin.Offset || occurrence.Range.End.Offset > range.End.Offset)
                    {
                        continue;
                    }
                    const Symbol* symbol = FindSymbol(*m_analysis, occurrence.Target);
                    while (symbol != nullptr && symbol->Parent != InvalidSymbol)
                    {
                        symbol = FindSymbol(*m_analysis, symbol->Parent);
                    }
                    if (symbol != nullptr && !symbol->Api)
                    {
                        Add(names, symbol->Name);
                    }
                }
            }

            void Walk(const CppSource& source, NodeIndex index, Array<String>& names, const DocumentAnalysis* document)
            {
                if (index == InvalidNode)
                {
                    return;
                }
                const SyntaxNode& node = source.Tree->Get(index);
                if (node.Kind == SyntaxKind::TypeName || node.Kind == SyntaxKind::GenericNameExpression)
                {
                    Add(names, String(node.Text));
                }
                if (document != nullptr && node.Kind >= SyntaxKind::NameExpression)
                {
                    AddType(names, GetExpressionType(*m_analysis, *document, index).View());
                }
                else if (document == nullptr && node.Kind == SyntaxKind::NameExpression)
                {
                    Add(names, String(node.Text));
                }
                for (NodeIndex child : source.Tree->GetChildren(index))
                {
                    Walk(source, child, names, document);
                }
            }

            void CollectValueType(const CppSource& source, NodeIndex index, Array<String>& names)
            {
                if (index == InvalidNode)
                {
                    return;
                }
                const SyntaxNode& node = source.Tree->Get(index);
                const Declaration* declaration = m_declarations.Find(String(node.Text));
                if (declaration != nullptr && !(node.Flags & SyntaxFlagRef
                    && declaration->Source->Tree->Get(declaration->Index).Kind == SyntaxKind::ScriptDeclaration))
                {
                    Add(names, declaration->Name);
                }
                for (NodeIndex child : source.Tree->GetChildren(index))
                {
                    CollectValueType(source, child, names);
                }
            }

            String Forward(const Declaration& declaration)
            {
                const SyntaxKind kind = declaration.Source->Tree->Get(declaration.Index).Kind;
                const String name = CppIdentifier(declaration.Name);
                String code = "#pragma once\n#include <JBro/ScriptAPI.h>\n"
                    "#include <JBro/Reflection/EnumDescriptor.h>\n\n";
                code += String(kind == SyntaxKind::EnumDeclaration ? "enum class "
                    : kind == SyntaxKind::StructDeclaration ? "struct " : "class ") + name
                    + String(kind == SyntaxKind::EnumDeclaration ? " : std::int64_t;\n" : ";\n");
                if (kind == SyntaxKind::ScriptDeclaration)
                {
                    code += "template<> struct JBro::RefCategoryOf<" + name + ">\n"
                        "{ static constexpr JBro::RefCategory value = JBro::RefCategory::Script; };\n";
                }
                if (kind != SyntaxKind::InterfaceDeclaration && kind != SyntaxKind::EnumDeclaration)
                {
                    code += "template<> const JBro::PropertyTable& JBro::GetPropertyTable<" + name + ">();\n";
                }
                if (kind == SyntaxKind::ClassDeclaration || kind == SyntaxKind::StructDeclaration || kind == SyntaxKind::EnumDeclaration)
                {
                    code += "template<> struct JBro::TypeDescriptorOf<" + name + ">\n"
                        "{ static const JBro::TypeDescriptor& Get(); };\n";
                }
                return code;
            }

            void Artifact(const String& file, const String& code)
            {
                if (m_files.Contains(file))
                {
                    m_result.Errors.Add("generated file name collision: " + file);
                    return;
                }
                m_files[file] = true;
                m_result.Files.Add({file, code});
            }

            ArrayView<const CppSource> m_sources;
            const ApiCatalog& m_api;
            bool m_moduleEntry;
            const AnalysisResult* m_analysis;
            Table<String, Declaration> m_declarations;
            Table<String, bool> m_files;
            CppProjectResult m_result;
        };
    }

    CppProjectResult EmitCppProject(ArrayView<const CppSource> sources, const ApiCatalog& api,
        bool emitModuleEntry, const AnalysisResult* analysis)
    {
        return ProjectEmitter(sources, api, emitModuleEntry, analysis).Run();
    }
}
