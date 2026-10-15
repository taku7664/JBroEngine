#include "CodeEditorPanel.h"
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Editor/EditorActionRegistry.h>
#include <JBro/Editor/EditorPopup.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Platform/Platform.h>
#include <algorithm>
#include <cstring>
#include <cctype>

namespace JBro
{
    namespace
    {
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
                if (Widget::Button(Loc::TextOr(LocKeys::CommonCancel, "Cancel")))
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
        save.CanExecute = [](const EditorActionContext& context) -> Bool {
            return context.panel != nullptr && static_cast<CodeEditorPanel*>(context.panel)->HasUnsavedChanges();
        };
        save.Execute = [](EditorActionContext& context) -> Bool { return static_cast<CodeEditorPanel*>(context.panel)->Save(); };
        EditorActionRegistry::Get().Register(save);
        EditorActionInfo undo = save;
        undo.name = "code_editor.undo";
        undo.labelKey = LocKeys::MenuUndo;
        undo.fallbackLabel = "Undo";
        undo.primary = {ImGuiKey_Z, true};
        undo.CanExecute = [](const EditorActionContext& context) -> Bool {
            return context.panel != nullptr && static_cast<CodeEditorPanel*>(context.panel)->m_buffer.CanUndo();
        };
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
        redo.CanExecute = [](const EditorActionContext& context) -> Bool {
            return context.panel != nullptr && static_cast<CodeEditorPanel*>(context.panel)->m_buffer.CanRedo();
        };
        redo.Execute = [](EditorActionContext& context) -> Bool {
            static_cast<CodeEditorPanel*>(context.panel)->Redo();
            return true;
        };
        EditorActionRegistry::Get().Register(redo);
    }
    Bool CodeEditorPanel::OnCreate(EditorApplication& editor)
    {
        m_editor = &editor;
        return true;
    }
    void CodeEditorPanel::OnDestroy()
    {
        if (m_language != nullptr)
        {
            m_language->Close(m_path);
        }
    }
    Bool CodeEditorPanel::Open(const String& path, UInt32 line, UInt32 column)
    {
        m_path = Code::LanguageClient::NormalizePath(path);
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
            m_language = &m_editor->GetCodeLanguageClient();
            m_language->Open(m_path, m_text);
        }
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
        m_buffer.SetText(m_text);
        m_saved = m_text;
        m_revision = m_buffer.GetRevision();
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
        m_text = m_buffer.GetText();
        m_revision = m_buffer.GetRevision();
        m_buffer.ClearAnalysis();
        m_diagnostics.Clear();
        if (m_language != nullptr)
        {
            m_language->Change(m_path, m_text);
        }
        UpdateTitle();
    }
    void CodeEditorPanel::OnUpdate(Float)
    {
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
    Bool CodeEditorPanel::SaveChanges() { return !HasUnsavedChanges() || Save(); }
    Bool CodeEditorPanel::Save()
    {
        if (m_readOnly)
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
        JArrayView<std::byte> data{reinterpret_cast<const std::byte*>(bytes.data()), static_cast<std::uint32_t>(bytes.size())};
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
    void CodeEditorPanel::FocusAt(UInt32 line, UInt32 column)
    {
        m_focus = true;
        m_focusLine = line > 0 ? line - 1 : UInt32(0);
        m_focusColumn = column;
        RequestFocus();
    }
    void CodeEditorPanel::OnDraw()
    {
        Widget::HintText(m_path.c_str());
        if (!m_message.empty())
        {
            Widget::SeverityTextF(Widget::Severity::Error, "%s", m_message.c_str());
        }
        if (m_language != nullptr)
        {
            const auto state = m_language->GetState();
            Widget::HintText(state == Code::ClientState::Ready ? Loc::TextOr(LocKeys::CodeEditorServerReady, "JBroScript analysis connected")
                : state == Code::ClientState::Starting ? Loc::TextOr(LocKeys::CodeEditorServerStarting, "Connecting JBroScript analysis...")
                : Loc::TextOr(LocKeys::CodeEditorServerFailed, "JBroScript analysis disconnected"));
            if (state == Code::ClientState::Failed && Widget::Button(Loc::TextOr(LocKeys::CodeEditorReconnect, "Reconnect language server")))
            {
                m_editor->ReconnectCodeLanguageClient();
            }
        }
        const auto cursor = m_buffer.GetCursor();
        Widget::HintTextF(Loc::TextOr(LocKeys::CodeEditorCursor, "Line %u, Column %u | %u diagnostics"), cursor.line + 1, cursor.character + 1, static_cast<std::uint32_t>(m_diagnostics.Size()));
        for (std::size_t i = 0; i < std::min<std::size_t>(m_diagnostics.Size(), 3); ++i)
        {
            const auto& diagnostic = m_diagnostics[i];
            Widget::SeverityTextF(diagnostic.severity == 1 ? Widget::Severity::Error : Widget::Severity::Warning, "%u: %s %s",
                diagnostic.range.start.line + 1, diagnostic.code.c_str(), diagnostic.message.c_str());
        }
        if (m_focus)
        {
            m_buffer.Focus(m_focusLine, m_focusColumn);
            m_focus = false;
        }
        if (m_buffer.Render("##CodeBuffer"))
        {
            Changed();
        }
    }
    void CodeEditorPanel::OnMenuBar()
    {
        EditorActionContext context;
        context.editor = m_editor;
        context.panel = this;
        if (Widget::BeginMenu(Loc::TextOr(LocKeys::MenuFile, "File")))
        {
            EditorActionUi::DrawItem("code_editor.save", context);
            if (Widget::MenuItem(Loc::TextOr(LocKeys::CodeEditorReload, "Reload from disk")))
            {
                RequestReload();
            }
            if (Widget::MenuItem(Loc::TextOr(LocKeys::CodeEditorExternal, "Open in external editor")))
            {
                m_editor->OpenCodeFileExternally(m_path);
            }
            if (Widget::MenuItem(Loc::TextOr(LocKeys::CodeEditorClose, "Close")))
            {
                RequestClose();
            }
            Widget::EndMenu();
        }
        if (Widget::BeginMenu(Loc::TextOr(LocKeys::MenuEdit, "Edit")))
        {
            EditorActionUi::DrawItem("code_editor.undo", context);
            EditorActionUi::DrawItem("code_editor.redo", context);
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
    void CodeEditorPanel::Undo() { m_buffer.Undo(); }
    void CodeEditorPanel::Redo() { m_buffer.Redo(); }
}
