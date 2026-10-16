#include <JBro/Editor/Widget/AgentChat.h>
#include <JBro/Editor/Widget/Basic.h>
#include <JBro/Editor/Localization.h>
#include <JBro/Editor/LocalizationKeys.h>
#include <nlohmann/json.hpp>
#include <imgui.h>
#include <charconv>
#include <JBro/Types/ValueMath.h>

namespace JBro::Widget
{
    void AgentHistory(const Array<Agent::Message>& messages, Bool changed, const String& currentContext, Delegate<Bool(const Agent::Target&)> validate)
    {
        const Float height = Max(Float(100.0f), Float(ImGui::GetContentRegionAvail().y - 135.0f));
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
            const auto current = nlohmann::json::parse(currentContext, nullptr, false);
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
                if (!message.context.empty())
                {
                    const auto context = nlohmann::json::parse(message.context, nullptr, false);
                    if (!context.is_discarded())
                    {
                        const Bool stale = current.is_discarded() || context.value("scope", "") != current.value("scope", "");
                        HintText(Loc::TextOr(stale ? LocKeys::AgentPreviousContext : LocKeys::AgentAttached, stale ? "Previous canvas references; refresh before use." : "Included at send time:"));
                        for (const auto& object : context.value("selection", nlohmann::json::array()))
                        {
                            const String label = object.value("name", "");
                            Agent::Target target;
                            target.project = object.value("project", "");
                            target.canvas = object.value("canvas", "");
                            const String id = object.value("instanceId", "");
                            std::uint64_t instance = 0;
                            const auto parsed = std::from_chars(id.data(), id.data() + id.size(), instance);
                            target.instance = instance;
                            const Bool valid = !stale && parsed.ec == std::errc{} && parsed.ptr == id.data() + id.size()
                                && validate.IsBound() && validate.Invoke(target);
                            HintText(label.c_str());
                            if (!valid)
                            {
                                SeverityTextF(Severity::Warning, "%s", Loc::TextOr(LocKeys::AgentInvalidTarget, "This reference is no longer valid."));
                            }
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
}
