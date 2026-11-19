#include "CodeEditorPanel.h"
#include <JBro/Types/ValueMath.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/Code/SourceDebugger.h>
#include "ScriptSummaryPanel.h"
#include <JBro/Editor/EditorActionRegistry.h>
#include <JBro/Editor/EditorIcons.h>
#include <JBro/Editor/EditorPopup.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/Common.h>
#include <JBro/Editor/Widget/Fields.h>
#include <JBro/Editor/Widget/GuideFocus.h>
#include <JBro/Platform/Platform.h>
#include <algorithm>
#include <cstring>
#include <cctype>
#include <cstdio>

namespace JBro
{
    namespace
    {
        UInt64 g_codeFocusSerial = 0;
        class CodeFileDecision final : public EditorPopup
        {
        public:
            CodeFileDecision(Uuid panel, Bool reload) : m_panel(panel), m_reload(reload) {}
            const char* GetTitle() const override { return Loc::TextOr(LocKeys::FileWindowUnsavedTitle, "Unsaved changes"); }
            void OnDraw(EditorApplication& editor) override
            {
                auto* panel = static_cast<CodeEditorPanel*>(editor.FindPanel(m_panel));
                if (panel == nullptr)
                {
                    Close();
                    return;
                }
                Widget::WrappedText(Loc::TextOr(LocKeys::CodeEditorUnsaved, "This file has unsaved changes."));
                if (!m_reload && Widget::ActionButton(Loc::TextOr(LocKeys::FileWindowSaveAndClose, "Save and close")))
                {
                    if (panel->Save())
                    {
                        editor.ClosePanel(*panel);
                        Close();
                    }
                }
                if (Widget::ActionButton(m_reload ? Loc::TextOr(LocKeys::FileWindowReloadDiscard, "Reload without saving")
                    : Loc::TextOr(LocKeys::FileWindowCloseWithoutSaving, "Close without saving"), Widget::Severity::Warning))
                {
                    if (m_reload)
                    {
                        panel->Reload();
                    }
                    else
                    {
                        editor.ClosePanel(*panel);
                    }
                    Close();
                }
                if (Widget::Button(Loc::TextOr(LocKeys::CommonCancel, "Cancel"), Icons::Xmark))
                {
                    Close();
                }
            }
        private:
            Uuid m_panel;
            Bool m_reload;
        };
        Bool Read(IPlatform& platform, const String& path, String& text)
        {
            Array<std::byte> data;
            if (!platform.ReadWholeFile(path.c_str(), data) || data.Size() > 8 * 1024 * 1024)
            {
                return false;
            }
            text = data.IsEmpty() ? String() : String(reinterpret_cast<const char*>(data.Data()), data.Size());
            for (std::size_t at = 0; at < text.size();)
            {
                const auto lead = static_cast<unsigned char>(text[at++]);
                if (lead == 0)
                {
                    return false;
                }
                if (lead < 0x80)
                {
                    continue;
                }
                const std::size_t count = lead >= 0xc2 && lead <= 0xdf ? 1 : lead >= 0xe0 && lead <= 0xef ? 2 : lead >= 0xf0 && lead <= 0xf4 ? 3 : 0;
                if (count == 0 || count > text.size() - at)
                {
                    return false;
                }
                const auto first = static_cast<unsigned char>(text[at]);
                if ((lead == 0xe0 && first < 0xa0) || (lead == 0xed && first >= 0xa0)
                    || (lead == 0xf0 && first < 0x90) || (lead == 0xf4 && first >= 0x90))
                {
                    return false;
                }
                for (std::size_t byte = 0; byte < count; ++byte)
                {
                    if ((static_cast<unsigned char>(text[at++]) & 0xc0) != 0x80)
                    {
                        return false;
                    }
                }
            }
            return true;
        }
        String NormalizedText(const String& source, Bool& bom, Bool& crlf)
        {
            bom = source.size() >= 3 && source.View().substr(0, 3) == "\xef\xbb\xbf";
            crlf = source.find("\r\n") != String::npos;
            String result;
            for (std::size_t i = bom ? 3 : 0; i < source.size(); ++i)
            {
                if (source[i] != '\r' || i + 1 == source.size() || source[i + 1] != '\n')
                {
                    result += source[i];
                }
            }
            return result;
        }
    }
    namespace
    {
        enum class SourceAction
        {
            Save, Undo, Redo, Cut, Copy, Paste, SelectAll, Reload, External, Close, DebugStart, DebugContinue, DebugStop
        };

