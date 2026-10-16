#include <JBro/Editor/Widget/AgentChat.h>
#include <JBro/Editor/Widget/Basic.h>
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
            for (const auto& message : messages)
            {
                const char* key = message.role == Agent::MessageRole::User ? LocKeys::AgentUser : message.role == Agent::MessageRole::Assistant ? LocKeys::AgentAssistant : LocKeys::AgentActivity;
                const char* fallback = message.role == Agent::MessageRole::User ? "You" : message.role == Agent::MessageRole::Assistant ? "AI" : "Activity";
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
                    for (const auto& target : message.targets)
                    {
                        WrappedText(target.name.c_str());
                        if (!validate.IsBound() || !validate.Invoke(target))
                        {
                            SeverityTextF(Severity::Warning, "%s", Loc::TextOr(LocKeys::AgentInvalidTarget, "This reference is no longer valid."));
                        }
                    }
                }
                ImGui::Spacing();
                ImGui::Separator();
            }
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
                for (std::size_t i = 0; i < targets.Size(); ++i)
                {
                    ImGui::PushID(static_cast<int>(i));
                    const auto& target = targets[i];
                    const char* kind = target.kind == Agent::Target::Kind::Asset ? LocKeys::AgentAsset
                        : target.kind == Agent::Target::Kind::Component ? LocKeys::AgentComponent : LocKeys::AgentObject;
                    HintText(Loc::Text(kind));
                    ImGui::SameLine();
                    if (Button(Loc::TextOr(LocKeys::AgentRemoveAttachment, "Remove")))
                    {
                        removed = static_cast<Int32>(i);
                    }
                    WrappedText(target.name.c_str());
                    if (!validate.IsBound() || !validate.Invoke(target))
                    {
                        SeverityTextF(Severity::Warning, "%s", Loc::TextOr(LocKeys::AgentInvalidTarget, "This reference is no longer valid."));
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
        }
        ActionButton(Loc::TextOr(LocKeys::AgentDropHere, "Drop to attach"), Severity::Info, true, nullptr,
            ImVec2(-FLT_MIN, ImGui::GetFrameHeight() * 1.5f));
        return removed;
    }

}
