#include <JBro/Editor/Widget/AgentChat.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/Button.h>
#include <JBro/Editor/Widget/List.h>
#include <JBro/Editor/Widget/TextField.h>
#include <JBro/Editor/Widget/FilterCombo.h>
#include <JBro/Editor/Widget/PathField.h>
#include <JBro/Editor/EditorIcons.h>
#include <JBro/Editor/EditorTheme.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <JBro/Types/ValueMath.h>
#include <imgui_internal.h>

namespace JBro::Widget
{
    namespace
    {
        // Chat keeps the editor's hue family, with more legible small text and a recessed composer.
        constexpr ImVec4 Muted(0.608f, 0.659f, 0.725f, 1.0f);
        constexpr ImVec4 Composer(0.082f, 0.106f, 0.133f, 1.0f);
        constexpr ImVec4 Bubble(0.149f, 0.192f, 0.243f, 1.0f);
        constexpr ImVec4 SoftAccent(0.698f, 0.792f, 0.906f, 1.0f);
        Float Scale() { return ImGui::GetStyle().FontScaleMain * ImGui::GetStyle().FontScaleDpi; }
        void Gap(Float height) { ImGui::Dummy(ImVec2(0.0f, height * Scale())); }
        void Small(const char* text, Bool wrap = false)
        {
            const Float size = 11.0f;
            ImGui::PushFont(nullptr, size.Value);
            StyleScope style;
            style.PushColor(ImGuiCol_Text, Muted);
            if (wrap)
            {
                WrappedText(text);
            }
            else
            {
                Text(text);
            }
            ImGui::PopFont();
        }
        void ChatTooltip(const char* text, const ImVec2& padding, ImGuiHoveredFlags flags = ImGuiHoveredFlags_None)
        {
            StyleScope style;
            style.PushVar(ImGuiStyleVar_WindowPadding, padding);
            style.PushColor(ImGuiCol_Text, EditorTheme::Ink);
            ImGui::PushFont(nullptr, 13.0f);
            HoveredTooltip(text, flags);
            ImGui::PopFont();
        }
        Bool IconAction(const char* id, const char* icon, const char* tooltip, const ImVec2& tooltipPadding, Float size = 28.0f)
        {
            StyleScope style;
            style.PushColor(ImGuiCol_Button, EditorTheme::Clear);
            style.PushColor(ImGuiCol_ButtonHovered, EditorTheme::Raised);
            style.PushColor(ImGuiCol_Text, Muted);
            style.PushVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
            const Float extent = size * Scale();
            const Bool pressed = Button(id, icon, ImVec2(extent, extent));
            ChatTooltip(tooltip, tooltipPadding);
            return pressed;
        }
        String FileName(const String& path)
        {
            const auto at = path.find_last_of("/\\");
            return at == String::npos ? path : String(path.substr(at + 1));
        }
        const char* TargetIcon(const Agent::Target& target)
        {
            return target.kind == Agent::Target::Kind::Asset ? Icons::File
                : target.kind == Agent::Target::Kind::Component ? Icons::ViewList : Icons::GameObject;
        }
        void TargetText(const Agent::Target& target, String& name, String& detail)
        {
            name = target.name;
            detail = Loc::Text(target.kind == Agent::Target::Kind::Asset ? LocKeys::AgentAsset
                : target.kind == Agent::Target::Kind::Component ? LocKeys::AgentComponent : LocKeys::AgentObject);
            if (target.kind == Agent::Target::Kind::Component)
            {
                const auto at = name.rfind(" / ");
                if (at != String::npos)
                {
                    detail += " / " + name.substr(0, at);
                    name = name.substr(at + 3);
                }
            }
            else if (target.kind == Agent::Target::Kind::Asset)
            {
                name = FileName(target.name);
                if (name != target.name)
                {
                    detail += " / " + target.name.substr(0, target.name.size() - name.size() - 1);
                }
            }
            else if (!target.canvas.empty())
            {
                detail += " / " + FileName(target.canvas);
            }
        }
        void ClippedText(const char* text, Float width, const ImVec2& tooltipPadding)
        {
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const ImVec2 max(min.x + Max(width, Float(1.0f)), min.y + ImGui::GetTextLineHeight());
            ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), min, max, max.x, text, nullptr, nullptr);
            ImGui::Dummy(ImVec2(Max(width, Float(1.0f)), ImGui::GetTextLineHeight()));
            if (ImGui::CalcTextSize(text).x > width)
            {
                ChatTooltip(text, tooltipPadding);
            }
        }
        Int32 Attachments(const AgentChatView& view, Float height, const ImVec2& tooltipPadding)
        {
            Int32 removed = -1;
            if (view.targets.IsEmpty())
            {
                return removed;
            }
            {
                ImGui::PushFont(nullptr, 11.0f);
                InlineIcon(Icons::Attachment);
                HintTextF(Loc::Text(LocKeys::AgentAttachments), static_cast<UInt32>(view.targets.Size()));
                ImGui::PopFont();
            }
            StyleScope style;
            style.PushVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            style.PushVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
            if (ImGui::BeginChild("##agent.attachments", ImVec2(0.0f, height)))
            {
                ListVirtual("##attachment.rows", static_cast<Int32>(view.targets.Size()), [&](Int32 index) -> Bool {
                    const auto& target = view.targets[index.Value];
                    const Bool valid = view.validate.IsBound() && view.validate.Invoke(target);
                    const Float scale = Scale();
                    const Float row = 43.0f * scale;
                    StyleScope rowStyle;
                    rowStyle.PushVar(ImGuiStyleVar_CellPadding, ImVec2(0.0f, 5.0f * scale));
                    if (ImGui::BeginTable("##reference", 3, ImGuiTableFlags_SizingStretchProp))
                    {
                        ImGui::TableSetupColumn("##icon", ImGuiTableColumnFlags_WidthFixed, 35.0f * scale);
                        ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, 24.0f * scale);
                        ImGui::TableNextRow(ImGuiTableRowFlags_None, row);
                        ImGui::TableSetColumnIndex(0);
                        const ImVec2 iconMin = ImGui::GetCursorScreenPos();
                        ImGui::GetWindowDrawList()->AddRectFilled(iconMin, iconMin + ImVec2(26.0f * scale, 26.0f * scale),
                            ImGui::GetColorU32(EditorTheme::Input), 5.0f * scale);
                        DrawGlyphCentered(TargetIcon(target), iconMin, iconMin + ImVec2(26.0f * scale, 26.0f * scale),
                            ImGui::GetColorU32(valid ? Muted : EditorTheme::Amber));
                        ImGui::Dummy(ImVec2(26.0f * scale, 26.0f * scale));
                        ImGui::TableSetColumnIndex(1);
                        String name, detail;
                        TargetText(target, name, detail);
                        if (!valid)
                        {
                            detail = Loc::Text(LocKeys::AgentInvalidTarget);
                        }
                        const Float textWidth = ImGui::GetContentRegionAvail().x;
                        ClippedText(name.c_str(), textWidth, tooltipPadding);
                        {
                            ImGui::PushFont(nullptr, 10.0f);
                            StyleScope color;
                            color.PushColor(ImGuiCol_Text, valid ? Muted : EditorTheme::Amber);
                            ClippedText(detail.c_str(), textWidth, tooltipPadding);
                            ImGui::PopFont();
                        }
                        ImGui::TableSetColumnIndex(2);
                        if (IconAction("##agent.remove", Icons::Xmark, Loc::Text(LocKeys::AgentRemoveAttachment), tooltipPadding, 24.0f))
                        {
                            removed = index;
                        }
                        ImGui::EndTable();
                    }
                    return false;
                }, [] {}, [](Int32) {}, [](Int32, Int32) {}, ListFlagsPlain | ListFlagsNoReorder);
            }
            ImGui::EndChild();
            ImGui::Separator();
            return removed;
        }
        void GuideCard(const AgentChatView& view, AgentChatResult& result)
        {
            if (!view.guidePaused)
            {
                return;
            }
            Gap(4.0f);
            StyleScope style;
            style.PushColor(ImGuiCol_ChildBg, EditorTheme::Raised);
            style.PushVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * Scale(), 10.0f * Scale()));
            style.PushVar(ImGuiStyleVar_ChildRounding, 6.0f * Scale());
            if (ImGui::BeginChild("##agent.guide", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding))
            {
                Small(Loc::Text(LocKeys::AgentGuide));
                WrappedText(view.guideTitle.c_str());
                if (!view.guideTarget.empty())
                {
                    Small(view.guideTarget.c_str(), true);
                }
                Gap(3.0f);
                StyleScope actionStyle;
                actionStyle.PushColor(ImGuiCol_Button, ImVec4(0.161f, 0.243f, 0.333f, 1.0f));
                actionStyle.PushColor(ImGuiCol_Text, SoftAccent);
                if (Button(Loc::Text(LocKeys::AgentResumeGuide), Icons::Play, ImVec2(-FLT_MIN, 30.0f * Scale())))
                {
                    result.action = AgentChatAction::ResumeGuide;
                }
            }
            ImGui::EndChild();
            Small(Loc::Text(LocKeys::AgentGuidePaused), true);
        }
        void History(const AgentChatView& view, Float height, String& input, AgentChatResult& result)
        {
            StyleScope style;
            style.PushVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f * Scale(), 12.0f * Scale()));
            if (ImGui::BeginChild("##agent.history", ImVec2(0.0f, height), ImGuiChildFlags_AlwaysUseWindowPadding))
            {
                const Bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 3.0f;
                auto* storage = ImGui::GetStateStorage();
                const auto followId = ImGui::GetID("##agent.follow_tail");
                const auto heightId = ImGui::GetID("##agent.scroll_height");
                const Float scrollHeight = ImGui::GetScrollMaxY();
                const Bool heightChanged = storage->GetFloat(heightId, -1.0f) != scrollHeight;
                storage->SetFloat(heightId, scrollHeight.Value);
                Bool follow = storage->GetBool(followId, true);
                if (ImGui::IsWindowHovered() && (ImGui::GetIO().MouseWheel != 0.0f || ImGui::IsMouseDragging(ImGuiMouseButton_Left)))
                {
                    follow = atBottom;
                }
                storage->SetBool(followId, follow.Value);
                Array<Int32> visible;
                for (UInt32 i = 0; i < view.messages.Size(); ++i)
                {
                    if (view.messages[i.Value].role != Agent::MessageRole::Activity)
                    {
                        visible.Add(static_cast<Int32>(i));
                    }
                }
                if (visible.IsEmpty() && !view.guidePaused)
                {
                    if (view.changed)
                    {
                        ImGui::SetScrollY(0.0f);
                    }
                    Gap(40.0f);
                    InlineIcon(Icons::Agent);
                    Text(Loc::Text(LocKeys::AgentWelcome));
                    Small(Loc::Text(LocKeys::AgentWelcomeHint), true);
                    Gap(18.0f);
                    const char* prompts[] = {LocKeys::AgentPromptFocus, LocKeys::AgentPromptOrder, LocKeys::AgentPromptScript};
                    const char* labels[] = {LocKeys::AgentPromptFocusLabel, LocKeys::AgentPromptOrderLabel, LocKeys::AgentPromptScriptLabel};
                    for (UInt32 index = 0; index < 3; ++index)
                    {
                        if (Button(Loc::Text(labels[index.Value]), Icons::Question, ImVec2(-FLT_MIN, 32.0f * Scale())))
                        {
                            input = Loc::Text(prompts[index.Value]);
                        }
                    }
                }
                ListVirtual("##history.rows", static_cast<Int32>(visible.Size()), [&](Int32 row) -> Bool {
                    const auto& message = view.messages[visible[row.Value].Value];
                    const Bool user = message.role == Agent::MessageRole::User;
                    if (user)
                    {
                        ImGui::Indent(20.0f * Scale());
                    }
                    StyleScope bubble;
                    bubble.PushVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * Scale(), 10.0f * Scale()));
                    bubble.PushVar(ImGuiStyleVar_ChildRounding, 7.0f * Scale());
                    bubble.PushColor(ImGuiCol_ChildBg, user ? Bubble : EditorTheme::Clear);
                    if (ImGui::BeginChild("##message", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding))
                    {
                        if (!user)
                        {
                            InlineIcon(message.role == Agent::MessageRole::Error ? Icons::Error : Icons::Agent);
                            Small(Loc::Text(LocKeys::PanelAgent));
                            Gap(2.0f);
                        }
                        if (message.role == Agent::MessageRole::Error)
                        {
                            SeverityTextF(Severity::Error, "%s", message.text.c_str());
                        }
                        else
                        {
                            WrappedText(message.text.c_str());
                        }
                        ListVirtual("##targets", static_cast<Int32>(message.targets.Size()), [&](Int32 index) -> Bool {
                            const auto& target = message.targets[index.Value];
                            InlineIcon(TargetIcon(target));
                            Small(target.name.c_str(), true);
                            if (!view.validate.IsBound() || !view.validate.Invoke(target))
                            {
                                Small(Loc::Text(LocKeys::AgentInvalidTarget), true);
                            }
                            return false;
                        }, [] {}, [](Int32) {}, [](Int32, Int32) {}, ListFlagsPlain);
                    }
                    ImGui::EndChild();
                    if (user)
                    {
                        ImGui::Unindent(20.0f * Scale());
                    }
                    Gap(12.0f);
                    return false;
                }, [] {}, [](Int32) {}, [](Int32, Int32) {}, ListFlagsPlain);
                GuideCard(view, result);
                if ((view.changed || heightChanged) && follow && (!visible.IsEmpty() || view.guidePaused))
                {
                    ImGui::SetScrollHereY(1.0f);
                }
            }
            ImGui::EndChild();
        }
        void Settings(const AgentChatView& view, String& executable, String& apiKey, AgentChatResult& result)
        {
            if (ImGui::BeginChild("##agent.settings"))
            {
                Gap(10.0f);
                Text(Loc::Text(LocKeys::AgentSettings));
                Gap(18.0f);
                Small(Loc::Text(LocKeys::AgentExecutable));
                if (PathField("##agent.executable", executable).Draw().browse)
                {
                    result.action = AgentChatAction::Browse;
                }
                Small(Loc::Text(LocKeys::AgentExecutableHint), true);
                Gap(16.0f);
                ImGui::Separator();
                Gap(12.0f);
                if (view.connection == Agent::Connection::Stopped || view.connection == Agent::Connection::Failed)
                {
                    if (ActionButton(Loc::Text(LocKeys::AgentConnect), Severity::Info, view.projectOpen,
                        Loc::Text(LocKeys::AgentProjectRequired), {}, Icons::Play))
                    {
                        result.action = AgentChatAction::Connect;
                    }
                    if (Button(Loc::Text(LocKeys::AgentInstall), Icons::Question))
                    {
                        result.action = AgentChatAction::Install;
                    }
                }
                else if (view.connection == Agent::Connection::SignedOut)
                {
                    if (Button(Loc::Text(LocKeys::AgentLoginChatgpt), Icons::OpenExternal))
                    {
                        result.action = AgentChatAction::LoginChatGPT;
                    }
                    Gap(12.0f);
                    Small(Loc::Text(LocKeys::AgentApiKey));
                    TextField("##agent.api_key", apiKey).Password().MaxLength(2047)();
                    if (ActionButton(Loc::Text(LocKeys::AgentLoginKey), Severity::Info, !apiKey.empty(), nullptr, {}, Icons::OpenExternal))
                    {
                        result.action = AgentChatAction::LoginKey;
                    }
                }
                else
                {
                    Small(Loc::Text(LocKeys::AgentAuthManaged), true);
                }
                Gap(16.0f);
                if (view.connection != Agent::Connection::Stopped && Button(Loc::Text(LocKeys::AgentDisconnect), Icons::Xmark))
                {
                    result.action = AgentChatAction::Disconnect;
                }
                if (!view.error.empty())
                {
                    SeverityTextF(Severity::Error, "%s", view.error.c_str());
                }
            }
            ImGui::EndChild();
        }
    }
    AgentChatResult AgentChat(const AgentChatView& view, Bool& settingsOpen, String& input, String& executable, String& apiKey)
    {
        AgentChatResult result;
        const Float scale = Scale();
        const ImVec2 popupItemSpacing = ImGui::GetStyle().ItemSpacing;
        ImGui::PushFont(nullptr, 13.0f);
        StyleScope style;
        style.PushVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * scale, 3.0f * scale));
        style.PushVar(ImGuiStyleVar_FrameRounding, 4.0f * scale);
        style.PushColor(ImGuiCol_TextDisabled, Muted);
        const Float width = ImGui::GetContentRegionAvail().x;
        const ImVec2 tooltipPadding = ImGui::GetStyle().WindowPadding;
        const Float left = ImGui::GetCursorPosX();
        const Float top = ImGui::GetCursorPosY();
        ClippedText(view.project.empty() ? Loc::Text(LocKeys::PanelAgent) : view.project.c_str(), width - 62.0f * scale, tooltipPadding);
        ImGui::SameLine();
        ImGui::SetCursorPos(ImVec2(left + width - 58.0f * scale, top));
        if (IconAction("##agent.new", Icons::Plus, Loc::Text(LocKeys::AgentNewChat), tooltipPadding))
        {
            result.action = AgentChatAction::NewChat;
        }
        ImGui::SameLine(0.0f, 2.0f * scale);
        if (IconAction("##agent.settings_toggle", settingsOpen ? Icons::Back : Icons::Settings,
            Loc::Text(settingsOpen ? LocKeys::AgentBack : LocKeys::AgentSettings), tooltipPadding))
        {
            settingsOpen = !settingsOpen;
        }
        const char* status = Loc::Text(view.connection == Agent::Connection::Ready || view.connection == Agent::Connection::Running
            ? LocKeys::AgentConnectedShort : view.connection == Agent::Connection::Starting ? LocKeys::AgentConnecting
            : view.connection == Agent::Connection::SignedOut ? LocKeys::AgentSignInRequired : LocKeys::AgentDisconnectedShort);
        String statusLine(status);
        if (!view.canvas.empty())
        {
            statusLine += " / " + view.canvas;
        }
        const ImVec2 statusStart = ImGui::GetCursorScreenPos();
        const Bool connected = view.connection == Agent::Connection::Ready || view.connection == Agent::Connection::Running;
        ImGui::GetWindowDrawList()->AddCircleFilled(statusStart + ImVec2(3.0f * scale, 6.0f * scale),
            2.5f * scale, ImGui::GetColorU32(connected ? EditorTheme::Success : Muted));
        ImGui::SetCursorPosX(left + 12.0f * scale);
        {
            ImGui::PushFont(nullptr, 11.0f);
            StyleScope statusStyle;
            statusStyle.PushColor(ImGuiCol_Text, Muted);
            ClippedText(statusLine.c_str(), width - 12.0f * scale, tooltipPadding);
            ImGui::PopFont();
        }
        Gap(8.0f);
        ImGui::Separator();
        if (settingsOpen)
        {
            Settings(view, executable, apiKey, result);
            ImGui::PopFont();
            return result;
        }
        const Bool running = view.connection == Agent::Connection::Running;
        Bool valid = true;
        for (const auto& target : view.targets)
        {
            valid = valid && view.validate.IsBound() && view.validate.Invoke(target);
        }
        const Float available = ImGui::GetContentRegionAvail().y;
        TextField inputField("##agent.input", input);
        const Float inputWidth = Max(width - 20.0f * scale, Float(1.0f));
        inputField.Multiline(true, 1.0f).WordWrap().MaxVisibleLines(8.0f).Width(inputWidth).CommitOnEnter().LiveText();
        Bool submitted = false;
        const ImVec2 inputPadding(4.0f * scale, 3.0f * scale);
        const Float composerPaddingY = 6.0f * scale;
        const Float toolsHeight = 28.0f * scale;
        const Float inputToTools = 4.0f * scale;
        Float inputHeight;
        {
            StyleScope measurement;
            measurement.PushVar(ImGuiStyleVar_FramePadding, inputPadding);
            inputHeight = inputField.GetMultilineHeight(inputWidth);
        }
        const Float refsHeight = view.targets.IsEmpty() ? Float(0.0f)
            : Min(Float(view.targets.Size()) * 43.0f * scale, Max(Float(43.0f) * scale, available * 0.28f));
        const Float attachmentHeight = view.targets.IsEmpty() ? Float(0.0f) : refsHeight + 14.0f * scale + 1.0f;
        const Bool notice = running || !view.attachmentError.empty() || !view.error.empty() || !valid;
        const Float noticeHeight = notice ? 32.0f * scale : Float(0.0f);
        const Bool needsConnection = !view.projectOpen || !connected;
        const Bool canConnect = view.projectOpen && (view.connection == Agent::Connection::Stopped || view.connection == Agent::Connection::Failed);
        const Bool needsLogin = view.projectOpen && view.connection == Agent::Connection::SignedOut;
        const Float connectionHeight = needsConnection ? (canConnect || needsLogin ? Float(54.0f) : Float(32.0f)) * scale : Float(0.0f);
        const Float composerHeight = inputHeight + attachmentHeight + toolsHeight + inputToTools
            + composerPaddingY * 2.0f + ImGui::GetStyle().ChildBorderSize * 2.0f;
        const Float historyHeight = Max(Float(30.0f) * scale, available - composerHeight - noticeHeight - connectionHeight - 25.0f * scale);
        History(view, historyHeight, input, result);
        if (needsConnection)
        {
            if (ImGui::BeginChild("##agent.connection", ImVec2(0.0f, connectionHeight)))
            {
                Small(Loc::Text(!view.projectOpen ? LocKeys::AgentProjectRequired
                    : needsLogin ? LocKeys::AgentSignInRequired
                    : canConnect ? LocKeys::AgentConnectHint : LocKeys::AgentConnecting), true);
                if (canConnect && Button(Loc::Text(LocKeys::AgentConnect), Icons::Play))
                {
                    result.action = AgentChatAction::Connect;
                }
                if (needsLogin && Button(Loc::Text(LocKeys::AgentSettings), Icons::Settings))
                {
                    settingsOpen = true;
                }
            }
            ImGui::EndChild();
        }
        if (notice)
        {
            const String progress = !view.messages.IsEmpty() && view.messages[view.messages.Size() - 1].role == Agent::MessageRole::Activity
                ? view.messages[view.messages.Size() - 1].text : String(Loc::Text(LocKeys::AgentToolRunning));
            const String message = !view.attachmentError.empty() ? view.attachmentError
                : !valid ? String(Loc::Text(LocKeys::AgentInvalidTarget))
                : !view.error.empty() ? view.error : progress;
            if (ImGui::BeginChild("##agent.notice", ImVec2(0.0f, noticeHeight)))
            {
                Small(message.c_str(), true);
            }
            ImGui::EndChild();
        }
        style.PushColor(ImGuiCol_ChildBg, Composer);
        style.PushColor(ImGuiCol_Border, EditorTheme::LineStrong);
        style.PushVar(ImGuiStyleVar_ChildRounding, 7.0f * scale);
        style.PushVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * scale, composerPaddingY));
        if (ImGui::BeginChild("##agent.composer", ImVec2(0.0f, composerHeight), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding))
        {
            result.removed = Attachments(view, refsHeight, tooltipPadding);
            {
                StyleScope fieldStyle;
                fieldStyle.PushColor(ImGuiCol_FrameBg, EditorTheme::Clear);
                fieldStyle.PushColor(ImGuiCol_Border, EditorTheme::Clear);
                fieldStyle.PushVar(ImGuiStyleVar_FramePadding, inputPadding);
                DisableScope disabled(running);
                submitted = inputField();
                if (input.empty() && !ImGui::IsItemActive())
                {
                    ImGui::GetWindowDrawList()->AddText(ImGui::GetItemRectMin() + ImVec2(4.0f * scale, 3.0f * scale),
                        ImGui::GetColorU32(Muted), Loc::Text(LocKeys::AgentInputHint));
                }
            }
            if (view.receiveDrop.IsBound())
            {
                view.receiveDrop.Invoke();
            }
            const Float toolTop = ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y + inputToTools;
            const Float toolLeft = ImGui::GetCursorPosX();
            const Float toolWidth = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosY(toolTop + 2.0f * scale);
            ImGui::PushFont(nullptr, 16.0f);
            if (ActionButton("##agent.attach", Severity::Info, view.projectOpen,
                nullptr, ImVec2(24.0f * scale, 24.0f * scale), Icons::Plus))
            {
                result.action = AgentChatAction::AttachFile;
            }
            ImGui::PopFont();
            ChatTooltip(Loc::Text(view.projectOpen ? LocKeys::AgentAttachFile : LocKeys::AgentProjectRequired),
                tooltipPadding, ImGuiHoveredFlags_AllowWhenDisabled);
            ImGui::SameLine(0.0f, 6.0f * scale);
            {
                ImGui::SetCursorPosY(toolTop + (toolsHeight - ImGui::GetFrameHeight()) * 0.5f);
                Array<const char*> labels;
                Int32 selected = -1;
                Float widestModel = 0.0f;
                for (const auto& model : view.models)
                {
                    if (model.id == view.model)
                    {
                        selected = static_cast<Int32>(labels.Size());
                    }
                    labels.Add(model.label.c_str());
                    widestModel = Max(widestModel, Float(ImGui::CalcTextSize(model.label.c_str()).x));
                }
                const char* modelLabel = selected >= 0 ? labels[selected.Value] : Loc::Text(LocKeys::AgentModelPlaceholder);
                if (view.connection == Agent::Connection::Ready && !labels.IsEmpty())
                {
                    StyleScope modelStyle;
                    modelStyle.PushVar(ImGuiStyleVar_WindowPadding, tooltipPadding);
                    modelStyle.PushVar(ImGuiStyleVar_ItemSpacing, popupItemSpacing);
                    const Float modelWidth = Min(widestModel + 30.0f * scale,
                        Min(Float(160.0f) * scale, Max(Float(50.0f) * scale, toolWidth - 72.0f * scale)));
                    if (FilterCombo("##agent.model", labels.View(), selected)
                        .EmptyText(Loc::Text(LocKeys::AgentModelPlaceholder)).Width(modelWidth)())
                    {
                        result.model = selected;
                    }
                    ChatTooltip(modelLabel, tooltipPadding);
                }
                else
                {
                    StyleScope modelStyle;
                    modelStyle.PushColor(ImGuiCol_Text, Muted);
                    ImGui::AlignTextToFramePadding();
                    ClippedText(connected ? modelLabel : Loc::Text(LocKeys::AgentModelAfterConnect), toolWidth - 72.0f * scale, tooltipPadding);
                }
            }
            ImGui::SetCursorPos(ImVec2(toolLeft + toolWidth - toolsHeight, toolTop));
            const Bool enabled = running || (view.connection == Agent::Connection::Ready && !input.empty() && valid);
            {
                ImGui::PushFont(nullptr, 16.0f);
                StyleScope sendStyle;
                sendStyle.PushColor(ImGuiCol_Button, SoftAccent);
                sendStyle.PushColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.84f, 0.95f, 1.0f));
                sendStyle.PushColor(ImGuiCol_ButtonActive, ImVec4(0.55f, 0.67f, 0.81f, 1.0f));
                sendStyle.PushColor(ImGuiCol_Text, Composer);
                if (ActionButton(running ? "##agent.stop" : "##agent.send", Severity::Info, enabled,
                    nullptr, ImVec2(toolsHeight, toolsHeight), running ? Icons::Stop : Icons::Send))
                {
                    result.action = running ? AgentChatAction::Stop : AgentChatAction::Send;
                }
                ImGui::PopFont();
            }
            ChatTooltip(Loc::Text(!valid ? LocKeys::AgentInvalidTarget
                : !connected ? LocKeys::AgentNotReady
                : !enabled ? LocKeys::AgentInputHint
                : running ? LocKeys::AgentStop : LocKeys::AgentSend), tooltipPadding, ImGuiHoveredFlags_AllowWhenDisabled);
            if (enabled && !running && submitted)
            {
                result.action = AgentChatAction::Send;
            }
        }
        ImGui::EndChild();
        if (view.receiveDrop.IsBound())
        {
            view.receiveDrop.Invoke();
        }
        Small(Loc::Text(LocKeys::AgentDropHint));
        const char* shortcut = Loc::Text(LocKeys::AgentShortcutHint);
        ImGui::PushFont(nullptr, 11.0f);
        const Float shortcutWidth = ImGui::CalcTextSize(shortcut).x;
        ImGui::PopFont();
        if (width >= ImGui::GetItemRectSize().x + shortcutWidth + 12.0f * scale)
        {
            ImGui::SameLine();
            ImGui::SetCursorPosX(left + width - shortcutWidth);
            Small(shortcut);
        }
        ImGui::PopFont();
        return result;
    }
}