        const char* WhySourceActionBlocked(const EditorActionContext& context, SourceAction action)
        {
            if (context.panel == nullptr || context.editor == nullptr)
            {
                return Loc::TextOr(LocKeys::BlockedPanelNotOpen, "The window this works in is not open.");
            }
            auto& document = *static_cast<CodeEditorPanel*>(context.panel);
            const Bool mutates = action == SourceAction::Save || action == SourceAction::Undo || action == SourceAction::Redo
                || action == SourceAction::Cut || action == SourceAction::Paste;
            if ((mutates || action == SourceAction::Reload) && context.editor->IsAtScriptBreak())
            {
                return Loc::TextOr(LocKeys::BlockedAtScriptBreak, "A script is stopped at a breakpoint. Continue or stop debugging first.");
            }
            if (mutates && document.IsReadOnly())
            {
                return Loc::TextOr(LocKeys::CodeEditorReadOnly, "This file is read only.");
            }
            const Bool editsText = action == SourceAction::Undo || action == SourceAction::Redo || action == SourceAction::Cut
                || action == SourceAction::Copy || action == SourceAction::Paste || action == SourceAction::SelectAll;
            if (editsText && Widget::IsTextInputActive())
            {
                return Loc::TextOr(LocKeys::CodeEditorFinishInput, "Finish editing the active input field first.");
            }
            if (action == SourceAction::Save && !document.HasUnsavedChanges())
            {
                return Loc::TextOr(LocKeys::CodeEditorNoChanges, "There are no changes to save.");
            }
            if (action == SourceAction::Undo && !document.GetBuffer().CanUndo())
            {
                return Loc::TextOr(LocKeys::BlockedNothingToUndo, "There is nothing to undo.");
            }
            if (action == SourceAction::Redo && !document.GetBuffer().CanRedo())
            {
                return Loc::TextOr(LocKeys::BlockedNothingToRedo, "There is nothing to redo.");
            }
            if (action == SourceAction::Cut && !document.GetBuffer().HasSelection())
            {
                return Loc::TextOr(LocKeys::CodeEditorNoSelection, "No text is selected.");
            }
            if (action == SourceAction::Paste && !document.GetBuffer().CanPaste())
            {
                return Loc::TextOr(LocKeys::BlockedClipboardEmpty, "Nothing has been copied.");
            }
            if ((action == SourceAction::Copy || action == SourceAction::SelectAll) && document.GetBufferText().empty())
            {
                return Loc::TextOr(LocKeys::CodeEditorEmptyDocument, "The document is empty.");
            }
            if (action == SourceAction::DebugStart || action == SourceAction::DebugContinue || action == SourceAction::DebugStop)
            {
                if (!document.IsJBroScript())
                {
                    return Loc::TextOr(LocKeys::ScriptSummaryNoDocument, "Open a JBroScript file first.");
                }
                if (action == SourceAction::DebugStart && context.editor->IsDebugSessionActive())
                {
                    return Loc::TextOr(LocKeys::CodeEditorDebugAlreadyActive, "A debugging session is already running.");
                }
                if (action != SourceAction::DebugStart && !context.editor->IsDebugSessionActive())
                {
                    return Loc::TextOr(LocKeys::CodeEditorDebugInactive, "No debugging session is running.");
                }
                if (action == SourceAction::DebugContinue && !context.editor->IsAtScriptBreak())
                {
                    return Loc::TextOr(LocKeys::CodeEditorDebugNotStopped, "The script is not stopped at a breakpoint.");
                }
            }
            return nullptr;
        }

