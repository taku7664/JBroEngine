#include "CatalogBuilder.h"
namespace JBro::ScriptApiCatalog2D::Internal
{
void AddCallbacks(ApiCatalog& catalog)
{
    {
    auto& type = AddType(catalog, "GameScriptBase", "JBro::GameScriptBase", ApiTypeKind::Script);
    {
        auto member = DeclareFunction<static_cast<void (GameScriptBase::*)()>(&GameScriptBase::OnAttached)>("OnAttached", Parameters());
        member.IsCallback = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (GameScriptBase::*)()>(&GameScriptBase::OnDetached)>("OnDetached", Parameters());
        member.IsCallback = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (GameScriptBase::*)()>(&GameScriptBase::OnEnabled)>("OnEnabled", Parameters());
        member.IsCallback = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (GameScriptBase::*)()>(&GameScriptBase::OnDisabled)>("OnDisabled", Parameters());
        member.IsCallback = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (GameScriptBase::*)()>(&GameScriptBase::OnCreate)>("OnCreate", Parameters());
        member.IsCallback = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (GameScriptBase::*)()>(&GameScriptBase::OnStart)>("OnStart", Parameters());
        member.IsCallback = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (GameScriptBase::*)()>(&GameScriptBase::OnUpdate)>("OnUpdate", Parameters());
        member.IsCallback = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (GameScriptBase::*)()>(&GameScriptBase::OnFixedUpdate)>("OnFixedUpdate", Parameters());
        member.IsCallback = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (GameScriptBase::*)()>(&GameScriptBase::OnDestroy)>("OnDestroy", Parameters());
        member.IsCallback = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<InstanceId (GameScriptBase::*)() const>(&GameScriptBase::GetInstanceId)>("GetInstanceId", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (GameScriptBase::*)() const>(&GameScriptBase::GetGameObject)>("GetGameObject", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (GameScriptBase::*)() const>(&GameScriptBase::IsActiveScript)>("IsActiveScript", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (GameScriptBase::*)() const>(&GameScriptBase::IsEnabled)>("IsEnabled", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (GameScriptBase::*)(Bool)>(&GameScriptBase::SetEnabled)>("SetEnabled", Parameters(In("enabled")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "GameScript2D", "JBro::GameScript2D", ApiTypeKind::Script);
    type.Bases.Add("GameScriptBase");
    {
        auto member = DeclareFunction<static_cast<void (GameScript2D::*)(const Collision2D&)>(&GameScript2D::OnCollisionEnter)>("OnCollisionEnter", Parameters(In("hit")));
        member.IsCallback = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (GameScript2D::*)(const Collision2D&)>(&GameScript2D::OnCollisionStay)>("OnCollisionStay", Parameters(In("hit")));
        member.IsCallback = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (GameScript2D::*)(const Collision2D&)>(&GameScript2D::OnCollisionExit)>("OnCollisionExit", Parameters(In("hit")));
        member.IsCallback = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (GameScript2D::*)(const Collision2D&)>(&GameScript2D::OnTriggerEnter)>("OnTriggerEnter", Parameters(In("hit")));
        member.IsCallback = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (GameScript2D::*)(const Collision2D&)>(&GameScript2D::OnTriggerStay)>("OnTriggerStay", Parameters(In("hit")));
        member.IsCallback = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (GameScript2D::*)(const Collision2D&)>(&GameScript2D::OnTriggerExit)>("OnTriggerExit", Parameters(In("hit")));
        member.IsCallback = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (GameScript2D::*)()>(&GameScript2D::OnPointerEnter)>("OnPointerEnter", Parameters());
        member.IsCallback = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (GameScript2D::*)()>(&GameScript2D::OnPointerExit)>("OnPointerExit", Parameters());
        member.IsCallback = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (GameScript2D::*)()>(&GameScript2D::OnPointerDown)>("OnPointerDown", Parameters());
        member.IsCallback = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (GameScript2D::*)()>(&GameScript2D::OnPointerUp)>("OnPointerUp", Parameters());
        member.IsCallback = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (GameScript2D::*)()>(&GameScript2D::OnClick)>("OnClick", Parameters());
        member.IsCallback = true;
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "IInputHandler", "JBro::IInputHandler", ApiTypeKind::Interface);
    auto member = DeclareFunction<&IInputHandler::OnInput>("OnInput", Parameters(In("input")));
    member.IsCallback = true;
    type.Members.Add(std::move(member));
    }
}
}
