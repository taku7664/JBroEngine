#pragma once
#include <JBro/Editor/Agent/Provider.h>
namespace JBro::Widget
{
    void AgentHistory(const Array<Agent::Message>& messages, Bool changed, const String& currentContext, Delegate<Bool(const Agent::Target&)> validate);
}