        template<SourceAction Action>
        void SetSourceActionState(EditorActionInfo& info)
        {
            info.CanExecute = [](const EditorActionContext& context) -> Bool {
                return WhySourceActionBlocked(context, Action) == nullptr;
            };
            info.WhyBlocked = [](const EditorActionContext& context) {
                return WhySourceActionBlocked(context, Action);
            };
        }
    }
    void CodeEditorPanel::RegisterActions()
    {
        EditorActionInfo save;
        save.name = "code_editor.save";
        save.labelKey = LocKeys::CodeEditorSave;
        save.fallbackLabel = "Save";
        save.categoryKey = LocKeys::CodeEditorTitle;
        save.panelType = TypeName;
        save.primary = {ImGuiKey_S, true};
        save.whileTyping = true;
        save.duringGame = true;
        save.menus = EditorActionMenu::File;
        save.placedByEditor = true;
        save.icon = Icons::Save;
        SetSourceActionState<SourceAction::Save>(save);
        save.Execute = [](EditorActionContext& context) -> Bool { return static_cast<CodeEditorPanel*>(context.panel)->Save(); };
        EditorActionRegistry::Get().Register(save);
        EditorActionInfo undo = save;
        undo.name = "code_editor.undo";
        undo.labelKey = LocKeys::MenuUndo;
        undo.fallbackLabel = "Undo";
        undo.primary = {ImGuiKey_Z, true};
        undo.menus = EditorActionMenu::Edit;
        undo.icon = Icons::Undo;
        SetSourceActionState<SourceAction::Undo>(undo);
        undo.Execute = [](EditorActionContext& context) -> Bool {
            static_cast<CodeEditorPanel*>(context.panel)->Undo();
            return true;
        };
        EditorActionRegistry::Get().Register(undo);
        EditorActionInfo redo = undo;
        redo.name = "code_editor.redo";
        redo.labelKey = LocKeys::MenuRedo;
        redo.fallbackLabel = "Redo";
        redo.primary = {ImGuiKey_Y, true};
        redo.secondary = {ImGuiKey_Z, true, true};
        redo.icon = Icons::Redo;
        SetSourceActionState<SourceAction::Redo>(redo);
        redo.Execute = [](EditorActionContext& context) -> Bool {
            static_cast<CodeEditorPanel*>(context.panel)->Redo();
            return true;
        };
        EditorActionRegistry::Get().Register(redo);
        EditorActionInfo cut = undo;
        cut.name = "code_editor.cut";
        cut.labelKey = LocKeys::MenuCut;
        cut.fallbackLabel = "Cut";
        cut.primary = {ImGuiKey_X, true};
        cut.secondary = {ImGuiKey_Delete, true};
        cut.whileTyping = true;
        cut.icon = Icons::Cut;
        SetSourceActionState<SourceAction::Cut>(cut);
        cut.Execute = [](EditorActionContext& context) -> Bool {
            static_cast<CodeEditorPanel*>(context.panel)->Cut();
            return true;
        };
        EditorActionRegistry::Get().Register(cut);
        EditorActionInfo copy = cut;
        copy.name = "code_editor.copy";
        copy.labelKey = LocKeys::MenuCopy;
        copy.fallbackLabel = "Copy";
        copy.primary = {ImGuiKey_C, true};
        copy.secondary = {ImGuiKey_Insert, true};
        copy.icon = Icons::Copy;
        SetSourceActionState<SourceAction::Copy>(copy);
        copy.Execute = [](EditorActionContext& context) -> Bool {
            static_cast<CodeEditorPanel*>(context.panel)->m_buffer.Copy();
            return true;
        };
        EditorActionRegistry::Get().Register(copy);
        EditorActionInfo paste = cut;
        paste.name = "code_editor.paste";
        paste.labelKey = LocKeys::MenuPaste;
        paste.fallbackLabel = "Paste";
        paste.primary = {ImGuiKey_V, true};
        paste.secondary = {ImGuiKey_Insert, false, true};
        paste.icon = Icons::Paste;
        SetSourceActionState<SourceAction::Paste>(paste);
        paste.Execute = [](EditorActionContext& context) -> Bool {
            static_cast<CodeEditorPanel*>(context.panel)->Paste();
            return true;
        };
        EditorActionRegistry::Get().Register(paste);
        EditorActionInfo selectAll = copy;
        selectAll.name = "code_editor.select_all";
        selectAll.labelKey = LocKeys::MenuSelectAll;
        selectAll.fallbackLabel = "Select all";
        selectAll.primary = {ImGuiKey_A, true};
        selectAll.secondary = {};
        selectAll.icon = Icons::SelectAll;
        SetSourceActionState<SourceAction::SelectAll>(selectAll);
        selectAll.Execute = [](EditorActionContext& context) -> Bool {
            static_cast<CodeEditorPanel*>(context.panel)->m_buffer.SelectAll();
            return true;
        };
        EditorActionRegistry::Get().Register(selectAll);
        EditorActionInfo reload = save;
        reload.name = "code_editor.reload";
        reload.labelKey = LocKeys::CodeEditorReload;
        reload.fallbackLabel = "Reload from disk";
        reload.primary = {};
        reload.icon = Icons::Refresh;
        SetSourceActionState<SourceAction::Reload>(reload);
        reload.Execute = [](EditorActionContext& context) -> Bool {
            static_cast<CodeEditorPanel*>(context.panel)->RequestReload();
            return true;
        };
        EditorActionRegistry::Get().Register(reload);
        EditorActionInfo external = reload;
        external.name = "code_editor.external";
        external.labelKey = LocKeys::CodeEditorExternal;
        external.fallbackLabel = "Open in external editor";
        external.icon = Icons::OpenExternal;
        SetSourceActionState<SourceAction::External>(external);
        external.Execute = [](EditorActionContext& context) -> Bool {
            return context.editor->OpenCodeFileExternally(static_cast<CodeEditorPanel*>(context.panel)->GetPath());
        };
        EditorActionRegistry::Get().Register(external);
        EditorActionInfo close = reload;
        close.name = "code_editor.close";
        close.labelKey = LocKeys::CodeEditorClose;
        close.fallbackLabel = "Close";
        close.icon = Icons::Xmark;
        SetSourceActionState<SourceAction::Close>(close);
        close.Execute = [](EditorActionContext& context) -> Bool {
            static_cast<CodeEditorPanel*>(context.panel)->RequestClose();
            return true;
        };
        EditorActionRegistry::Get().Register(close);
        EditorActionInfo debug = reload;
        debug.name = "code_editor.debug_start";
        debug.labelKey = LocKeys::CodeEditorStartDebugging;
        debug.fallbackLabel = "Start debugging";
        debug.menus = EditorActionMenu::None;
        debug.icon = Icons::Debug;
        SetSourceActionState<SourceAction::DebugStart>(debug);
        debug.Execute = [](EditorActionContext& context) -> Bool {
            context.editor->StartDebugSession();
            return true;
        };
        EditorActionRegistry::Get().Register(debug);
        debug.name = "code_editor.debug_continue";
        debug.labelKey = LocKeys::CodeEditorContinueDebugging;
        debug.fallbackLabel = "Continue";
        debug.icon = Icons::Play;
        SetSourceActionState<SourceAction::DebugContinue>(debug);
        debug.Execute = [](EditorActionContext& context) -> Bool {
            context.editor->ContinueScriptBreak();
            return true;
        };
        EditorActionRegistry::Get().Register(debug);
        debug.name = "code_editor.debug_stop";
        debug.labelKey = LocKeys::CodeEditorStopDebugging;
        debug.fallbackLabel = "Stop debugging";
        debug.icon = Icons::Stop;
        SetSourceActionState<SourceAction::DebugStop>(debug);
        debug.Execute = [](EditorActionContext& context) -> Bool {
            context.editor->StopDebugSession();
            return true;
        };
        EditorActionRegistry::Get().Register(debug);
    }
    Bool CodeEditorPanel::OnCreate(EditorApplication& editor)
    {
        m_editor = &editor;
        m_buffer.BindMenu(&editor, this);
        return true;
    }
    void CodeEditorPanel::OnDestroy()
    {
        m_buffer.BindMenu(nullptr, nullptr);
        m_assistance.Connect(nullptr, {});
        if (m_language != nullptr)
        {
            m_language->Close(m_path);
        }
    }
    Bool CodeEditorPanel::Open(const String& path, UInt32 line, UInt32 column)
    {
        m_path = Code::LanguageClient::NormalizePath(path);
        m_markerAnchors = UInt64(-1);
        m_markerClient = UInt64(-1);
        m_markerLocale = UInt64(-1);
        m_debugMarkers.Clear();
        m_executionLine = 0;
        m_buffer.SetDebugMarkers({}, 0);
        String extension = m_path.substr(m_path.find_last_of('.') + 1);
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        m_jscript = extension == "jscript";
        m_buffer.SetJBroScript(m_jscript);
        const String root = Code::LanguageClient::NormalizePath(m_editor->GetScriptRoot());
        m_readOnly = root.empty() || (m_path != root && !m_path.StartsWith(root + "/"));
        m_buffer.SetReadOnly(m_readOnly);
        if (!Reload())
        {
            return false;
        }
        if (m_jscript)
        {
            m_language = m_editor->GetCodeLanguageClient().SafeFromThis();
            m_language->Open(m_path, m_text);
        }
        m_assistance.Connect(m_language, m_path);
        FocusAt(line, column);
        return true;
    }
    Bool CodeEditorPanel::Reload()
    {
        String bytes;
        if (!Read(m_editor->GetPlatform(), m_path, bytes))
        {
            m_message = Loc::TextOr(LocKeys::CodeEditorReadFailed, "The code file could not be read as UTF-8 text.");
            return false;
        }
        m_diskBytes = bytes;
        m_text = NormalizedText(bytes, m_bom, m_crlf);
        m_assistance.Invalidate();
        m_buffer.SetText(m_text);
        m_buffer.TakeChanges();
        m_saved = m_text;
        m_revision = m_buffer.GetRevision();
        if (m_jscript)
        {
            m_editor->GetSourceDebugger().Anchors().Open(m_path, m_text, m_buffer.GetHistoryIndex());
            if (m_editor->FindPanel(ScriptSummaryPanel::TypeName) == nullptr)
            {
                m_editor->CreatePanel(ScriptSummaryPanel::TypeName);
            }
        }
        m_analysisRevision = 0;
        m_message.clear();
        if (m_language != nullptr)
        {
            m_language->Change(m_path, m_text);
        }
        UpdateTitle();
        return true;
    }
    void CodeEditorPanel::Changed()
    {
        m_assistance.Invalidate();
        const auto sourceChanges = m_buffer.TakeChanges();
        m_text = m_buffer.GetText();
        m_revision = m_buffer.GetRevision();
        if (m_jscript)
        {
            m_editor->GetSourceDebugger().Anchors().Change(m_path, m_text, m_buffer.GetHistoryIndex(), sourceChanges);
        }
        m_buffer.ClearAnalysis();
        m_diagnostics.Clear();
        if (m_language != nullptr)
        {
            m_language->Change(m_path, m_text);
        }
        UpdateTitle();
    }
    void CodeEditorPanel::OnUpdate(Float elapsed)
    {
        m_buffer.SetReadOnly(IsReadOnly());
        if (m_buffer.GetRevision() != m_revision)
        {
            Changed();
        }
        if (m_language != nullptr)
        {
            const auto* analysis = m_language->GetAnalysis(m_path);
            if (analysis != nullptr && analysis->revision != m_analysisRevision)
            {
                m_analysisRevision = analysis->revision;
                m_diagnostics = analysis->diagnostics;
                m_buffer.SetAnalysis(*analysis);
            }
        }
        m_assistance.Update(elapsed);
    }
    void CodeEditorPanel::UpdateTitle()
    {
        const auto slash = m_path.find_last_of("/\\");
        m_title = slash == String::npos ? m_path : m_path.substr(slash + 1);
        if (HasUnsavedChanges())
        {
            m_title += " *";
        }
    }
    Bool CodeEditorPanel::HasUnsavedChanges() const { return !m_readOnly && m_text != m_saved; }
    Bool CodeEditorPanel::ApplyAgentText(const String& expected, const String& text)
    {
        if (m_buffer.GetRevision() != m_revision)
        {
            Changed();
        }
        String disk;
        if (IsReadOnly() || HasUnsavedChanges() || m_buffer.GetText() != expected
            || !Read(m_editor->GetPlatform(), m_path, disk) || disk != m_diskBytes)
        {
            return false;
        }
        Code::TextEdit replacement;
        replacement.range.start = {0, 0};
        replacement.range.end = {0, 0};
        for (std::size_t at = 0; at < expected.size(); ++at)
        {
            const auto byte = static_cast<unsigned char>(expected[at]);
            if (byte == '\n')
            {
                ++replacement.range.end.line;
                replacement.range.end.character = 0;
            }
            else if ((byte & 0xc0) != 0x80)
            {
                replacement.range.end.character += byte >= 0xf0 ? 2 : 1;
            }
        }
        replacement.newText = text;
        Array<Code::TextEdit> edits;
        edits.Add(std::move(replacement));
        if (!m_buffer.ApplyEdits(edits))
        {
            return false;
        }
        Changed();
        return Save();
    }
    Bool CodeEditorPanel::SaveChanges() { return !HasUnsavedChanges() || Save(); }
    Bool CodeEditorPanel::Save()
    {
        if (IsReadOnly())
        {
            return false;
        }
        if (m_buffer.GetRevision() != m_revision)
        {
            Changed();
        }
        String current;
        if (!Read(m_editor->GetPlatform(), m_path, current) || current != m_diskBytes)
        {
            m_message = Loc::TextOr(LocKeys::CodeEditorConflict, "The file changed on disk. Reload it before saving.");
            return false;
        }
        String bytes = m_bom ? String("\xef\xbb\xbf") : String();
        for (char ch : m_text)
        {
            if (m_crlf && ch == '\n')
            {
                bytes += '\r';
            }
            bytes += ch;
        }
        JArrayView<std::byte> data{reinterpret_cast<const std::byte*>(bytes.data()), static_cast<UInt32>(bytes.size())};
        char identity[Uuid::TextCapacity]{};
        GetId().ToText(identity, sizeof(identity));
        const String temporary = m_path + ".jbro-save-" + identity + ".tmp";
        auto& platform = m_editor->GetPlatform();
        if (!platform.WriteWholeFile(temporary.c_str(), data) || !platform.MoveFileTo(temporary.c_str(), m_path.c_str()))
        {
            platform.DeleteFileAt(temporary.c_str());
            m_message = Loc::TextOr(LocKeys::CodeEditorSaveFailed, "The code file could not be saved.");
            return false;
        }
        m_diskBytes = bytes;
        m_saved = m_text;
        m_message.clear();
        if (m_language != nullptr)
        {
            m_language->Saved(m_path);
        }
        UpdateTitle();
        return true;
    }
    void CodeEditorPanel::CancelSiblingFocusRequests()
    {
        if (m_editor == nullptr)
        {
            return;
        }
        Array<EditorPanel*> panels;
        m_editor->FindPanels(TypeName, panels);
        for (auto* panel : panels)
        {
            if (panel != this)
            {
                auto* document = static_cast<CodeEditorPanel*>(panel);
                document->TakeFocusRequest();
                document->m_focus = false;
                document->m_selectFocus = false;
            }
        }
    }
    void CodeEditorPanel::FocusAt(UInt32 line, UInt32 column)
    {
        CancelSiblingFocusRequests();
        m_focusSerial = ++g_codeFocusSerial;
        m_focus = true;
        m_selectFocus = false;
        m_focusLine = line > 0 ? line - 1 : UInt32(0);
        m_focusColumn = column;
        RequestFocus();
    }
    void CodeEditorPanel::SelectDefinition(const Code::Range& range)
    {
        CancelSiblingFocusRequests();
        m_focusSerial = ++g_codeFocusSerial;
        m_focus = false;
        m_selectFocus = true;
        m_focusRange = range;
        m_assistance.HighlightReferences(range.start);
        RequestFocus();
    }
    void CodeEditorPanel::OnDraw()
    {
        if (IsFocused())
        {
            m_focusSerial = ++g_codeFocusSerial;
        }
        m_buffer.SetReadOnly(IsReadOnly());
        if (!m_message.empty())
        {
            Widget::SeverityTextF(Widget::Severity::Error, "%s", m_message.c_str());
        }
        if (m_language != nullptr)
        {
            const auto state = m_language->GetState();
            if (state == Code::ClientState::Failed && Widget::Button(Loc::TextOr(LocKeys::CodeEditorReconnect, "Reconnect language server"), Icons::Refresh))
            {
                m_editor->ReconnectCodeLanguageClient();
            }
        }
        for (std::size_t i = 0; i < Min<std::size_t>(m_diagnostics.Size(), 3); ++i)
        {
            const auto& diagnostic = m_diagnostics[i];
            Widget::SeverityTextF(diagnostic.severity == 1 ? Widget::Severity::Error : Widget::Severity::Warning, "%u: %s %s",
                diagnostic.range.start.line + 1, diagnostic.code.c_str(), diagnostic.message.c_str());
        }
        if (m_focus && IsVisible() && IsFocused())
        {
            m_buffer.Focus(m_focusLine, m_focusColumn);
            m_focus = false;
        }
        if (m_selectFocus && IsVisible() && IsFocused())
        {
            m_buffer.SelectRange(m_focusRange);
            m_selectFocus = false;
        }
        if (m_jscript)
        {
            auto& debugger = m_editor->GetSourceDebugger();
            const Bool stopped = m_editor->IsAtScriptBreak();
            EditorActionContext context;
            context.editor = m_editor;
            context.panel = this;
            const auto drawDebugAction = [&](const char* name) {
                const auto* action = EditorActionRegistry::Get().Find(name);
                if (action == nullptr)
                {
                    return;
                }
                const Bool enabled = EditorActionUi::CanExecute(*action, context);
                String tooltip = EditorActionUi::Label(*action, EditorActionMenu::None);
                if (!enabled)
                {
                    const char* reason = EditorActionUi::WhyBlocked(*action, context);
                    if (reason != nullptr)
                    {
                        tooltip += "\n";
                        tooltip += reason;
                    }
                }
                Widget::IdScope actionId(name);
                Widget::SetNextItemTarget(GuideFocusTargets::Action(name));
                if (Widget::IconButton("##action", action->icon).Tooltip(tooltip.c_str()).Disabled(!enabled).Draw())
                {
                    EditorActionUi::Execute(*action, context);
                }
            };
            if (!m_editor->IsDebugSessionActive())
            {
                drawDebugAction("code_editor.debug_start");
            }
            else
            {
                if (stopped)
                {
                    drawDebugAction("code_editor.debug_continue");
                    ImGui::SameLine();
                }
                drawDebugAction("code_editor.debug_stop");
            }
            const Bool matches = debugger.Matches(m_path, m_text);
            const UInt64 anchors = debugger.Anchors().GetRevision();
            const UInt64 client = debugger.Client().GetRevision();
            const UInt64 locale = LocalizationTable::Get().GetRevision();
            const Bool markersChanged = m_markerAnchors != anchors || m_markerClient != client
                || m_markerLocale != locale || m_markerMatches != matches;
            if (markersChanged)
            {
                m_debugMarkers.Clear();
                for (const auto line : debugger.Anchors().Lines(m_path))
                {
                    Widget::CodeEditor::Marker marker;
                    marker.line = line;
                    marker.message = Loc::TextOr(LocKeys::CodeEditorBreakpointPending, "Pending a matching build and engine installation.");
                    if (matches)
                    {
                        if (const auto* confirmed = debugger.Client().GetBreakpoints(m_path))
                        {
                            for (const auto& entry : *confirmed)
                            {
                                if (entry.requestedLine == line)
                                {
                                    marker.verified = entry.verified;
                                    marker.line = entry.actualLine > 0 ? entry.actualLine : line;
                                    marker.message = entry.message;
                                    if (marker.line != line)
                                    {
                                        marker.message += " (" + std::to_string(line.Get()) + " -> " + std::to_string(marker.line.Get()) + ")";
                                    }
                                }
                            }
                        }
                    }
                    m_debugMarkers.Add(std::move(marker));
                }
                m_markerAnchors = anchors;
                m_markerClient = client;
                m_markerLocale = locale;
                m_markerMatches = matches;
            }
            const auto& location = m_editor->GetScriptBreakLocation();
            const UInt32 execution = stopped && matches && Code::LanguageClient::NormalizePath(location.file) == m_path ? location.line : UInt32(0);
            if (markersChanged || m_executionLine != execution)
            {
                m_buffer.SetDebugMarkers(m_debugMarkers, execution);
                m_executionLine = execution;
            }
        }
        if (m_buffer.Render("##CodeBuffer", true) && m_buffer.GetRevision() != m_revision)
        {
            Changed();
        }
        m_assistance.AfterDraw();
        if (const UInt32 line = m_buffer.TakeBreakpointToggle(); line > 0 && m_jscript && !m_readOnly)
        {
            auto& debugger = m_editor->GetSourceDebugger();
            UInt32 requested = line;
            if (debugger.Matches(m_path, m_text))
            {
                if (const auto* confirmed = debugger.Client().GetBreakpoints(m_path))
                {
                    for (const auto& entry : *confirmed)
                    {
                        if (entry.actualLine == line)
                        {
                            requested = entry.requestedLine;
                            break;
                        }
                    }
                }
            }
            debugger.Anchors().Toggle(m_path, requested);
        }
        const auto cursor = m_buffer.GetCursor();
        char status[256]{};
        std::snprintf(status, sizeof(status), Loc::TextOr(LocKeys::CodeEditorCursor, "Line %u, Column %u | %u diagnostics"),
            cursor.line.Get() + 1, cursor.character.Get() + 1, static_cast<UInt32>(m_diagnostics.Size()).Get());
        const auto state = m_language != nullptr ? m_language->GetState() : Code::ClientState::Stopped;
        const char* analysis = m_language == nullptr ? "" : state == Code::ClientState::Ready
            ? Loc::TextOr(LocKeys::CodeEditorServerReady, "JBroScript analysis connected") : state == Code::ClientState::Starting
            ? Loc::TextOr(LocKeys::CodeEditorServerStarting, "Connecting JBroScript analysis...")
            : Loc::TextOr(LocKeys::CodeEditorServerFailed, "JBroScript analysis disconnected");
        Widget::HintTextF("%s%s%s", status, m_language == nullptr ? "" : "  |  ", analysis);
        Widget::HoveredTooltip(m_path.c_str());
    }
    Bool CodeEditorPanel::IsReadOnly() const
    {
        return m_readOnly || (m_editor != nullptr && m_editor->IsAtScriptBreak());
    }
    Bool CodeEditorPanel::ApplySourceEdits(Int32 version, const Array<Code::TextEdit>& edits)
    {
        if (m_buffer.GetRevision() != m_revision)
        {
            Changed();
        }
        if (IsReadOnly() || m_language == nullptr || m_language->GetVersion(m_path) != version)
        {
            return false;
        }
        m_buffer.SetReadOnly(IsReadOnly());
        if (!m_buffer.ApplyEdits(edits))
        {
            return false;
        }
        Changed();
        return true;
    }
    CodeEditorPanel* CodeEditorPanel::FindActive(EditorApplication& editor)
    {
        Array<EditorPanel*> panels;
        editor.FindPanels(TypeName, panels);
        CodeEditorPanel* best = nullptr;
        for (auto* panel : panels)
        {
            auto* code = static_cast<CodeEditorPanel*>(panel);
            if (code->IsOpen() && (best == nullptr || code->m_focusSerial > best->m_focusSerial))
            {
                best = code;
            }
        }
        return best;
    }
    void CodeEditorPanel::OnMenuBar()
    {
        EditorActionContext context;
        context.editor = m_editor;
        context.panel = this;
        if (Widget::BeginMenu(Loc::TextOr(LocKeys::MenuFile, "File")))
        {
            EditorActionUi::DrawItem("code_editor.save", context);
            EditorActionUi::DrawItem("code_editor.reload", context);
            EditorActionUi::DrawItem("code_editor.external", context);
            EditorActionUi::DrawItem("code_editor.close", context);
            Widget::EndMenu();
        }
        if (Widget::BeginMenu(Loc::TextOr(LocKeys::MenuEdit, "Edit")))
        {
            EditorActionUi::DrawItem("code_editor.undo", context);
            EditorActionUi::DrawItem("code_editor.redo", context);
            ImGui::Separator();
            EditorActionUi::DrawItem("code_editor.cut", context);
            EditorActionUi::DrawItem("code_editor.copy", context);
            EditorActionUi::DrawItem("code_editor.paste", context);
            ImGui::Separator();
            EditorActionUi::DrawItem("code_editor.select_all", context);
            Widget::EndMenu();
        }
    }
    void CodeEditorPanel::RequestClose()
    {
        if (HasUnsavedChanges())
        {
            m_editor->OpenPopup(MakeOwnerPtr<CodeFileDecision>(GetId(), false));
        }
        else
        {
            m_editor->ClosePanel(*this);
        }
    }
    void CodeEditorPanel::RequestReload()
    {
        if (HasUnsavedChanges())
        {
            m_editor->OpenPopup(MakeOwnerPtr<CodeFileDecision>(GetId(), true));
        }
        else
        {
            Reload();
        }
    }
    Bool CodeEditorPanel::OnCloseRequested() { RequestClose(); return false; }
    void CodeEditorPanel::Undo()
    {
        if (!IsReadOnly())
        {
            m_buffer.SetReadOnly(IsReadOnly());
            m_buffer.Undo();
            Changed();
        }
    }
    void CodeEditorPanel::Redo()
    {
        if (!IsReadOnly())
        {
            m_buffer.SetReadOnly(IsReadOnly());
            m_buffer.Redo();
            Changed();
        }
    }
    void CodeEditorPanel::Cut()
    {
        if (!IsReadOnly() && m_buffer.HasSelection())
        {
            m_buffer.SetReadOnly(false);
            m_buffer.Cut();
            if (m_buffer.GetRevision() != m_revision)
            {
                Changed();
            }
        }
    }
    void CodeEditorPanel::Paste()
    {
        if (!IsReadOnly() && m_buffer.CanPaste())
        {
            m_buffer.SetReadOnly(false);
            m_buffer.Paste();
            if (m_buffer.GetRevision() != m_revision)
            {
                Changed();
            }
        }
    }
}
