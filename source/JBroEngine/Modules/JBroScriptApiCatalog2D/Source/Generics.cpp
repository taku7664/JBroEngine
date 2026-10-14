#include "CatalogBuilder.h"
namespace JBro::ScriptApiCatalog2D::Internal
{
void AddGenericMembers(ApiCatalog& catalog)
{
    auto& object = FindType(catalog, "GameObject");
    {
        auto member = DeclareFunction<&Handle::GameObject::GetComponent<Handle::Transform2D>>("GetComponent", Parameters());
        member.Type = "T?";
        member.TypeParameters.Add("T");
        member.Description = "T must be a built-in component handle.";
        object.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<&Handle::GameObject::AddComponent<Handle::Transform2D>>("AddComponent", Parameters());
        member.Type = "T?";
        member.TypeParameters.Add("T");
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        member.Description = "T must be a built-in component handle.";
        object.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<&Handle::GameObject::GetScript<CatalogScriptShape>>("GetScript", Parameters());
        member.Type = "T?";
        member.TypeParameters.Add("T");
        member.Description = "T must be a user script.";
        object.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<&Handle::GameObject::AddScript<CatalogScriptShape>>("AddScript", Parameters());
        member.Type = "T?";
        member.TypeParameters.Add("T");
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        member.Description = "T must be a user script.";
        object.Members.Add(std::move(member));
    }
    auto& network = FindType(catalog, "NetworkService");
    {
        auto member = DeclareFunction<&Service::NetworkService::Send<UInt32>>("Send", Parameters(In("connection"), In("messageId"), In("message", nullptr, "T"), In("channel", "NetChannel.ReliableOrdered")));
        member.TypeParameters.Add("T");
        member.Description = "T must be a trivially-copyable POD message. Native transport copies its little-endian bytes.";
        network.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<&Service::NetworkService::Broadcast<UInt32>>("Broadcast", Parameters(In("messageId"), In("message", nullptr, "T"), In("channel", "NetChannel.ReliableOrdered")));
        member.TypeParameters.Add("T");
        member.Description = "T must be a trivially-copyable POD message.";
        network.Members.Add(std::move(member));
    }
}
}
