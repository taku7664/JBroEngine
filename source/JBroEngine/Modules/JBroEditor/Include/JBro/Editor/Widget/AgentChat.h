#pragma once
#include <JBro/Editor/Agent/Provider.h>
namespace JBro::Widget
{
    void AgentHistory(const Array<Agent::Message>& messages, Bool changed, Delegate<Bool(const Agent::Target&)> validate, Float reservedHeight = 135.0f);
    Int32 AgentAttachments(const Array<Agent::Target>& targets, Delegate<Bool(const Agent::Target&)> validate);
}
