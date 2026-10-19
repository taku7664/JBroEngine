#pragma once
#include <JBro/Editor/Agent/Provider.h>
namespace JBro::Widget
{
    enum class AgentChatAction { None, Connect, Disconnect, Install, LoginChatGPT, LoginKey, NewChat, Send, Stop, ResumeGuide, Browse, AttachFile };
    struct AgentChatView
    {
        const Array<Agent::Message>& messages;
        const Array<Agent::Target>& targets;
        const Array<Agent::Model>& models;
        Agent::Connection connection = Agent::Connection::Stopped;
        String project, canvas, model, error, attachmentError, guideTitle, guideTarget;
        Bool projectOpen = false;
        Bool guidePaused = false;
        Bool guideReady = false;
        Bool changed = false;
        Delegate<Bool(const Agent::Target&)> validate;
        Delegate<void()> receiveDrop;
    };
    struct AgentChatResult
    {
        AgentChatAction action = AgentChatAction::None;
        Int32 removed = -1;
        Int32 model = -1;
    };
    AgentChatResult AgentChat(const AgentChatView& view, Bool& settingsOpen, String& input, String& executable, String& apiKey);
}
