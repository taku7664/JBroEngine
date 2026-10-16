#include <JBro/Editor/Widget/AgentChat.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Widget/List.h>
#include <JBro/Editor/EditorIcons.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <imgui.h>
#include <JBro/Types/ValueMath.h>

namespace JBro::Widget
{
    void AgentHistory(const Array<Agent::Message>& messages, Bool changed, Delegate<Bool(const Agent::Target&)> validate, Float reservedHeight)
    {
        const Float height = Max(Float(100.0f), Float(ImGui::GetContentRegionAvail().y - reservedHeight.Value));
        if (ImGui::BeginChild("##agent.history", ImVec2(0.0f, height), ImGuiChildFlags_Borders))
        {
            const Bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 3.0f;
            auto* storage = ImGui::GetStateStorage();
            const auto followId = ImGui::GetID("##agent.follow_tail");
            Bool follow = storage->GetBool(followId, true);
            if (ImGui::IsWindowHovered() && (ImGui::GetIO().MouseWheel != 0.0f || ImGui::IsMouseDragging(ImGuiMouseButton_Left)))
            {
                follow = atBottom;
            }
            storage->SetBool(followId, follow.Value);
            ListVirtual("##history.rows", static_cast<Int32>(messages.Size()), [&](Int32 index) -> Bool {
                const auto& message = messages[static_cast<std::size_t>(index)];
                const char* key = message.role == Agent::MessageRole::User ? LocKeys::AgentUser : message.role == Agent::MessageRole::Assistant ? LocKeys::AgentAssistant : LocKeys::AgentActivity;
                const char* fallback = message.role == Agent::MessageRole::User ? "You" : message.role == Agent::MessageRole::Assistant ? "AI" : "Activity";
                InlineIcon(message.role == Agent::MessageRole::Error ? Icons::Error : message.role == Agent::MessageRole::User ? Icons::Question : Icons::Info);
                HintText(Loc::TextOr(key, fallback));
                if (message.role == Agent::MessageRole::Error)
                {
                    SeverityTextF(Severity::Error, "%s", message.text.c_str());
                }
                else
                {
                    WrappedText(message.text.c_str());
                }
                if (!message.targets.IsEmpty())
                {
                    HintText(Loc::TextOr(LocKeys::AgentAttached, "Included at send time:"));
                    ListVirtual("##targets", static_cast<Int32>(message.targets.Size()), [&](Int32 targetIndex) -> Bool {
                        const auto& target = message.targets[static_cast<std::size_t>(targetIndex)];
                        InlineIcon(target.kind == Agent::Target::Kind::Asset ? Icons::File : target.kind == Agent::Target::Kind::Component ? Icons::ViewList : Icons::GameObject);
                        WrappedText(target.name.c_str());
                        if (!validate.IsBound() || !validate.Invoke(target))
                        {
                            SeverityTextF(Severity::Warning, "%s", Loc::TextOr(LocKeys::AgentInvalidTarget, "This reference is no longer valid."));
                        }
                        return false;
                    }, [] {}, [](Int32) {}, [](Int32, Int32) {}, ListFlagsPlain);
                }
                ImGui::Spacing();
                ImGui::Separator();
                return false;
            }, [] {}, [](Int32) {}, [](Int32, Int32) {}, ListFlagsPlain | (changed ? 0u : ListFlagsClipRows));
            if (changed && follow)
            {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();
    }
    Int32 AgentAttachments(const Array<Agent::Target>& targets, Delegate<Bool(const Agent::Target&)> validate)
    {
        Int32 removed = -1;
        if (!targets.IsEmpty())
        {
            HintText(Loc::TextOr(LocKeys::AgentAttachments, "Attachments"));
            const Float height = Min(Float(166.0f), Float(static_cast<Float>(targets.Size()) * ImGui::GetTextLineHeightWithSpacing() * 4.0f));
            if (ImGui::BeginChild("##agent.attachments", ImVec2(0.0f, height.Value), ImGuiChildFlags_Borders))
            {
                ListVirtual("##attachment.rows", static_cast<Int32>(targets.Size()), [&](Int32 index) -> Bool {
                    const auto& target = targets[static_cast<std::size_t>(index)];
                    const char* kind = target.kind == Agent::Target::Kind::Asset ? LocKeys::AgentAsset
                        : target.kind == Agent::Target::Kind::Component ? LocKeys::AgentComponent : LocKeys::AgentObject;
                    InlineIcon(target.kind == Agent::Target::Kind::Asset ? Icons::File : target.kind == Agent::Target::Kind::Component ? Icons::ViewList : Icons::GameObject);
                    HintText(Loc::Text(kind));
                    ImGui::SameLine();
                    if (Button(Loc::TextOr(LocKeys::AgentRemoveAttachment, "Remove"), Icons::Xmark))
                    {
                        removed = index;
                    }
                    WrappedText(target.name.c_str());
                    if (!validate.IsBound() || !validate.Invoke(target))
                    {
                        SeverityTextF(Severity::Warning, "%s", Loc::TextOr(LocKeys::AgentInvalidTarget, "This reference is no longer valid."));
                    }
                    return false;
                }, [] {}, [](Int32) {}, [](Int32, Int32) {}, ListFlagsPlain | ListFlagsNoReorder);
            }
            ImGui::EndChild();
        }
        ActionButton(Loc::TextOr(LocKeys::AgentDropHere, "Drop to attach"), Severity::Info, true, nullptr,
            ImVec2(-FLT_MIN, ImGui::GetFrameHeight() * 1.5f), Icons::Plus);
        return removed;
    }

}
