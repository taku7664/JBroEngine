#include "ScriptSummaryPanel.h"
#include "CodeEditorPanel.h"
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorActionRegistry.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/Common.h>
#include <JBro/Editor/Widget/Fields.h>
#include <JBro/Editor/Widget/FieldLabel.h>
#include <JBro/Editor/Widget/FilterCombo.h>
#include <JBro/Editor/Widget/FormLayout.h>
#include <JBro/Editor/Widget/TextField.h>

namespace JBro
{
    namespace
    {
        struct AttributeInfo
        {
            const char* name;
            const char* label;
            UInt32 arity;
        };
        const AttributeInfo Attributes[] = {
            {"prop", LocKeys::ScriptSummaryProperty, 0},
            {"range", LocKeys::ScriptSummaryRange, 2},
            {"clamp", LocKeys::ScriptSummaryClamp, 2},
            {"name", LocKeys::ScriptSummaryName, 1},
            {"tooltip", LocKeys::ScriptSummaryTooltip, 1},
            {"category", LocKeys::ScriptSummaryCategory, 1},
            {"visiblewhen", LocKeys::ScriptSummaryVisibleWhen, 1},
            {"readonly", LocKeys::ScriptSummaryReadOnly, 0},
            {"noserialize", LocKeys::ScriptSummaryNoSerialize, 0},
            {"hidden", LocKeys::ScriptSummaryHidden, 0}
        };
        Bool SamePosition(Code::Position left, Code::Position right)
        {
            return left.line == right.line && left.character == right.character;
        }
        CodeEditorPanel* ActiveDocument(const EditorActionContext& context)
        {
            return context.editor == nullptr ? nullptr : CodeEditorPanel::FindActive(*context.editor);
        }
    }
    void ScriptSummaryPanel::RegisterActions()
    {
        EditorActionInfo save;
        save.name = "script_summary.save";
        save.labelKey = LocKeys::CodeEditorSave;
        save.fallbackLabel = "Save";
        save.categoryKey = LocKeys::ScriptSummaryTitle;
        save.panelType = TypeName;
        save.primary = {ImGuiKey_S, true};
        save.whileTyping = true;
        save.duringGame = true;
        save.CanExecute = [](const EditorActionContext& context) -> Bool {
            const auto* document = ActiveDocument(context);
            return document != nullptr && document->IsJBroScript() && !document->IsReadOnly() && document->HasUnsavedChanges();
        };
        save.Execute = [](EditorActionContext& context) -> Bool {
            auto* document = ActiveDocument(context);
            return document != nullptr && document->Save();
        };
        EditorActionRegistry::Get().Register(save);
        EditorActionInfo undo = save;
        undo.name = "script_summary.undo";
        undo.labelKey = LocKeys::MenuUndo;
        undo.fallbackLabel = "Undo";
        undo.primary = {ImGuiKey_Z, true};
        undo.whileTyping = false;
        undo.CanExecute = [](const EditorActionContext& context) -> Bool {
            auto* document = ActiveDocument(context);
            return document != nullptr && document->IsJBroScript() && !document->IsReadOnly() && document->GetBuffer().CanUndo();
        };
        undo.Execute = [](EditorActionContext& context) -> Bool {
            auto* document = ActiveDocument(context);
            if (document == nullptr || document->IsReadOnly())
            {
                return false;
            }
            document->Undo();
            return true;
        };
        EditorActionRegistry::Get().Register(undo);
        EditorActionInfo redo = undo;
        redo.name = "script_summary.redo";
        redo.labelKey = LocKeys::MenuRedo;
        redo.fallbackLabel = "Redo";
        redo.primary = {ImGuiKey_Y, true};
        redo.secondary = {ImGuiKey_Z, true, true};
        redo.CanExecute = [](const EditorActionContext& context) -> Bool {
            auto* document = ActiveDocument(context);
            return document != nullptr && document->IsJBroScript() && !document->IsReadOnly() && document->GetBuffer().CanRedo();
        };
        redo.Execute = [](EditorActionContext& context) -> Bool {
            auto* document = ActiveDocument(context);
            if (document == nullptr || document->IsReadOnly())
            {
                return false;
            }
            document->Redo();
            return true;
        };
        EditorActionRegistry::Get().Register(redo);
    }
    const char* ScriptSummaryPanel::GetDisplayTitle() const
    {
        return Loc::TextOr(LocKeys::ScriptSummaryTitle, "Script Summary");
    }
    Bool ScriptSummaryPanel::OnCreate(EditorApplication& editor)
    {
        m_editor = &editor;
        return true;
    }
    void ScriptSummaryPanel::OnUpdate(Float)
    {
        if (m_pending == 0 || m_editor == nullptr)
        {
            return;
        }
        auto* panel = m_editor->FindPanel(m_pendingDocument);
        if (panel == nullptr || String(panel->GetTitle()) != CodeEditorPanel::TypeName)
        {
            m_pending = 0;
            return;
        }
        auto* document = static_cast<CodeEditorPanel*>(panel);
        Code::LanguageClient* language = document->GetLanguageClient();
        const auto* result = language == nullptr ? nullptr : language->GetFieldEditResult(m_pendingPath);
        if (result == nullptr || result->requestId != m_pending || !result->ready)
        {
            if (language == nullptr || language->GetState() != Code::ClientState::Ready)
            {
                m_error = Loc::TextOr(LocKeys::ScriptSummaryUnavailable, "Summary is unavailable");
                m_pending = 0;
            }
            return;
        }
        m_pending = 0;
        if (!result->success)
        {
            m_error = result->error;
            return;
        }
        if (document->IsReadOnly() || !document->ApplySourceEdits(result->version, result->edits))
        {
            m_error = Loc::TextOr(LocKeys::ScriptSummaryChanged, "The source changed. Refresh the field and try again.");
            return;
        }
        m_error.clear();
    }
    void ScriptSummaryPanel::OnDraw()
    {
        CodeEditorPanel* document = CodeEditorPanel::FindActive(*m_editor);
        if (!m_path.empty() && (document == nullptr || document->GetPath() != m_path))
        {
            Array<EditorPanel*> panels;
            m_editor->FindPanels(CodeEditorPanel::TypeName, panels);
            for (auto* panel : panels)
            {
                auto* previous = static_cast<CodeEditorPanel*>(panel);
                if (previous->GetPath() == m_path)
                {
                    CommitSelectedDraft(*previous);
                    break;
                }
            }
        }
        if (document == nullptr || !document->IsJBroScript())
        {
            m_path.clear();
            m_selected.clear();
            m_navigationPending = false;
            m_fields.Clear();
            m_entries.Clear();
            m_outline.Clear();
            Widget::HintText(Loc::TextOr(LocKeys::ScriptSummaryNoDocument, "Open a JBroScript file to see its summary"));
            return;
        }
        Code::LanguageClient* language = document->GetLanguageClient();
        const Code::ScriptSummary* summary = language == nullptr ? nullptr : language->GetSummary(document->GetPath());
        const Bool available = summary != nullptr && summary->valid && language->GetState() == Code::ClientState::Ready
            && summary->version == language->GetVersion(document->GetPath());
        if (m_path != document->GetPath())
        {
            m_path = document->GetPath();
            const auto separator = m_path.find_last_of("/\\");
            m_filename = separator == String::npos ? m_path : String(m_path.substr(separator + 1));
            m_selected.clear();
            m_navigationPending = false;
            m_summaryRevision = 0;
            m_fields.Clear();
            m_entries.Clear();
            m_outline.Clear();
            m_fieldCount = 0;
            m_functionCount = 0;
            m_error.clear();
        }
        if (available && summary->revision != m_summaryRevision)
        {
            m_fields.Clear();
            m_entries.Clear();
            m_outline.Clear();
            m_fieldCount = 0;
            m_functionCount = 0;
            CollectEntries(summary->types, {}, 0);
            m_sourceVersion = summary->version;
            Bool selectionExists = false;
            for (const auto& entry : m_entries)
            {
                selectionExists |= entry.key == m_selected;
            }
            if (!selectionExists)
            {
                m_selected = m_fields.IsEmpty() ? (m_entries.IsEmpty() ? String() : m_entries[0].key) : m_fields[0].key;
            }
            m_summaryRevision = summary->revision;
        }
        const char* status = !available ? Loc::TextOr(LocKeys::ScriptSummaryWaiting, "Waiting for a valid summary")
            : m_pending != 0 ? Loc::TextOr(LocKeys::ScriptSummaryApplying, "Checking source edit...")
            : document->IsReadOnly() ? Loc::TextOr(LocKeys::ScriptSummaryLocked, "Source editing is locked") : nullptr;
        Widget::ScriptSummaryHeader(m_filename.c_str(), m_path.c_str(), m_fieldCount, m_functionCount, status, !available);
        Widget::IdScope source(m_path.c_str());
        String nextSelection = m_selected;
        Bool selectionChanged = false;
        if (Widget::BeginScriptOutline())
        {
            if (m_outline.IsEmpty())
            {
                Widget::HintText(Loc::TextOr(LocKeys::ScriptSummaryEmpty, "No declarations in this file"));
            }
            selectionChanged = Widget::ScriptOutline({m_outline.Data(), m_outline.Size()}, nextSelection);
        }
        Widget::EndScriptOutline();
        Widget::IdScope detail(m_selected.c_str());
        if (Widget::BeginScriptDetails())
        {
            DrawDetails(*document, available);
        }
        Widget::EndScriptDetails();
        if (selectionChanged)
        {
            CommitSelectedDraft(*document);
            m_selected = nextSelection;
            m_navigationPending = available;
        }
        if (m_navigationPending && available && m_pending == 0)
        {
            for (const auto& entry : m_entries)
            {
                if (entry.key == m_selected)
                {
                    document->FocusAt(entry.source.selectionRange.start.line + 1u, entry.source.selectionRange.start.character);
                    break;
                }
            }
            m_navigationPending = false;
        }
    }
    void ScriptSummaryPanel::CollectEntries(const Array<Code::SummaryEntry>& entries, const String& parent, UInt32 depth, Bool fieldsReadOnly)
    {
        for (const auto& source : entries)
        {
            Entry entry;
            entry.key = parent + "/" + std::to_string(static_cast<int>(source.kind)) + ":" + source.name;
            if (source.kind == Code::SummaryKind::Function)
            {
                entry.key += ":" + source.signature;
                ++m_functionCount;
            }
            for (const auto& existing : m_entries)
            {
                if (existing.key == entry.key)
                {
                    entry.key += ":" + std::to_string(m_entries.Size());
                    break;
                }
            }
            entry.source = source;
            entry.source.members.Clear();
            entry.fieldReadOnly = fieldsReadOnly;
            const auto qualifier = [&](const char* value) {
                if (!entry.qualifiers.empty())
                {
                    entry.qualifiers += "  ·  ";
                }
                entry.qualifiers += value;
            };
            if (source.access != "default" && !source.access.empty())
            {
                qualifier(source.access.c_str());
            }
            if (source.isStatic)
            {
                qualifier("static");
            }
            if (source.isCallback)
            {
                qualifier(Loc::TextOr(LocKeys::ScriptSummaryCallback, "Callback"));
            }
            Widget::ScriptOutlineItem item;
            item.key = entry.key;
            item.name = source.kind == Code::SummaryKind::Function ? String(source.name + "()") : source.name;
            item.detail = source.type;
            item.kind = source.kind;
            item.depth = depth;
            m_outline.Add(std::move(item));
            const String key = entry.key;
            m_entries.Add(std::move(entry));
            if (source.kind == Code::SummaryKind::Field)
            {
                ++m_fieldCount;
                FieldDraft field;
                field.key = key;
                field.position = source.selectionRange.start;
                field.initializerPresent = source.initializerPresent;
                field.initializer = source.initializer;
                for (const auto& info : Attributes)
                {
                    AttributeDraft attribute;
                    attribute.name = info.name;
                    for (UInt32 argument = 0; argument < info.arity; ++argument)
                    {
                        attribute.arguments.Add(info.arity == 2 ? (argument == 0 ? "0" : "1") : "\"\"");
                    }
                    for (const auto& original : source.attributes)
                    {
                        if (original.name == attribute.name)
                        {
                            attribute.enabled = true;
                            attribute.arguments = original.arguments;
                            break;
                        }
                    }
                    field.attributes.Add(std::move(attribute));
                }
                m_fields.Add(std::move(field));
            }
            CollectEntries(source.members, key, depth + 1u, fieldsReadOnly || source.type == "enum");
        }
    }
    void ScriptSummaryPanel::DrawDetails(CodeEditorPanel& document, Bool available)
    {
        for (const auto& entry : m_entries)
        {
            if (entry.key != m_selected)
            {
                continue;
            }
            Widget::IdScope selected(entry.key.c_str());
            Widget::ScriptMemberHeader(entry.source.name.c_str(), entry.source.type.c_str(), entry.qualifiers.c_str(), entry.source.kind);
            if (entry.source.kind == Code::SummaryKind::Field)
            {
                DrawField(document, entry, available && !entry.fieldReadOnly);
            }
            else if (!entry.source.signature.empty())
            {
                Widget::WrappedText(entry.source.signature.c_str());
            }
            return;
        }
        Widget::HintText(Loc::TextOr(LocKeys::ScriptSummarySelectMember, "Select a declaration to see its details"));
    }
    void ScriptSummaryPanel::DrawField(CodeEditorPanel& document, const Entry& selected, Bool available)
    {
        const auto& entry = selected.source;
        FieldDraft* field = nullptr;
        for (auto& candidate : m_fields)
        {
            if (candidate.key == selected.key)
            {
                field = &candidate;
                break;
            }
        }
        if (field == nullptr)
        {
            return;
        }
        const Bool invalid = !m_error.empty() && m_errorPath == document.GetPath() && SamePosition(m_errorPosition, field->position);
        const Bool locked = !available || document.IsReadOnly() || m_pending != 0;
        const auto attribute = [&](const char* name) -> AttributeDraft& {
            for (auto& candidate : field->attributes)
            {
                if (candidate.name == name)
                {
                    return candidate;
                }
            }
            return field->attributes[0];
        };
        {
            Widget::FormLayout layout("##initializer", 6.0f, ImVec2(2.0f, 3.0f), 90.0f);
            layout.Row(Widget::FieldLabel(Loc::TextOr(LocKeys::ScriptSummaryInitializer, "Initial value")), [&] {
                Widget::DisableScope disabled(locked);
                if (Widget::ScriptInitializerField("##initializer", field->initializer, field->initializerPresent, invalid))
                {
                    Submit(document, *field, true);
                }
            });
        }
        {
            Widget::FormLayout layout("##common", 6.0f, ImVec2(2.0f, 3.0f), 90.0f);
            layout.Row(Widget::FieldLabel(Loc::Text(LocKeys::ScriptSummaryProperty)), [&] {
                Widget::DisableScope disabled(locked || m_pending != 0);
                if (Widget::Checkbox("##prop", attribute("prop").enabled))
                {
                    Submit(document, *field, false);
                }
            });
            layout.Row(Widget::FieldLabel(Loc::Text(LocKeys::ScriptSummaryReadOnly)), [&] {
                Widget::DisableScope disabled(locked);
                if (entry.isConst)
                {
                    const char* items[] = {
                        Loc::Text(LocKeys::ScriptSummaryAutomaticReadOnly),
                        Loc::Text(LocKeys::ScriptSummaryExplicitReadOnly)
                    };
                    Int32 current = attribute("readonly").enabled ? 1 : 0;
                    if (Widget::FilterCombo("##readonly", {items, 2}, current).ShowFilter(false).Draw())
                    {
                        attribute("readonly").enabled = current == 1;
                        Submit(document, *field, false);
                    }
                }
                else if (Widget::Checkbox("##readonly", attribute("readonly").enabled))
                {
                    Submit(document, *field, false);
                }
                Widget::HoveredTooltip(entry.isConst ? Loc::Text(LocKeys::ScriptSummaryConstRule) : nullptr);
            });
            layout.Row(Widget::FieldLabel(Loc::Text(LocKeys::ScriptSummaryNoSerialize)), [&] {
                Widget::DisableScope disabled(locked || m_pending != 0);
                const Bool implicit = entry.isConst || entry.isStatic;
                const char* items[] = {
                    Loc::Text(implicit ? LocKeys::ScriptSummaryAutomaticExclude : LocKeys::ScriptSummaryDefault),
                    Loc::Text(implicit ? LocKeys::ScriptSummaryExplicitExclude : LocKeys::ScriptSummaryExcluded)
                };
                Int32 current = attribute("noserialize").enabled ? 1 : 0;
                if (Widget::FilterCombo("##serialize", {items, 2}, current).ShowFilter(false).Draw())
                {
                    attribute("noserialize").enabled = current == 1;
                    Submit(document, *field, false);
                }
                Widget::HoveredTooltip(implicit ? Loc::Text(entry.isConst ? LocKeys::ScriptSummaryConstRule : LocKeys::ScriptSummaryStaticRule) : nullptr);
            });
        }
        if (entry.isConst || entry.isStatic)
        {
            Widget::ScriptSummaryNote(Loc::Text(entry.isConst ? LocKeys::ScriptSummaryConstRule : LocKeys::ScriptSummaryStaticRule));
        }
        else if (entry.effective.property && !attribute("prop").enabled)
        {
            Widget::ScriptSummaryNote(Loc::Text(LocKeys::ScriptSummaryImplicitProperty));
        }
        if (invalid)
        {
            Widget::ScriptSummaryNote(Loc::Text(LocKeys::ScriptSummaryEditRejected), true);
            Widget::HoveredTooltip(m_error.c_str());
        }
        if (!Widget::CollapsingSection(Loc::TextOr(LocKeys::ScriptSummaryAdvanced, "Advanced settings"), false))
        {
            return;
        }
        Widget::FormLayout advanced("##advanced", 6.0f, ImVec2(2.0f, 3.0f), 90.0f);
        for (std::size_t index = 0; index < field->attributes.Size(); ++index)
        {
            auto& setting = field->attributes[index];
            if (setting.name == "prop" || setting.name == "readonly" || setting.name == "noserialize")
            {
                continue;
            }
            Widget::IdScope id(setting.name.c_str());
            advanced.Row(Widget::FieldLabel(Loc::Text(Attributes[index].label)), [&] {
                Widget::DisableScope disabled(locked || m_pending != 0);
                if (Widget::Checkbox("##enabled", setting.enabled))
                {
                    Submit(document, *field, false);
                }
            });
            if (setting.enabled)
            {
                for (std::size_t argument = 0; argument < setting.arguments.Size(); ++argument)
                {
                    Widget::IdScope argumentId(static_cast<Int32>(argument));
                    const char* label = setting.arguments.Size() == 2
                        ? (argument == 0 ? LocKeys::ScriptSummaryMinimum : LocKeys::ScriptSummaryMaximum)
                        : LocKeys::ScriptSummaryExpression;
                    advanced.Row(Widget::FieldLabel(Loc::Text(label)), [&] {
                        Widget::DisableScope disabled(locked || m_pending != 0);
                        if (Widget::TextField("##argument", setting.arguments[argument]).CommitOnFinish().Invalid(invalid).Draw())
                        {
                            Submit(document, *field, false);
                        }
                    });
                }
            }
        }
    }
    void ScriptSummaryPanel::CommitSelectedDraft(CodeEditorPanel& document)
    {
        auto* language = document.GetLanguageClient();
        const auto* summary = language == nullptr ? nullptr : language->GetSummary(document.GetPath());
        if (m_pending != 0 || document.IsReadOnly() || document.GetPath() != m_path || summary == nullptr
            || !summary->valid || language->GetVersion(document.GetPath()) != m_sourceVersion || summary->version != m_sourceVersion)
        {
            return;
        }
        for (auto& field : m_fields)
        {
            if (field.key != m_selected)
            {
                continue;
            }
            for (const auto& entry : m_entries)
            {
                if (entry.key != field.key || entry.fieldReadOnly)
                {
                    continue;
                }
                Bool initializer = field.initializerPresent != entry.source.initializerPresent
                    || (field.initializerPresent && field.initializer != entry.source.initializer);
                if (field.initializerPresent && field.initializer.empty())
                {
                    initializer = false;
                }
                Bool attributes = false;
                for (const auto& desired : field.attributes)
                {
                    const Code::FieldAttribute* original = nullptr;
                    for (const auto& source : entry.source.attributes)
                    {
                        if (source.name == desired.name)
                        {
                            original = &source;
                            break;
                        }
                    }
                    attributes |= desired.enabled != (original != nullptr);
                    if (desired.enabled && original != nullptr)
                    {
                        attributes |= desired.arguments.Size() != original->arguments.Size();
                        for (std::size_t argument = 0; argument < desired.arguments.Size() && argument < original->arguments.Size(); ++argument)
                        {
                            attributes |= desired.arguments[argument] != original->arguments[argument];
                        }
                    }
                }
                if (initializer || attributes)
                {
                    Submit(document, field, initializer);
                }
                return;
            }
        }
    }
    void ScriptSummaryPanel::Submit(CodeEditorPanel& document, FieldDraft& field, Bool initializer)
    {
        auto* language = document.GetLanguageClient();
        if (language == nullptr || m_pending != 0 || document.IsReadOnly())
        {
            return;
        }
        Code::FieldSettings settings;
        settings.replaceInitializer = initializer;
        settings.initializerPresent = field.initializerPresent;
        settings.initializer = field.initializer;
        for (const auto& attribute : field.attributes)
        {
            if (attribute.enabled)
            {
                settings.attributes.Add({attribute.name, attribute.arguments});
            }
        }
        m_errorPosition = field.position;
        m_errorPath = document.GetPath();
        m_error.clear();
        m_pending = language->EditFieldSettings(document.GetPath(), language->GetVersion(document.GetPath()), field.position, settings);
        if (m_pending == 0)
        {
            m_error = Loc::TextOr(LocKeys::ScriptSummaryChanged, "The source changed. Refresh the field and try again.");
            return;
        }
        m_pendingPath = document.GetPath();
        m_pendingDocument = document.GetId();
    }
}
