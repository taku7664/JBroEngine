#include "CatalogBuilder.h"
namespace JBro::ScriptApiCatalog2D::Internal
{
void AddHandles(ApiCatalog& catalog)
{
    {
    auto& type = AddType(catalog, "GameObject", "JBro::Handle::GameObject", ApiTypeKind::Handle);
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::GameObject::*)() const>(&Handle::GameObject::IsValid)>("IsValid", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::GameObject::*)()>(&Handle::GameObject::Destroy)>("Destroy", Parameters());
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::GameObject::*)(Bool)>(&Handle::GameObject::SetActive)>("SetActive", Parameters(In("active")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::GameObject::*)() const>(&Handle::GameObject::IsActive)>("IsActive", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<const char* (Handle::GameObject::*)() const>(&Handle::GameObject::GetName)>("GetName", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::GameObject::*)(const char*)>(&Handle::GameObject::SetName)>("SetName", Parameters(In("name")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::Layer (Handle::GameObject::*)() const>(&Handle::GameObject::GetLayer)>("GetLayer", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::GameObject::*)() const>(&Handle::GameObject::GetParent)>("GetParent", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::GameObject::*)(Handle::GameObject)>(&Handle::GameObject::SetParent)>("SetParent", Parameters(In("parent")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<UInt32 (Handle::GameObject::*)() const>(&Handle::GameObject::GetChildCount)>("GetChildCount", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::GameObject::*)(UInt32) const>(&Handle::GameObject::GetChild)>("GetChild", Parameters(In("index")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::GameObject::*)(const char*) const>(&Handle::GameObject::operator[])>("operator[]", Parameters(In("name")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::GameObject::*)(NameId) const>(&Handle::GameObject::operator[])>("operator[]", Parameters(In("name")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::GameObject::*)(const char*) const>(&Handle::GameObject::FindInChildren)>("FindInChildren", Parameters(In("name")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<const char* (Handle::GameObject::*)() const>(&Handle::GameObject::GetTag)>("GetTag", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::GameObject::*)(const char*)>(&Handle::GameObject::SetTag)>("SetTag", Parameters(In("tag")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::GameObject::*)(const char*) const>(&Handle::GameObject::CompareTag)>("CompareTag", Parameters(In("tag")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<InstanceId (Handle::GameObject::*)() const>(&Handle::GameObject::GetInstanceId)>("GetInstanceId", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::GameObject::IsValid>("valid");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::GameObject::IsActive, &Handle::GameObject::SetActive>("active");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::GameObject::GetName, &Handle::GameObject::SetName>("name");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::GameObject::GetLayer>("layer");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::GameObject::GetParent, &Handle::GameObject::SetParent>("parent");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::GameObject::GetChildCount>("childCount");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::GameObject::GetTag, &Handle::GameObject::SetTag>("tag");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::GameObject::GetInstanceId>("instanceId");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "Layer", "JBro::Handle::Layer", ApiTypeKind::Handle);
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Layer::*)() const>(&Handle::Layer::IsValid)>("IsValid", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<const char* (Handle::Layer::*)() const>(&Handle::Layer::GetName)>("GetName", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::Layer::*)(const char*) const>(&Handle::Layer::operator[])>("operator[]", Parameters(In("name")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::Layer::*)(NameId) const>(&Handle::Layer::operator[])>("operator[]", Parameters(In("name")));
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<UInt32 (Handle::Layer::*)() const>(&Handle::Layer::GetId)>("GetId", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Layer::IsValid>("valid");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Layer::GetName>("name");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Layer::GetId>("id");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "AudioListener2D", "JBro::Handle::AudioListener2D", ApiTypeKind::Handle);
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::AudioListener2D::*)() const>(&Handle::AudioListener2D::IsValid)>("IsValid", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::AudioListener2D::*)() const>(&Handle::AudioListener2D::GetGameObject)>("GetGameObject", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::AudioListener2D::*)() const>(&Handle::AudioListener2D::IsEnabled)>("IsEnabled", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::AudioListener2D::*)(Bool)>(&Handle::AudioListener2D::SetEnabled)>("SetEnabled", Parameters(In("enabled")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::AudioListener2D::*)() const>(&Handle::AudioListener2D::GetPanDistance)>("GetPanDistance", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::AudioListener2D::*)(Float)>(&Handle::AudioListener2D::SetPanDistance)>("SetPanDistance", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::AudioListener2D::*)() const>(&Handle::AudioListener2D::GetVolume)>("GetVolume", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::AudioListener2D::*)(Float)>(&Handle::AudioListener2D::SetVolume)>("SetVolume", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::AudioListener2D::IsValid>("valid");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::AudioListener2D::GetGameObject>("gameObject");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::AudioListener2D::IsEnabled, &Handle::AudioListener2D::SetEnabled>("enabled");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::AudioListener2D::GetPanDistance, &Handle::AudioListener2D::SetPanDistance>("panDistance");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::AudioListener2D::GetVolume, &Handle::AudioListener2D::SetVolume>("volume");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "Button2D", "JBro::Handle::Button2D", ApiTypeKind::Handle);
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Button2D::*)() const>(&Handle::Button2D::IsValid)>("IsValid", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::Button2D::*)() const>(&Handle::Button2D::GetGameObject)>("GetGameObject", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Button2D::*)() const>(&Handle::Button2D::IsEnabled)>("IsEnabled", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Button2D::*)(Bool)>(&Handle::Button2D::SetEnabled)>("SetEnabled", Parameters(In("enabled")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::Button2D::*)() const>(&Handle::Button2D::GetSize)>("GetSize", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Button2D::*)(Vector2)>(&Handle::Button2D::SetSize)>("SetSize", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::Button2D::*)() const>(&Handle::Button2D::GetOffset)>("GetOffset", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Button2D::*)(Vector2)>(&Handle::Button2D::SetOffset)>("SetOffset", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Button2D::*)() const>(&Handle::Button2D::GetInteractable)>("GetInteractable", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Button2D::*)(Bool)>(&Handle::Button2D::SetInteractable)>("SetInteractable", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Component::ButtonTransition (Handle::Button2D::*)() const>(&Handle::Button2D::GetTransition)>("GetTransition", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Button2D::*)(Component::ButtonTransition)>(&Handle::Button2D::SetTransition)>("SetTransition", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::SpriteRenderer2D (Handle::Button2D::*)() const>(&Handle::Button2D::GetTargetGraphic)>("GetTargetGraphic", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Button2D::*)(Handle::SpriteRenderer2D)>(&Handle::Button2D::SetTargetGraphic)>("SetTargetGraphic", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<AssetHandle (Handle::Button2D::*)() const>(&Handle::Button2D::GetHoverSprite)>("GetHoverSprite", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Button2D::*)(AssetHandle)>(&Handle::Button2D::SetHoverSprite)>("SetHoverSprite", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<AssetHandle (Handle::Button2D::*)() const>(&Handle::Button2D::GetPressedSprite)>("GetPressedSprite", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Button2D::*)(AssetHandle)>(&Handle::Button2D::SetPressedSprite)>("SetPressedSprite", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<AssetHandle (Handle::Button2D::*)() const>(&Handle::Button2D::GetDisabledSprite)>("GetDisabledSprite", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Button2D::*)(AssetHandle)>(&Handle::Button2D::SetDisabledSprite)>("SetDisabledSprite", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Color (Handle::Button2D::*)() const>(&Handle::Button2D::GetNormalTint)>("GetNormalTint", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Button2D::*)(Color)>(&Handle::Button2D::SetNormalTint)>("SetNormalTint", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Color (Handle::Button2D::*)() const>(&Handle::Button2D::GetHoverTint)>("GetHoverTint", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Button2D::*)(Color)>(&Handle::Button2D::SetHoverTint)>("SetHoverTint", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Color (Handle::Button2D::*)() const>(&Handle::Button2D::GetPressedTint)>("GetPressedTint", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Button2D::*)(Color)>(&Handle::Button2D::SetPressedTint)>("SetPressedTint", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Color (Handle::Button2D::*)() const>(&Handle::Button2D::GetDisabledTint)>("GetDisabledTint", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Button2D::*)(Color)>(&Handle::Button2D::SetDisabledTint)>("SetDisabledTint", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Button2D::*)() const>(&Handle::Button2D::GetHovered)>("GetHovered", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Button2D::*)() const>(&Handle::Button2D::GetFocused)>("GetFocused", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Button2D::*)() const>(&Handle::Button2D::GetPressed)>("GetPressed", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Button2D::*)() const>(&Handle::Button2D::GetClicked)>("GetClicked", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Button2D::IsValid>("valid");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Button2D::GetGameObject>("gameObject");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Button2D::IsEnabled, &Handle::Button2D::SetEnabled>("enabled");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Button2D::GetSize, &Handle::Button2D::SetSize>("size");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Button2D::GetOffset, &Handle::Button2D::SetOffset>("offset");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Button2D::GetInteractable, &Handle::Button2D::SetInteractable>("interactable");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Button2D::GetTransition, &Handle::Button2D::SetTransition>("transition");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Button2D::GetTargetGraphic, &Handle::Button2D::SetTargetGraphic>("targetGraphic");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Button2D::GetHoverSprite, &Handle::Button2D::SetHoverSprite>("hoverSprite");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Button2D::GetPressedSprite, &Handle::Button2D::SetPressedSprite>("pressedSprite");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Button2D::GetDisabledSprite, &Handle::Button2D::SetDisabledSprite>("disabledSprite");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Button2D::GetNormalTint, &Handle::Button2D::SetNormalTint>("normalTint");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Button2D::GetHoverTint, &Handle::Button2D::SetHoverTint>("hoverTint");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Button2D::GetPressedTint, &Handle::Button2D::SetPressedTint>("pressedTint");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Button2D::GetDisabledTint, &Handle::Button2D::SetDisabledTint>("disabledTint");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Button2D::GetHovered>("hovered");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Button2D::GetFocused>("focused");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Button2D::GetPressed>("pressed");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Button2D::GetClicked>("clicked");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "Slider2D", "JBro::Handle::Slider2D", ApiTypeKind::Handle);
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Slider2D::*)() const>(&Handle::Slider2D::IsValid)>("IsValid", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::Slider2D::*)() const>(&Handle::Slider2D::GetGameObject)>("GetGameObject", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Slider2D::*)() const>(&Handle::Slider2D::IsEnabled)>("IsEnabled", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Slider2D::*)(Bool)>(&Handle::Slider2D::SetEnabled)>("SetEnabled", Parameters(In("enabled")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::Slider2D::*)() const>(&Handle::Slider2D::GetSize)>("GetSize", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Slider2D::*)(Vector2)>(&Handle::Slider2D::SetSize)>("SetSize", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::Slider2D::*)() const>(&Handle::Slider2D::GetOffset)>("GetOffset", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Slider2D::*)(Vector2)>(&Handle::Slider2D::SetOffset)>("SetOffset", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Slider2D::*)() const>(&Handle::Slider2D::GetInteractable)>("GetInteractable", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Slider2D::*)(Bool)>(&Handle::Slider2D::SetInteractable)>("SetInteractable", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Component::SliderDirection (Handle::Slider2D::*)() const>(&Handle::Slider2D::GetDirection)>("GetDirection", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Slider2D::*)(Component::SliderDirection)>(&Handle::Slider2D::SetDirection)>("SetDirection", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::Slider2D::*)() const>(&Handle::Slider2D::GetValue)>("GetValue", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Slider2D::*)(Float)>(&Handle::Slider2D::SetValue)>("SetValue", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::Slider2D::*)() const>(&Handle::Slider2D::GetHandle)>("GetHandle", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Slider2D::*)(Handle::GameObject)>(&Handle::Slider2D::SetHandle)>("SetHandle", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::SpriteRenderer2D (Handle::Slider2D::*)() const>(&Handle::Slider2D::GetFill)>("GetFill", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Slider2D::*)(Handle::SpriteRenderer2D)>(&Handle::Slider2D::SetFill)>("SetFill", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Slider2D::*)() const>(&Handle::Slider2D::GetHovered)>("GetHovered", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Slider2D::*)() const>(&Handle::Slider2D::GetFocused)>("GetFocused", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Slider2D::*)() const>(&Handle::Slider2D::GetPressed)>("GetPressed", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Slider2D::*)() const>(&Handle::Slider2D::GetChanged)>("GetChanged", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Slider2D::IsValid>("valid");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Slider2D::GetGameObject>("gameObject");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Slider2D::IsEnabled, &Handle::Slider2D::SetEnabled>("enabled");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Slider2D::GetSize, &Handle::Slider2D::SetSize>("size");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Slider2D::GetOffset, &Handle::Slider2D::SetOffset>("offset");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Slider2D::GetInteractable, &Handle::Slider2D::SetInteractable>("interactable");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Slider2D::GetDirection, &Handle::Slider2D::SetDirection>("direction");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Slider2D::GetValue, &Handle::Slider2D::SetValue>("value");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Slider2D::GetHandle, &Handle::Slider2D::SetHandle>("handle");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Slider2D::GetFill, &Handle::Slider2D::SetFill>("fill");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Slider2D::GetHovered>("hovered");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Slider2D::GetFocused>("focused");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Slider2D::GetPressed>("pressed");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Slider2D::GetChanged>("changed");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "Toggle2D", "JBro::Handle::Toggle2D", ApiTypeKind::Handle);
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Toggle2D::*)() const>(&Handle::Toggle2D::IsValid)>("IsValid", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::Toggle2D::*)() const>(&Handle::Toggle2D::GetGameObject)>("GetGameObject", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Toggle2D::*)() const>(&Handle::Toggle2D::IsEnabled)>("IsEnabled", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Toggle2D::*)(Bool)>(&Handle::Toggle2D::SetEnabled)>("SetEnabled", Parameters(In("enabled")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::Toggle2D::*)() const>(&Handle::Toggle2D::GetSize)>("GetSize", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Toggle2D::*)(Vector2)>(&Handle::Toggle2D::SetSize)>("SetSize", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::Toggle2D::*)() const>(&Handle::Toggle2D::GetOffset)>("GetOffset", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Toggle2D::*)(Vector2)>(&Handle::Toggle2D::SetOffset)>("SetOffset", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Toggle2D::*)() const>(&Handle::Toggle2D::GetInteractable)>("GetInteractable", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Toggle2D::*)(Bool)>(&Handle::Toggle2D::SetInteractable)>("SetInteractable", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Toggle2D::*)() const>(&Handle::Toggle2D::GetIsOn)>("GetIsOn", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Toggle2D::*)(Bool)>(&Handle::Toggle2D::SetIsOn)>("SetIsOn", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::SpriteRenderer2D (Handle::Toggle2D::*)() const>(&Handle::Toggle2D::GetCheckmark)>("GetCheckmark", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Toggle2D::*)(Handle::SpriteRenderer2D)>(&Handle::Toggle2D::SetCheckmark)>("SetCheckmark", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Toggle2D::*)() const>(&Handle::Toggle2D::GetHovered)>("GetHovered", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Toggle2D::*)() const>(&Handle::Toggle2D::GetFocused)>("GetFocused", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Toggle2D::*)() const>(&Handle::Toggle2D::GetPressed)>("GetPressed", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Toggle2D::*)() const>(&Handle::Toggle2D::GetChanged)>("GetChanged", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Toggle2D::IsValid>("valid");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Toggle2D::GetGameObject>("gameObject");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Toggle2D::IsEnabled, &Handle::Toggle2D::SetEnabled>("enabled");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Toggle2D::GetSize, &Handle::Toggle2D::SetSize>("size");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Toggle2D::GetOffset, &Handle::Toggle2D::SetOffset>("offset");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Toggle2D::GetInteractable, &Handle::Toggle2D::SetInteractable>("interactable");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Toggle2D::GetIsOn, &Handle::Toggle2D::SetIsOn>("isOn");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Toggle2D::GetCheckmark, &Handle::Toggle2D::SetCheckmark>("checkmark");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Toggle2D::GetHovered>("hovered");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Toggle2D::GetFocused>("focused");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Toggle2D::GetPressed>("pressed");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Toggle2D::GetChanged>("changed");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "Mask2D", "JBro::Handle::Mask2D", ApiTypeKind::Handle);
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Mask2D::*)() const>(&Handle::Mask2D::IsValid)>("IsValid", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::Mask2D::*)() const>(&Handle::Mask2D::GetGameObject)>("GetGameObject", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Mask2D::*)() const>(&Handle::Mask2D::IsEnabled)>("IsEnabled", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Mask2D::*)(Bool)>(&Handle::Mask2D::SetEnabled)>("SetEnabled", Parameters(In("enabled")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::Mask2D::*)() const>(&Handle::Mask2D::GetSize)>("GetSize", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Mask2D::*)(Vector2)>(&Handle::Mask2D::SetSize)>("SetSize", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::Mask2D::*)() const>(&Handle::Mask2D::GetOffset)>("GetOffset", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Mask2D::*)(Vector2)>(&Handle::Mask2D::SetOffset)>("SetOffset", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Mask2D::IsValid>("valid");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Mask2D::GetGameObject>("gameObject");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Mask2D::IsEnabled, &Handle::Mask2D::SetEnabled>("enabled");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Mask2D::GetSize, &Handle::Mask2D::SetSize>("size");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Mask2D::GetOffset, &Handle::Mask2D::SetOffset>("offset");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "ScrollView2D", "JBro::Handle::ScrollView2D", ApiTypeKind::Handle);
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::ScrollView2D::*)() const>(&Handle::ScrollView2D::IsValid)>("IsValid", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::ScrollView2D::*)() const>(&Handle::ScrollView2D::GetGameObject)>("GetGameObject", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::ScrollView2D::*)() const>(&Handle::ScrollView2D::IsEnabled)>("IsEnabled", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::ScrollView2D::*)(Bool)>(&Handle::ScrollView2D::SetEnabled)>("SetEnabled", Parameters(In("enabled")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::ScrollView2D::*)() const>(&Handle::ScrollView2D::GetContent)>("GetContent", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::ScrollView2D::*)(Handle::GameObject)>(&Handle::ScrollView2D::SetContent)>("SetContent", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::ScrollView2D::*)() const>(&Handle::ScrollView2D::GetContentSize)>("GetContentSize", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::ScrollView2D::*)(Vector2)>(&Handle::ScrollView2D::SetContentSize)>("SetContentSize", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::ScrollView2D::*)() const>(&Handle::ScrollView2D::GetHorizontal)>("GetHorizontal", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::ScrollView2D::*)(Bool)>(&Handle::ScrollView2D::SetHorizontal)>("SetHorizontal", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::ScrollView2D::*)() const>(&Handle::ScrollView2D::GetVertical)>("GetVertical", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::ScrollView2D::*)(Bool)>(&Handle::ScrollView2D::SetVertical)>("SetVertical", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::ScrollView2D::*)() const>(&Handle::ScrollView2D::GetScrollSensitivity)>("GetScrollSensitivity", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::ScrollView2D::*)(Float)>(&Handle::ScrollView2D::SetScrollSensitivity)>("SetScrollSensitivity", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::ScrollView2D::*)() const>(&Handle::ScrollView2D::GetDecelerationRate)>("GetDecelerationRate", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::ScrollView2D::*)(Float)>(&Handle::ScrollView2D::SetDecelerationRate)>("SetDecelerationRate", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::ScrollView2D::*)() const>(&Handle::ScrollView2D::GetDragging)>("GetDragging", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::ScrollView2D::*)() const>(&Handle::ScrollView2D::GetVelocity)>("GetVelocity", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::ScrollView2D::IsValid>("valid");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::ScrollView2D::GetGameObject>("gameObject");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::ScrollView2D::IsEnabled, &Handle::ScrollView2D::SetEnabled>("enabled");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::ScrollView2D::GetContent, &Handle::ScrollView2D::SetContent>("content");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::ScrollView2D::GetContentSize, &Handle::ScrollView2D::SetContentSize>("contentSize");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::ScrollView2D::GetHorizontal, &Handle::ScrollView2D::SetHorizontal>("horizontal");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::ScrollView2D::GetVertical, &Handle::ScrollView2D::SetVertical>("vertical");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::ScrollView2D::GetScrollSensitivity, &Handle::ScrollView2D::SetScrollSensitivity>("scrollSensitivity");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::ScrollView2D::GetDecelerationRate, &Handle::ScrollView2D::SetDecelerationRate>("decelerationRate");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::ScrollView2D::GetDragging>("dragging");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::ScrollView2D::GetVelocity>("velocity");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "InputField2D", "JBro::Handle::InputField2D", ApiTypeKind::Handle);
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::InputField2D::*)() const>(&Handle::InputField2D::IsValid)>("IsValid", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::InputField2D::*)() const>(&Handle::InputField2D::GetGameObject)>("GetGameObject", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::InputField2D::*)() const>(&Handle::InputField2D::IsEnabled)>("IsEnabled", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::InputField2D::*)(Bool)>(&Handle::InputField2D::SetEnabled)>("SetEnabled", Parameters(In("enabled")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::InputField2D::*)() const>(&Handle::InputField2D::GetSize)>("GetSize", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::InputField2D::*)(Vector2)>(&Handle::InputField2D::SetSize)>("SetSize", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::InputField2D::*)() const>(&Handle::InputField2D::GetOffset)>("GetOffset", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::InputField2D::*)(Vector2)>(&Handle::InputField2D::SetOffset)>("SetOffset", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::InputField2D::*)() const>(&Handle::InputField2D::GetInteractable)>("GetInteractable", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::InputField2D::*)(Bool)>(&Handle::InputField2D::SetInteractable)>("SetInteractable", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<const char* (Handle::InputField2D::*)() const>(&Handle::InputField2D::GetText)>("GetText", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::InputField2D::*)(const char*)>(&Handle::InputField2D::SetText)>("SetText", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::Text2D (Handle::InputField2D::*)() const>(&Handle::InputField2D::GetDisplay)>("GetDisplay", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::InputField2D::*)(Handle::Text2D)>(&Handle::InputField2D::SetDisplay)>("SetDisplay", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::SpriteRenderer2D (Handle::InputField2D::*)() const>(&Handle::InputField2D::GetCaret)>("GetCaret", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::InputField2D::*)(Handle::SpriteRenderer2D)>(&Handle::InputField2D::SetCaret)>("SetCaret", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Int32 (Handle::InputField2D::*)() const>(&Handle::InputField2D::GetMaxLength)>("GetMaxLength", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::InputField2D::*)(Int32)>(&Handle::InputField2D::SetMaxLength)>("SetMaxLength", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::InputField2D::*)() const>(&Handle::InputField2D::GetBlinkRate)>("GetBlinkRate", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::InputField2D::*)(Float)>(&Handle::InputField2D::SetBlinkRate)>("SetBlinkRate", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::InputField2D::*)() const>(&Handle::InputField2D::GetFocused)>("GetFocused", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::InputField2D::*)(Bool)>(&Handle::InputField2D::SetFocused)>("SetFocused", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::InputField2D::*)() const>(&Handle::InputField2D::GetChanged)>("GetChanged", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::InputField2D::*)() const>(&Handle::InputField2D::GetSubmitted)>("GetSubmitted", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Int32 (Handle::InputField2D::*)() const>(&Handle::InputField2D::GetCaretIndex)>("GetCaretIndex", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::InputField2D::*)(Int32)>(&Handle::InputField2D::SetCaretIndex)>("SetCaretIndex", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Int32 (Handle::InputField2D::*)() const>(&Handle::InputField2D::GetSelectionAnchor)>("GetSelectionAnchor", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::InputField2D::*)(Int32, Int32)>(&Handle::InputField2D::SetSelection)>("SetSelection", Parameters(In("anchor"), In("caret")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::InputField2D::*)() const>(&Handle::InputField2D::IsComposing)>("IsComposing", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::InputField2D::IsValid>("valid");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::InputField2D::GetGameObject>("gameObject");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::InputField2D::IsEnabled, &Handle::InputField2D::SetEnabled>("enabled");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::InputField2D::GetSize, &Handle::InputField2D::SetSize>("size");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::InputField2D::GetOffset, &Handle::InputField2D::SetOffset>("offset");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::InputField2D::GetInteractable, &Handle::InputField2D::SetInteractable>("interactable");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::InputField2D::GetText, &Handle::InputField2D::SetText>("text");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::InputField2D::GetDisplay, &Handle::InputField2D::SetDisplay>("display");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::InputField2D::GetCaret, &Handle::InputField2D::SetCaret>("caret");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::InputField2D::GetMaxLength, &Handle::InputField2D::SetMaxLength>("maxLength");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::InputField2D::GetBlinkRate, &Handle::InputField2D::SetBlinkRate>("blinkRate");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::InputField2D::GetFocused, &Handle::InputField2D::SetFocused>("focused");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::InputField2D::GetChanged>("changed");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::InputField2D::GetSubmitted>("submitted");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::InputField2D::GetCaretIndex, &Handle::InputField2D::SetCaretIndex>("caretIndex");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::InputField2D::GetSelectionAnchor>("selectionAnchor");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::InputField2D::IsComposing>("composing");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "Layout2D", "JBro::Handle::Layout2D", ApiTypeKind::Handle);
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Layout2D::*)() const>(&Handle::Layout2D::IsValid)>("IsValid", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::Layout2D::*)() const>(&Handle::Layout2D::GetGameObject)>("GetGameObject", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Layout2D::*)() const>(&Handle::Layout2D::IsEnabled)>("IsEnabled", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Layout2D::*)(Bool)>(&Handle::Layout2D::SetEnabled)>("SetEnabled", Parameters(In("enabled")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Component::LayoutMode (Handle::Layout2D::*)() const>(&Handle::Layout2D::GetMode)>("GetMode", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Layout2D::*)(Component::LayoutMode)>(&Handle::Layout2D::SetMode)>("SetMode", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::Layout2D::*)() const>(&Handle::Layout2D::GetCellSize)>("GetCellSize", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Layout2D::*)(Vector2)>(&Handle::Layout2D::SetCellSize)>("SetCellSize", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::Layout2D::*)() const>(&Handle::Layout2D::GetSpacing)>("GetSpacing", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Layout2D::*)(Vector2)>(&Handle::Layout2D::SetSpacing)>("SetSpacing", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::Layout2D::*)() const>(&Handle::Layout2D::GetPadding)>("GetPadding", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Layout2D::*)(Vector2)>(&Handle::Layout2D::SetPadding)>("SetPadding", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Int32 (Handle::Layout2D::*)() const>(&Handle::Layout2D::GetColumns)>("GetColumns", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Layout2D::*)(Int32)>(&Handle::Layout2D::SetColumns)>("SetColumns", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::Layout2D::*)() const>(&Handle::Layout2D::GetPivot)>("GetPivot", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Layout2D::*)(Vector2)>(&Handle::Layout2D::SetPivot)>("SetPivot", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::Layout2D::*)() const>(&Handle::Layout2D::GetContentWidth)>("GetContentWidth", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::Layout2D::*)() const>(&Handle::Layout2D::GetContentHeight)>("GetContentHeight", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Layout2D::IsValid>("valid");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Layout2D::GetGameObject>("gameObject");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Layout2D::IsEnabled, &Handle::Layout2D::SetEnabled>("enabled");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Layout2D::GetMode, &Handle::Layout2D::SetMode>("mode");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Layout2D::GetCellSize, &Handle::Layout2D::SetCellSize>("cellSize");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Layout2D::GetSpacing, &Handle::Layout2D::SetSpacing>("spacing");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Layout2D::GetPadding, &Handle::Layout2D::SetPadding>("padding");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Layout2D::GetColumns, &Handle::Layout2D::SetColumns>("columns");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Layout2D::GetPivot, &Handle::Layout2D::SetPivot>("pivot");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Layout2D::GetContentWidth>("contentWidth");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Layout2D::GetContentHeight>("contentHeight");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "Camera2D", "JBro::Handle::Camera2D", ApiTypeKind::Handle);
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Camera2D::*)() const>(&Handle::Camera2D::IsValid)>("IsValid", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::Camera2D::*)() const>(&Handle::Camera2D::GetGameObject)>("GetGameObject", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Camera2D::*)() const>(&Handle::Camera2D::IsEnabled)>("IsEnabled", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Camera2D::*)(Bool)>(&Handle::Camera2D::SetEnabled)>("SetEnabled", Parameters(In("enabled")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Component::CameraProjection2D (Handle::Camera2D::*)() const>(&Handle::Camera2D::GetProjection)>("GetProjection", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Camera2D::*)(Component::CameraProjection2D)>(&Handle::Camera2D::SetProjection)>("SetProjection", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::Camera2D::*)() const>(&Handle::Camera2D::GetOrthographicSize)>("GetOrthographicSize", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Camera2D::*)(Float)>(&Handle::Camera2D::SetOrthographicSize)>("SetOrthographicSize", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::Camera2D::*)() const>(&Handle::Camera2D::GetPixelsPerUnit)>("GetPixelsPerUnit", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Camera2D::*)(Float)>(&Handle::Camera2D::SetPixelsPerUnit)>("SetPixelsPerUnit", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::Camera2D::*)() const>(&Handle::Camera2D::GetNearPlane)>("GetNearPlane", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Camera2D::*)(Float)>(&Handle::Camera2D::SetNearPlane)>("SetNearPlane", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::Camera2D::*)() const>(&Handle::Camera2D::GetFarPlane)>("GetFarPlane", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Camera2D::*)(Float)>(&Handle::Camera2D::SetFarPlane)>("SetFarPlane", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Color (Handle::Camera2D::*)() const>(&Handle::Camera2D::GetClearColor)>("GetClearColor", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Camera2D::*)(Color)>(&Handle::Camera2D::SetClearColor)>("SetClearColor", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Camera2D::*)() const>(&Handle::Camera2D::GetPrimary)>("GetPrimary", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Camera2D::*)(Bool)>(&Handle::Camera2D::SetPrimary)>("SetPrimary", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Camera2D::IsValid>("valid");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Camera2D::GetGameObject>("gameObject");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Camera2D::IsEnabled, &Handle::Camera2D::SetEnabled>("enabled");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Camera2D::GetProjection, &Handle::Camera2D::SetProjection>("projection");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Camera2D::GetOrthographicSize, &Handle::Camera2D::SetOrthographicSize>("orthographicSize");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Camera2D::GetPixelsPerUnit, &Handle::Camera2D::SetPixelsPerUnit>("pixelsPerUnit");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Camera2D::GetNearPlane, &Handle::Camera2D::SetNearPlane>("nearPlane");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Camera2D::GetFarPlane, &Handle::Camera2D::SetFarPlane>("farPlane");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Camera2D::GetClearColor, &Handle::Camera2D::SetClearColor>("clearColor");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Camera2D::GetPrimary, &Handle::Camera2D::SetPrimary>("primary");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "Collider2D", "JBro::Handle::Collider2D", ApiTypeKind::Handle);
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Collider2D::*)() const>(&Handle::Collider2D::IsValid)>("IsValid", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::Collider2D::*)() const>(&Handle::Collider2D::GetGameObject)>("GetGameObject", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Collider2D::*)() const>(&Handle::Collider2D::IsEnabled)>("IsEnabled", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Collider2D::*)(Bool)>(&Handle::Collider2D::SetEnabled)>("SetEnabled", Parameters(In("enabled")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Component::ColliderShape2D (Handle::Collider2D::*)() const>(&Handle::Collider2D::GetShape)>("GetShape", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Collider2D::*)(Component::ColliderShape2D)>(&Handle::Collider2D::SetShape)>("SetShape", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::Collider2D::*)() const>(&Handle::Collider2D::GetOffset)>("GetOffset", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Collider2D::*)(Vector2)>(&Handle::Collider2D::SetOffset)>("SetOffset", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::Collider2D::*)() const>(&Handle::Collider2D::GetSize)>("GetSize", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Collider2D::*)(Vector2)>(&Handle::Collider2D::SetSize)>("SetSize", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::Collider2D::*)() const>(&Handle::Collider2D::GetRadius)>("GetRadius", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Collider2D::*)(Float)>(&Handle::Collider2D::SetRadius)>("SetRadius", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Collider2D::*)() const>(&Handle::Collider2D::GetIsTrigger)>("GetIsTrigger", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Collider2D::*)(Bool)>(&Handle::Collider2D::SetIsTrigger)>("SetIsTrigger", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Collider2D::*)(Array<Vector2>&) const>(&Handle::Collider2D::GetPoints)>("GetPoints", Parameters(Out("points")));
        member.Type = TypeName<Array<Vector2>>();
        member.Projection = ApiProjectionKind::CollectionReturn;
        member.HasOutputProjection = true;
        member.SuccessFromNativeReturn = false;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Collider2D::*)(const Array<Vector2>&)>(&Handle::Collider2D::SetPoints)>("SetPoints", Parameters(In("points")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Collider2D::*)() const>(&Handle::Collider2D::GetLoop)>("GetLoop", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Collider2D::*)(Bool)>(&Handle::Collider2D::SetLoop)>("SetLoop", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Collider2D::*)() const>(&Handle::Collider2D::GetOneWay)>("GetOneWay", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Collider2D::*)(Bool)>(&Handle::Collider2D::SetOneWay)>("SetOneWay", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::Collider2D::*)() const>(&Handle::Collider2D::GetFriction)>("GetFriction", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Collider2D::*)(Float)>(&Handle::Collider2D::SetFriction)>("SetFriction", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::Collider2D::*)() const>(&Handle::Collider2D::GetRestitution)>("GetRestitution", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Collider2D::*)(Float)>(&Handle::Collider2D::SetRestitution)>("SetRestitution", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<PhysicsLayerMask (Handle::Collider2D::*)() const>(&Handle::Collider2D::GetLayer)>("GetLayer", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Collider2D::*)(PhysicsLayerMask)>(&Handle::Collider2D::SetLayer)>("SetLayer", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<PhysicsLayerMask (Handle::Collider2D::*)() const>(&Handle::Collider2D::GetMask)>("GetMask", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Collider2D::*)(PhysicsLayerMask)>(&Handle::Collider2D::SetMask)>("SetMask", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Collider2D::IsValid>("valid");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Collider2D::GetGameObject>("gameObject");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Collider2D::IsEnabled, &Handle::Collider2D::SetEnabled>("enabled");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Collider2D::GetShape, &Handle::Collider2D::SetShape>("shape");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Collider2D::GetOffset, &Handle::Collider2D::SetOffset>("offset");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Collider2D::GetSize, &Handle::Collider2D::SetSize>("size");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Collider2D::GetRadius, &Handle::Collider2D::SetRadius>("radius");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Collider2D::GetIsTrigger, &Handle::Collider2D::SetIsTrigger>("isTrigger");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Collider2D::GetLoop, &Handle::Collider2D::SetLoop>("loop");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Collider2D::GetOneWay, &Handle::Collider2D::SetOneWay>("oneWay");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Collider2D::GetFriction, &Handle::Collider2D::SetFriction>("friction");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Collider2D::GetRestitution, &Handle::Collider2D::SetRestitution>("restitution");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Collider2D::GetLayer, &Handle::Collider2D::SetLayer>("layer");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Collider2D::GetMask, &Handle::Collider2D::SetMask>("mask");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "DistanceJoint2D", "JBro::Handle::DistanceJoint2D", ApiTypeKind::Handle);
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::DistanceJoint2D::*)() const>(&Handle::DistanceJoint2D::IsValid)>("IsValid", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::DistanceJoint2D::*)() const>(&Handle::DistanceJoint2D::GetGameObject)>("GetGameObject", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::DistanceJoint2D::*)() const>(&Handle::DistanceJoint2D::IsEnabled)>("IsEnabled", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::DistanceJoint2D::*)(Bool)>(&Handle::DistanceJoint2D::SetEnabled)>("SetEnabled", Parameters(In("enabled")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::DistanceJoint2D::*)() const>(&Handle::DistanceJoint2D::GetConnectedObject)>("GetConnectedObject", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::DistanceJoint2D::*)(Handle::GameObject)>(&Handle::DistanceJoint2D::SetConnectedObject)>("SetConnectedObject", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::DistanceJoint2D::*)() const>(&Handle::DistanceJoint2D::GetAnchor)>("GetAnchor", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::DistanceJoint2D::*)(Vector2)>(&Handle::DistanceJoint2D::SetAnchor)>("SetAnchor", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::DistanceJoint2D::*)() const>(&Handle::DistanceJoint2D::GetConnectedAnchor)>("GetConnectedAnchor", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::DistanceJoint2D::*)(Vector2)>(&Handle::DistanceJoint2D::SetConnectedAnchor)>("SetConnectedAnchor", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::DistanceJoint2D::*)() const>(&Handle::DistanceJoint2D::GetAutoDistance)>("GetAutoDistance", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::DistanceJoint2D::*)(Bool)>(&Handle::DistanceJoint2D::SetAutoDistance)>("SetAutoDistance", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::DistanceJoint2D::*)() const>(&Handle::DistanceJoint2D::GetDistance)>("GetDistance", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::DistanceJoint2D::*)(Float)>(&Handle::DistanceJoint2D::SetDistance)>("SetDistance", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::DistanceJoint2D::*)() const>(&Handle::DistanceJoint2D::GetMaxDistanceOnly)>("GetMaxDistanceOnly", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::DistanceJoint2D::*)(Bool)>(&Handle::DistanceJoint2D::SetMaxDistanceOnly)>("SetMaxDistanceOnly", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::DistanceJoint2D::*)() const>(&Handle::DistanceJoint2D::GetFrequency)>("GetFrequency", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::DistanceJoint2D::*)(Float)>(&Handle::DistanceJoint2D::SetFrequency)>("SetFrequency", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::DistanceJoint2D::*)() const>(&Handle::DistanceJoint2D::GetDampingRatio)>("GetDampingRatio", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::DistanceJoint2D::*)(Float)>(&Handle::DistanceJoint2D::SetDampingRatio)>("SetDampingRatio", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::DistanceJoint2D::*)() const>(&Handle::DistanceJoint2D::GetCollideConnected)>("GetCollideConnected", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::DistanceJoint2D::*)(Bool)>(&Handle::DistanceJoint2D::SetCollideConnected)>("SetCollideConnected", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::DistanceJoint2D::IsValid>("valid");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::DistanceJoint2D::GetGameObject>("gameObject");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::DistanceJoint2D::IsEnabled, &Handle::DistanceJoint2D::SetEnabled>("enabled");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::DistanceJoint2D::GetConnectedObject, &Handle::DistanceJoint2D::SetConnectedObject>("connectedObject");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::DistanceJoint2D::GetAnchor, &Handle::DistanceJoint2D::SetAnchor>("anchor");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::DistanceJoint2D::GetConnectedAnchor, &Handle::DistanceJoint2D::SetConnectedAnchor>("connectedAnchor");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::DistanceJoint2D::GetAutoDistance, &Handle::DistanceJoint2D::SetAutoDistance>("autoDistance");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::DistanceJoint2D::GetDistance, &Handle::DistanceJoint2D::SetDistance>("distance");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::DistanceJoint2D::GetMaxDistanceOnly, &Handle::DistanceJoint2D::SetMaxDistanceOnly>("maxDistanceOnly");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::DistanceJoint2D::GetFrequency, &Handle::DistanceJoint2D::SetFrequency>("frequency");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::DistanceJoint2D::GetDampingRatio, &Handle::DistanceJoint2D::SetDampingRatio>("dampingRatio");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::DistanceJoint2D::GetCollideConnected, &Handle::DistanceJoint2D::SetCollideConnected>("collideConnected");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "HingeJoint2D", "JBro::Handle::HingeJoint2D", ApiTypeKind::Handle);
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::HingeJoint2D::*)() const>(&Handle::HingeJoint2D::IsValid)>("IsValid", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::HingeJoint2D::*)() const>(&Handle::HingeJoint2D::GetGameObject)>("GetGameObject", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::HingeJoint2D::*)() const>(&Handle::HingeJoint2D::IsEnabled)>("IsEnabled", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::HingeJoint2D::*)(Bool)>(&Handle::HingeJoint2D::SetEnabled)>("SetEnabled", Parameters(In("enabled")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::HingeJoint2D::*)() const>(&Handle::HingeJoint2D::GetConnectedObject)>("GetConnectedObject", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::HingeJoint2D::*)(Handle::GameObject)>(&Handle::HingeJoint2D::SetConnectedObject)>("SetConnectedObject", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::HingeJoint2D::*)() const>(&Handle::HingeJoint2D::GetAnchor)>("GetAnchor", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::HingeJoint2D::*)(Vector2)>(&Handle::HingeJoint2D::SetAnchor)>("SetAnchor", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::HingeJoint2D::*)() const>(&Handle::HingeJoint2D::GetConnectedAnchor)>("GetConnectedAnchor", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::HingeJoint2D::*)(Vector2)>(&Handle::HingeJoint2D::SetConnectedAnchor)>("SetConnectedAnchor", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::HingeJoint2D::*)() const>(&Handle::HingeJoint2D::GetAutoConnectedAnchor)>("GetAutoConnectedAnchor", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::HingeJoint2D::*)(Bool)>(&Handle::HingeJoint2D::SetAutoConnectedAnchor)>("SetAutoConnectedAnchor", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::HingeJoint2D::*)() const>(&Handle::HingeJoint2D::GetUseLimits)>("GetUseLimits", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::HingeJoint2D::*)(Bool)>(&Handle::HingeJoint2D::SetUseLimits)>("SetUseLimits", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Degree (Handle::HingeJoint2D::*)() const>(&Handle::HingeJoint2D::GetLowerAngle)>("GetLowerAngle", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::HingeJoint2D::*)(Degree)>(&Handle::HingeJoint2D::SetLowerAngle)>("SetLowerAngle", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Degree (Handle::HingeJoint2D::*)() const>(&Handle::HingeJoint2D::GetUpperAngle)>("GetUpperAngle", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::HingeJoint2D::*)(Degree)>(&Handle::HingeJoint2D::SetUpperAngle)>("SetUpperAngle", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::HingeJoint2D::*)() const>(&Handle::HingeJoint2D::GetUseMotor)>("GetUseMotor", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::HingeJoint2D::*)(Bool)>(&Handle::HingeJoint2D::SetUseMotor)>("SetUseMotor", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Degree (Handle::HingeJoint2D::*)() const>(&Handle::HingeJoint2D::GetMotorSpeed)>("GetMotorSpeed", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::HingeJoint2D::*)(Degree)>(&Handle::HingeJoint2D::SetMotorSpeed)>("SetMotorSpeed", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::HingeJoint2D::*)() const>(&Handle::HingeJoint2D::GetMaxMotorTorque)>("GetMaxMotorTorque", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::HingeJoint2D::*)(Float)>(&Handle::HingeJoint2D::SetMaxMotorTorque)>("SetMaxMotorTorque", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::HingeJoint2D::*)() const>(&Handle::HingeJoint2D::GetCollideConnected)>("GetCollideConnected", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::HingeJoint2D::*)(Bool)>(&Handle::HingeJoint2D::SetCollideConnected)>("SetCollideConnected", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::HingeJoint2D::IsValid>("valid");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::HingeJoint2D::GetGameObject>("gameObject");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::HingeJoint2D::IsEnabled, &Handle::HingeJoint2D::SetEnabled>("enabled");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::HingeJoint2D::GetConnectedObject, &Handle::HingeJoint2D::SetConnectedObject>("connectedObject");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::HingeJoint2D::GetAnchor, &Handle::HingeJoint2D::SetAnchor>("anchor");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::HingeJoint2D::GetConnectedAnchor, &Handle::HingeJoint2D::SetConnectedAnchor>("connectedAnchor");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::HingeJoint2D::GetAutoConnectedAnchor, &Handle::HingeJoint2D::SetAutoConnectedAnchor>("autoConnectedAnchor");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::HingeJoint2D::GetUseLimits, &Handle::HingeJoint2D::SetUseLimits>("useLimits");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::HingeJoint2D::GetLowerAngle, &Handle::HingeJoint2D::SetLowerAngle>("lowerAngle");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::HingeJoint2D::GetUpperAngle, &Handle::HingeJoint2D::SetUpperAngle>("upperAngle");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::HingeJoint2D::GetUseMotor, &Handle::HingeJoint2D::SetUseMotor>("useMotor");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::HingeJoint2D::GetMotorSpeed, &Handle::HingeJoint2D::SetMotorSpeed>("motorSpeed");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::HingeJoint2D::GetMaxMotorTorque, &Handle::HingeJoint2D::SetMaxMotorTorque>("maxMotorTorque");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::HingeJoint2D::GetCollideConnected, &Handle::HingeJoint2D::SetCollideConnected>("collideConnected");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "Light2D", "JBro::Handle::Light2D", ApiTypeKind::Handle);
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Light2D::*)() const>(&Handle::Light2D::IsValid)>("IsValid", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::Light2D::*)() const>(&Handle::Light2D::GetGameObject)>("GetGameObject", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Light2D::*)() const>(&Handle::Light2D::IsEnabled)>("IsEnabled", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Light2D::*)(Bool)>(&Handle::Light2D::SetEnabled)>("SetEnabled", Parameters(In("enabled")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Component::Light2DType (Handle::Light2D::*)() const>(&Handle::Light2D::GetType)>("GetType", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Light2D::*)(Component::Light2DType)>(&Handle::Light2D::SetType)>("SetType", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Color (Handle::Light2D::*)() const>(&Handle::Light2D::GetColor)>("GetColor", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Light2D::*)(Color)>(&Handle::Light2D::SetColor)>("SetColor", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::Light2D::*)() const>(&Handle::Light2D::GetIntensity)>("GetIntensity", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Light2D::*)(Float)>(&Handle::Light2D::SetIntensity)>("SetIntensity", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::Light2D::*)() const>(&Handle::Light2D::GetInnerRadius)>("GetInnerRadius", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Light2D::*)(Float)>(&Handle::Light2D::SetInnerRadius)>("SetInnerRadius", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::Light2D::*)() const>(&Handle::Light2D::GetOuterRadius)>("GetOuterRadius", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Light2D::*)(Float)>(&Handle::Light2D::SetOuterRadius)>("SetOuterRadius", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Degree (Handle::Light2D::*)() const>(&Handle::Light2D::GetInnerAngle)>("GetInnerAngle", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Light2D::*)(Degree)>(&Handle::Light2D::SetInnerAngle)>("SetInnerAngle", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Degree (Handle::Light2D::*)() const>(&Handle::Light2D::GetOuterAngle)>("GetOuterAngle", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Light2D::*)(Degree)>(&Handle::Light2D::SetOuterAngle)>("SetOuterAngle", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Light2D::*)() const>(&Handle::Light2D::GetCastShadows)>("GetCastShadows", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Light2D::*)(Bool)>(&Handle::Light2D::SetCastShadows)>("SetCastShadows", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::Light2D::*)() const>(&Handle::Light2D::GetShadowSoftness)>("GetShadowSoftness", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Light2D::*)(Float)>(&Handle::Light2D::SetShadowSoftness)>("SetShadowSoftness", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Light2D::IsValid>("valid");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Light2D::GetGameObject>("gameObject");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Light2D::IsEnabled, &Handle::Light2D::SetEnabled>("enabled");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Light2D::GetType, &Handle::Light2D::SetType>("type");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Light2D::GetColor, &Handle::Light2D::SetColor>("color");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Light2D::GetIntensity, &Handle::Light2D::SetIntensity>("intensity");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Light2D::GetInnerRadius, &Handle::Light2D::SetInnerRadius>("innerRadius");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Light2D::GetOuterRadius, &Handle::Light2D::SetOuterRadius>("outerRadius");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Light2D::GetInnerAngle, &Handle::Light2D::SetInnerAngle>("innerAngle");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Light2D::GetOuterAngle, &Handle::Light2D::SetOuterAngle>("outerAngle");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Light2D::GetCastShadows, &Handle::Light2D::SetCastShadows>("castShadows");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Light2D::GetShadowSoftness, &Handle::Light2D::SetShadowSoftness>("shadowSoftness");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "Rigidbody2D", "JBro::Handle::Rigidbody2D", ApiTypeKind::Handle);
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Rigidbody2D::*)() const>(&Handle::Rigidbody2D::IsValid)>("IsValid", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::Rigidbody2D::*)() const>(&Handle::Rigidbody2D::GetGameObject)>("GetGameObject", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Rigidbody2D::*)() const>(&Handle::Rigidbody2D::IsEnabled)>("IsEnabled", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Rigidbody2D::*)(Bool)>(&Handle::Rigidbody2D::SetEnabled)>("SetEnabled", Parameters(In("enabled")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Component::BodyType2D (Handle::Rigidbody2D::*)() const>(&Handle::Rigidbody2D::GetBodyType)>("GetBodyType", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Rigidbody2D::*)(Component::BodyType2D)>(&Handle::Rigidbody2D::SetBodyType)>("SetBodyType", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::Rigidbody2D::*)() const>(&Handle::Rigidbody2D::GetLinearVelocity)>("GetLinearVelocity", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Rigidbody2D::*)(Vector2)>(&Handle::Rigidbody2D::SetLinearVelocity)>("SetLinearVelocity", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::Rigidbody2D::*)() const>(&Handle::Rigidbody2D::GetAngularVelocity)>("GetAngularVelocity", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Rigidbody2D::*)(Float)>(&Handle::Rigidbody2D::SetAngularVelocity)>("SetAngularVelocity", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::Rigidbody2D::*)() const>(&Handle::Rigidbody2D::GetMass)>("GetMass", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Rigidbody2D::*)(Float)>(&Handle::Rigidbody2D::SetMass)>("SetMass", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::Rigidbody2D::*)() const>(&Handle::Rigidbody2D::GetGravityScale)>("GetGravityScale", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Rigidbody2D::*)(Float)>(&Handle::Rigidbody2D::SetGravityScale)>("SetGravityScale", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::Rigidbody2D::*)() const>(&Handle::Rigidbody2D::GetLinearDamping)>("GetLinearDamping", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Rigidbody2D::*)(Float)>(&Handle::Rigidbody2D::SetLinearDamping)>("SetLinearDamping", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::Rigidbody2D::*)() const>(&Handle::Rigidbody2D::GetAngularDamping)>("GetAngularDamping", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Rigidbody2D::*)(Float)>(&Handle::Rigidbody2D::SetAngularDamping)>("SetAngularDamping", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Rigidbody2D::*)() const>(&Handle::Rigidbody2D::GetFixedRotation)>("GetFixedRotation", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Rigidbody2D::*)(Bool)>(&Handle::Rigidbody2D::SetFixedRotation)>("SetFixedRotation", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Rigidbody2D::*)() const>(&Handle::Rigidbody2D::GetFreezePositionX)>("GetFreezePositionX", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Rigidbody2D::*)(Bool)>(&Handle::Rigidbody2D::SetFreezePositionX)>("SetFreezePositionX", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Rigidbody2D::*)() const>(&Handle::Rigidbody2D::GetFreezePositionY)>("GetFreezePositionY", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Rigidbody2D::*)(Bool)>(&Handle::Rigidbody2D::SetFreezePositionY)>("SetFreezePositionY", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Rigidbody2D::*)() const>(&Handle::Rigidbody2D::GetCanSleep)>("GetCanSleep", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Rigidbody2D::*)(Bool)>(&Handle::Rigidbody2D::SetCanSleep)>("SetCanSleep", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Rigidbody2D::*)(Vector2)>(&Handle::Rigidbody2D::AddForce)>("AddForce", Parameters(In("force")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Rigidbody2D::*)(Vector2,Vector2)>(&Handle::Rigidbody2D::AddForceAtPosition)>("AddForceAtPosition", Parameters(In("force"), In("worldPoint")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Rigidbody2D::*)(Float)>(&Handle::Rigidbody2D::AddTorque)>("AddTorque", Parameters(In("torque")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Rigidbody2D::*)(Vector2)>(&Handle::Rigidbody2D::AddImpulse)>("AddImpulse", Parameters(In("impulse")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Rigidbody2D::*)(Vector2,Vector2)>(&Handle::Rigidbody2D::AddImpulseAtPosition)>("AddImpulseAtPosition", Parameters(In("impulse"), In("worldPoint")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Rigidbody2D::*)(Float)>(&Handle::Rigidbody2D::AddAngularImpulse)>("AddAngularImpulse", Parameters(In("impulse")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Rigidbody2D::*)()>(&Handle::Rigidbody2D::WakeUp)>("WakeUp", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Rigidbody2D::*)() const>(&Handle::Rigidbody2D::IsSleeping)>("IsSleeping", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Rigidbody2D::IsValid>("valid");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Rigidbody2D::GetGameObject>("gameObject");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Rigidbody2D::IsEnabled, &Handle::Rigidbody2D::SetEnabled>("enabled");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Rigidbody2D::GetBodyType, &Handle::Rigidbody2D::SetBodyType>("bodyType");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Rigidbody2D::GetLinearVelocity, &Handle::Rigidbody2D::SetLinearVelocity>("linearVelocity");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Rigidbody2D::GetAngularVelocity, &Handle::Rigidbody2D::SetAngularVelocity>("angularVelocity");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Rigidbody2D::GetMass, &Handle::Rigidbody2D::SetMass>("mass");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Rigidbody2D::GetGravityScale, &Handle::Rigidbody2D::SetGravityScale>("gravityScale");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Rigidbody2D::GetLinearDamping, &Handle::Rigidbody2D::SetLinearDamping>("linearDamping");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Rigidbody2D::GetAngularDamping, &Handle::Rigidbody2D::SetAngularDamping>("angularDamping");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Rigidbody2D::GetFixedRotation, &Handle::Rigidbody2D::SetFixedRotation>("fixedRotation");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Rigidbody2D::GetFreezePositionX, &Handle::Rigidbody2D::SetFreezePositionX>("freezePositionX");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Rigidbody2D::GetFreezePositionY, &Handle::Rigidbody2D::SetFreezePositionY>("freezePositionY");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Rigidbody2D::GetCanSleep, &Handle::Rigidbody2D::SetCanSleep>("canSleep");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Rigidbody2D::IsSleeping>("sleeping");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "ShadowCaster2D", "JBro::Handle::ShadowCaster2D", ApiTypeKind::Handle);
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::ShadowCaster2D::*)() const>(&Handle::ShadowCaster2D::IsValid)>("IsValid", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::ShadowCaster2D::*)() const>(&Handle::ShadowCaster2D::GetGameObject)>("GetGameObject", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::ShadowCaster2D::*)() const>(&Handle::ShadowCaster2D::IsEnabled)>("IsEnabled", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::ShadowCaster2D::*)(Bool)>(&Handle::ShadowCaster2D::SetEnabled)>("SetEnabled", Parameters(In("enabled")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Component::ShadowShape2D (Handle::ShadowCaster2D::*)() const>(&Handle::ShadowCaster2D::GetShape)>("GetShape", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::ShadowCaster2D::*)(Component::ShadowShape2D)>(&Handle::ShadowCaster2D::SetShape)>("SetShape", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::ShadowCaster2D::*)() const>(&Handle::ShadowCaster2D::GetSelfShadow)>("GetSelfShadow", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::ShadowCaster2D::*)(Bool)>(&Handle::ShadowCaster2D::SetSelfShadow)>("SetSelfShadow", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::ShadowCaster2D::IsValid>("valid");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::ShadowCaster2D::GetGameObject>("gameObject");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::ShadowCaster2D::IsEnabled, &Handle::ShadowCaster2D::SetEnabled>("enabled");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::ShadowCaster2D::GetShape, &Handle::ShadowCaster2D::SetShape>("shape");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::ShadowCaster2D::GetSelfShadow, &Handle::ShadowCaster2D::SetSelfShadow>("selfShadow");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "SpriteRenderer2D", "JBro::Handle::SpriteRenderer2D", ApiTypeKind::Handle);
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::SpriteRenderer2D::*)() const>(&Handle::SpriteRenderer2D::IsValid)>("IsValid", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::SpriteRenderer2D::*)() const>(&Handle::SpriteRenderer2D::GetGameObject)>("GetGameObject", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::SpriteRenderer2D::*)() const>(&Handle::SpriteRenderer2D::IsEnabled)>("IsEnabled", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::SpriteRenderer2D::*)(Bool)>(&Handle::SpriteRenderer2D::SetEnabled)>("SetEnabled", Parameters(In("enabled")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<AssetId (Handle::SpriteRenderer2D::*)() const>(&Handle::SpriteRenderer2D::GetSpriteId)>("GetSpriteId", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<AssetHandle (Handle::SpriteRenderer2D::*)() const>(&Handle::SpriteRenderer2D::GetSprite)>("GetSprite", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::SpriteRenderer2D::*)(AssetHandle)>(&Handle::SpriteRenderer2D::SetSprite)>("SetSprite", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<AssetId (Handle::SpriteRenderer2D::*)() const>(&Handle::SpriteRenderer2D::GetMaterialId)>("GetMaterialId", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<AssetHandle (Handle::SpriteRenderer2D::*)() const>(&Handle::SpriteRenderer2D::GetMaterial)>("GetMaterial", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::SpriteRenderer2D::*)(AssetHandle)>(&Handle::SpriteRenderer2D::SetMaterial)>("SetMaterial", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Color (Handle::SpriteRenderer2D::*)() const>(&Handle::SpriteRenderer2D::GetTint)>("GetTint", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::SpriteRenderer2D::*)(Color)>(&Handle::SpriteRenderer2D::SetTint)>("SetTint", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Component::SpriteSizeMode (Handle::SpriteRenderer2D::*)() const>(&Handle::SpriteRenderer2D::GetSizeMode)>("GetSizeMode", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::SpriteRenderer2D::*)(Component::SpriteSizeMode)>(&Handle::SpriteRenderer2D::SetSizeMode)>("SetSizeMode", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Component::SpritePivotMode (Handle::SpriteRenderer2D::*)() const>(&Handle::SpriteRenderer2D::GetPivotMode)>("GetPivotMode", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::SpriteRenderer2D::*)(Component::SpritePivotMode)>(&Handle::SpriteRenderer2D::SetPivotMode)>("SetPivotMode", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::SpriteRenderer2D::*)() const>(&Handle::SpriteRenderer2D::GetPivot)>("GetPivot", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::SpriteRenderer2D::*)(Vector2)>(&Handle::SpriteRenderer2D::SetPivot)>("SetPivot", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::SpriteRenderer2D::*)() const>(&Handle::SpriteRenderer2D::GetSize)>("GetSize", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::SpriteRenderer2D::*)(Vector2)>(&Handle::SpriteRenderer2D::SetSize)>("SetSize", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Component::SpriteFlip (Handle::SpriteRenderer2D::*)() const>(&Handle::SpriteRenderer2D::GetFlip)>("GetFlip", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::SpriteRenderer2D::*)(Component::SpriteFlip)>(&Handle::SpriteRenderer2D::SetFlip)>("SetFlip", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<UInt32 (Handle::SpriteRenderer2D::*)() const>(&Handle::SpriteRenderer2D::GetFrameIndex)>("GetFrameIndex", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::SpriteRenderer2D::*)(UInt32)>(&Handle::SpriteRenderer2D::SetFrameIndex)>("SetFrameIndex", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Int32 (Handle::SpriteRenderer2D::*)() const>(&Handle::SpriteRenderer2D::GetRenderOrder)>("GetRenderOrder", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::SpriteRenderer2D::*)(Int32)>(&Handle::SpriteRenderer2D::SetRenderOrder)>("SetRenderOrder", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::SpriteRenderer2D::*)() const>(&Handle::SpriteRenderer2D::GetVisible)>("GetVisible", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::SpriteRenderer2D::*)(Bool)>(&Handle::SpriteRenderer2D::SetVisible)>("SetVisible", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Component::SpriteDrawMode (Handle::SpriteRenderer2D::*)() const>(&Handle::SpriteRenderer2D::GetDrawMode)>("GetDrawMode", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::SpriteRenderer2D::*)(Component::SpriteDrawMode)>(&Handle::SpriteRenderer2D::SetDrawMode)>("SetDrawMode", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::SpriteRenderer2D::*)() const>(&Handle::SpriteRenderer2D::GetFillAmount)>("GetFillAmount", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::SpriteRenderer2D::*)(Float)>(&Handle::SpriteRenderer2D::SetFillAmount)>("SetFillAmount", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Component::SpriteFillOrigin (Handle::SpriteRenderer2D::*)() const>(&Handle::SpriteRenderer2D::GetFillOrigin)>("GetFillOrigin", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::SpriteRenderer2D::*)(Component::SpriteFillOrigin)>(&Handle::SpriteRenderer2D::SetFillOrigin)>("SetFillOrigin", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::SpriteRenderer2D::IsValid>("valid");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::SpriteRenderer2D::GetGameObject>("gameObject");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::SpriteRenderer2D::IsEnabled, &Handle::SpriteRenderer2D::SetEnabled>("enabled");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::SpriteRenderer2D::GetSpriteId>("spriteId");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::SpriteRenderer2D::GetSprite, &Handle::SpriteRenderer2D::SetSprite>("sprite");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::SpriteRenderer2D::GetMaterialId>("materialId");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::SpriteRenderer2D::GetMaterial, &Handle::SpriteRenderer2D::SetMaterial>("material");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::SpriteRenderer2D::GetTint, &Handle::SpriteRenderer2D::SetTint>("tint");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::SpriteRenderer2D::GetSizeMode, &Handle::SpriteRenderer2D::SetSizeMode>("sizeMode");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::SpriteRenderer2D::GetPivotMode, &Handle::SpriteRenderer2D::SetPivotMode>("pivotMode");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::SpriteRenderer2D::GetPivot, &Handle::SpriteRenderer2D::SetPivot>("pivot");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::SpriteRenderer2D::GetSize, &Handle::SpriteRenderer2D::SetSize>("size");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::SpriteRenderer2D::GetFlip, &Handle::SpriteRenderer2D::SetFlip>("flip");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::SpriteRenderer2D::GetFrameIndex, &Handle::SpriteRenderer2D::SetFrameIndex>("frameIndex");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::SpriteRenderer2D::GetRenderOrder, &Handle::SpriteRenderer2D::SetRenderOrder>("renderOrder");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::SpriteRenderer2D::GetVisible, &Handle::SpriteRenderer2D::SetVisible>("visible");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::SpriteRenderer2D::GetDrawMode, &Handle::SpriteRenderer2D::SetDrawMode>("drawMode");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::SpriteRenderer2D::GetFillAmount, &Handle::SpriteRenderer2D::SetFillAmount>("fillAmount");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::SpriteRenderer2D::GetFillOrigin, &Handle::SpriteRenderer2D::SetFillOrigin>("fillOrigin");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "Text2D", "JBro::Handle::Text2D", ApiTypeKind::Handle);
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Text2D::*)() const>(&Handle::Text2D::IsValid)>("IsValid", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::Text2D::*)() const>(&Handle::Text2D::GetGameObject)>("GetGameObject", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Text2D::*)() const>(&Handle::Text2D::IsEnabled)>("IsEnabled", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Text2D::*)(Bool)>(&Handle::Text2D::SetEnabled)>("SetEnabled", Parameters(In("enabled")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<AssetId (Handle::Text2D::*)() const>(&Handle::Text2D::GetFontId)>("GetFontId", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<AssetHandle (Handle::Text2D::*)() const>(&Handle::Text2D::GetFont)>("GetFont", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Text2D::*)(AssetHandle)>(&Handle::Text2D::SetFont)>("SetFont", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::Text2D::*)() const>(&Handle::Text2D::GetFontSize)>("GetFontSize", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Text2D::*)(Float)>(&Handle::Text2D::SetFontSize)>("SetFontSize", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::Text2D::*)() const>(&Handle::Text2D::GetBoxSize)>("GetBoxSize", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Text2D::*)(Vector2)>(&Handle::Text2D::SetBoxSize)>("SetBoxSize", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Component::TextOverflow (Handle::Text2D::*)() const>(&Handle::Text2D::GetOverflow)>("GetOverflow", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Text2D::*)(Component::TextOverflow)>(&Handle::Text2D::SetOverflow)>("SetOverflow", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Component::TextWrapMode (Handle::Text2D::*)() const>(&Handle::Text2D::GetWrapMode)>("GetWrapMode", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Text2D::*)(Component::TextWrapMode)>(&Handle::Text2D::SetWrapMode)>("SetWrapMode", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Component::TextAlignX (Handle::Text2D::*)() const>(&Handle::Text2D::GetAlignX)>("GetAlignX", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Text2D::*)(Component::TextAlignX)>(&Handle::Text2D::SetAlignX)>("SetAlignX", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Component::TextAlignY (Handle::Text2D::*)() const>(&Handle::Text2D::GetAlignY)>("GetAlignY", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Text2D::*)(Component::TextAlignY)>(&Handle::Text2D::SetAlignY)>("SetAlignY", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::Text2D::*)() const>(&Handle::Text2D::GetLineSpacing)>("GetLineSpacing", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Text2D::*)(Float)>(&Handle::Text2D::SetLineSpacing)>("SetLineSpacing", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::Text2D::*)() const>(&Handle::Text2D::GetLetterSpacing)>("GetLetterSpacing", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Text2D::*)(Float)>(&Handle::Text2D::SetLetterSpacing)>("SetLetterSpacing", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Color (Handle::Text2D::*)() const>(&Handle::Text2D::GetColor)>("GetColor", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Text2D::*)(Color)>(&Handle::Text2D::SetColor)>("SetColor", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Int32 (Handle::Text2D::*)() const>(&Handle::Text2D::GetRenderOrder)>("GetRenderOrder", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Text2D::*)(Int32)>(&Handle::Text2D::SetRenderOrder)>("SetRenderOrder", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Text2D::*)() const>(&Handle::Text2D::GetVisible)>("GetVisible", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Text2D::*)(Bool)>(&Handle::Text2D::SetVisible)>("SetVisible", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Color (Handle::Text2D::*)() const>(&Handle::Text2D::GetOutlineColor)>("GetOutlineColor", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Text2D::*)(Color)>(&Handle::Text2D::SetOutlineColor)>("SetOutlineColor", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::Text2D::*)() const>(&Handle::Text2D::GetOutlineWidth)>("GetOutlineWidth", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Text2D::*)(Float)>(&Handle::Text2D::SetOutlineWidth)>("SetOutlineWidth", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Text2D::*)() const>(&Handle::Text2D::GetAutoSize)>("GetAutoSize", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Text2D::*)(Bool)>(&Handle::Text2D::SetAutoSize)>("SetAutoSize", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::Text2D::*)() const>(&Handle::Text2D::GetMinFontSize)>("GetMinFontSize", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Text2D::*)(Float)>(&Handle::Text2D::SetMinFontSize)>("SetMinFontSize", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::Text2D::*)() const>(&Handle::Text2D::GetMaxFontSize)>("GetMaxFontSize", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Text2D::*)(Float)>(&Handle::Text2D::SetMaxFontSize)>("SetMaxFontSize", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Text2D::*)() const>(&Handle::Text2D::GetPixelSnap)>("GetPixelSnap", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Text2D::*)(Bool)>(&Handle::Text2D::SetPixelSnap)>("SetPixelSnap", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Text2D::*)() const>(&Handle::Text2D::GetRichText)>("GetRichText", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Text2D::*)(Bool)>(&Handle::Text2D::SetRichText)>("SetRichText", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Text2D::IsValid>("valid");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Text2D::GetGameObject>("gameObject");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Text2D::IsEnabled, &Handle::Text2D::SetEnabled>("enabled");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Text2D::GetFontId>("fontId");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Text2D::GetFont, &Handle::Text2D::SetFont>("font");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Text2D::GetFontSize, &Handle::Text2D::SetFontSize>("fontSize");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Text2D::GetBoxSize, &Handle::Text2D::SetBoxSize>("boxSize");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Text2D::GetOverflow, &Handle::Text2D::SetOverflow>("overflow");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Text2D::GetWrapMode, &Handle::Text2D::SetWrapMode>("wrapMode");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Text2D::GetAlignX, &Handle::Text2D::SetAlignX>("alignX");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Text2D::GetAlignY, &Handle::Text2D::SetAlignY>("alignY");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Text2D::GetLineSpacing, &Handle::Text2D::SetLineSpacing>("lineSpacing");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Text2D::GetLetterSpacing, &Handle::Text2D::SetLetterSpacing>("letterSpacing");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Text2D::GetColor, &Handle::Text2D::SetColor>("color");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Text2D::GetRenderOrder, &Handle::Text2D::SetRenderOrder>("renderOrder");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Text2D::GetVisible, &Handle::Text2D::SetVisible>("visible");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Text2D::GetOutlineColor, &Handle::Text2D::SetOutlineColor>("outlineColor");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Text2D::GetOutlineWidth, &Handle::Text2D::SetOutlineWidth>("outlineWidth");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Text2D::GetAutoSize, &Handle::Text2D::SetAutoSize>("autoSize");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Text2D::GetMinFontSize, &Handle::Text2D::SetMinFontSize>("minFontSize");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Text2D::GetMaxFontSize, &Handle::Text2D::SetMaxFontSize>("maxFontSize");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Text2D::GetPixelSnap, &Handle::Text2D::SetPixelSnap>("pixelSnap");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Text2D::GetRichText, &Handle::Text2D::SetRichText>("richText");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "Transform2D", "JBro::Handle::Transform2D", ApiTypeKind::Handle);
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Transform2D::*)() const>(&Handle::Transform2D::IsValid)>("IsValid", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::Transform2D::*)() const>(&Handle::Transform2D::GetGameObject)>("GetGameObject", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Transform2D::*)() const>(&Handle::Transform2D::IsEnabled)>("IsEnabled", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Transform2D::*)(Bool)>(&Handle::Transform2D::SetEnabled)>("SetEnabled", Parameters(In("enabled")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::Transform2D::*)() const>(&Handle::Transform2D::GetPosition)>("GetPosition", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Transform2D::*)(Vector2)>(&Handle::Transform2D::SetPosition)>("SetPosition", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Degree (Handle::Transform2D::*)() const>(&Handle::Transform2D::GetRotation)>("GetRotation", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Transform2D::*)(Degree)>(&Handle::Transform2D::SetRotation)>("SetRotation", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::Transform2D::*)() const>(&Handle::Transform2D::GetScale)>("GetScale", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Transform2D::*)(Vector2)>(&Handle::Transform2D::SetScale)>("SetScale", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::Transform2D::*)() const>(&Handle::Transform2D::GetAnchor)>("GetAnchor", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::Transform2D::*)(Vector2)>(&Handle::Transform2D::SetAnchor)>("SetAnchor", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Matrix3x2 (Handle::Transform2D::*)() const>(&Handle::Transform2D::GetWorld)>("GetWorld", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::Transform2D::*)() const>(&Handle::Transform2D::GetWorldPosition)>("GetWorldPosition", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Degree (Handle::Transform2D::*)() const>(&Handle::Transform2D::GetWorldRotation)>("GetWorldRotation", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Vector2 (Handle::Transform2D::*)() const>(&Handle::Transform2D::GetWorldScale)>("GetWorldScale", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::Transform2D::*)() const>(&Handle::Transform2D::GetWorldValid)>("GetWorldValid", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Transform2D::IsValid>("valid");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Transform2D::GetGameObject>("gameObject");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Transform2D::IsEnabled, &Handle::Transform2D::SetEnabled>("enabled");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Transform2D::GetPosition, &Handle::Transform2D::SetPosition>("position");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Transform2D::GetRotation, &Handle::Transform2D::SetRotation>("rotation");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Transform2D::GetScale, &Handle::Transform2D::SetScale>("scale");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Transform2D::GetAnchor, &Handle::Transform2D::SetAnchor>("anchor");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Transform2D::GetWorld>("world");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Transform2D::GetWorldPosition>("worldPosition");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Transform2D::GetWorldRotation>("worldRotation");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Transform2D::GetWorldScale>("worldScale");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::Transform2D::GetWorldValid>("worldValid");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "AudioSource", "JBro::Handle::AudioSource", ApiTypeKind::Handle);
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::AudioSource::*)() const>(&Handle::AudioSource::IsValid)>("IsValid", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Handle::GameObject (Handle::AudioSource::*)() const>(&Handle::AudioSource::GetGameObject)>("GetGameObject", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::AudioSource::*)() const>(&Handle::AudioSource::IsEnabled)>("IsEnabled", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::AudioSource::*)(Bool)>(&Handle::AudioSource::SetEnabled)>("SetEnabled", Parameters(In("enabled")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<AssetId (Handle::AudioSource::*)() const>(&Handle::AudioSource::GetClipId)>("GetClipId", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<AssetHandle (Handle::AudioSource::*)() const>(&Handle::AudioSource::GetClip)>("GetClip", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::AudioSource::*)(AssetHandle)>(&Handle::AudioSource::SetClip)>("SetClip", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<AudioBusName (Handle::AudioSource::*)() const>(&Handle::AudioSource::GetBus)>("GetBus", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::AudioSource::*)(AudioBusName)>(&Handle::AudioSource::SetBus)>("SetBus", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::AudioSource::*)() const>(&Handle::AudioSource::GetVolume)>("GetVolume", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::AudioSource::*)(Float)>(&Handle::AudioSource::SetVolume)>("SetVolume", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::AudioSource::*)() const>(&Handle::AudioSource::GetPitch)>("GetPitch", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::AudioSource::*)(Float)>(&Handle::AudioSource::SetPitch)>("SetPitch", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::AudioSource::*)() const>(&Handle::AudioSource::GetLoop)>("GetLoop", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::AudioSource::*)(Bool)>(&Handle::AudioSource::SetLoop)>("SetLoop", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::AudioSource::*)() const>(&Handle::AudioSource::GetPlayOnStart)>("GetPlayOnStart", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::AudioSource::*)(Bool)>(&Handle::AudioSource::SetPlayOnStart)>("SetPlayOnStart", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Bool (Handle::AudioSource::*)() const>(&Handle::AudioSource::GetSpatial)>("GetSpatial", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::AudioSource::*)(Bool)>(&Handle::AudioSource::SetSpatial)>("SetSpatial", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<AudioAttenuation (Handle::AudioSource::*)() const>(&Handle::AudioSource::GetAttenuation)>("GetAttenuation", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::AudioSource::*)(AudioAttenuation)>(&Handle::AudioSource::SetAttenuation)>("SetAttenuation", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::AudioSource::*)() const>(&Handle::AudioSource::GetMinDistance)>("GetMinDistance", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::AudioSource::*)(Float)>(&Handle::AudioSource::SetMinDistance)>("SetMinDistance", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::AudioSource::*)() const>(&Handle::AudioSource::GetMaxDistance)>("GetMaxDistance", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::AudioSource::*)(Float)>(&Handle::AudioSource::SetMaxDistance)>("SetMaxDistance", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::AudioSource::*)() const>(&Handle::AudioSource::GetRolloff)>("GetRolloff", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::AudioSource::*)(Float)>(&Handle::AudioSource::SetRolloff)>("SetRolloff", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::AudioSource::*)() const>(&Handle::AudioSource::GetDoppler)>("GetDoppler", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::AudioSource::*)(Float)>(&Handle::AudioSource::SetDoppler)>("SetDoppler", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Int32 (Handle::AudioSource::*)() const>(&Handle::AudioSource::GetPriority)>("GetPriority", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::AudioSource::*)(Int32)>(&Handle::AudioSource::SetPriority)>("SetPriority", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::AudioSource::*)() const>(&Handle::AudioSource::GetFadeIn)>("GetFadeIn", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::AudioSource::*)(Float)>(&Handle::AudioSource::SetFadeIn)>("SetFadeIn", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::AudioSource::*)() const>(&Handle::AudioSource::GetLowPass)>("GetLowPass", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::AudioSource::*)(Float)>(&Handle::AudioSource::SetLowPass)>("SetLowPass", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Float (Handle::AudioSource::*)() const>(&Handle::AudioSource::GetHighPass)>("GetHighPass", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::AudioSource::*)(Float)>(&Handle::AudioSource::SetHighPass)>("SetHighPass", Parameters(In("value")));
        member.MutatesReceiver = true;
        member.MayInvalidateHandles = true;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<void (Handle::AudioSource::*)(Array<AssetId>&) const>(&Handle::AudioSource::GetAudioEffectIds)>("GetAudioEffectIds", Parameters(Out("ids")));
        member.Type = TypeName<Array<AssetId>>();
        member.Projection = ApiProjectionKind::CollectionReturn;
        member.HasOutputProjection = true;
        member.SuccessFromNativeReturn = false;
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareFunction<static_cast<Component::AudioSourceState (Handle::AudioSource::*)() const>(&Handle::AudioSource::GetState)>("GetState", Parameters());
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::AudioSource::IsValid>("valid");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::AudioSource::GetGameObject>("gameObject");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::AudioSource::IsEnabled, &Handle::AudioSource::SetEnabled>("enabled");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::AudioSource::GetClipId>("clipId");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::AudioSource::GetClip, &Handle::AudioSource::SetClip>("clip");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::AudioSource::GetBus, &Handle::AudioSource::SetBus>("bus");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::AudioSource::GetVolume, &Handle::AudioSource::SetVolume>("volume");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::AudioSource::GetPitch, &Handle::AudioSource::SetPitch>("pitch");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::AudioSource::GetLoop, &Handle::AudioSource::SetLoop>("loop");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::AudioSource::GetPlayOnStart, &Handle::AudioSource::SetPlayOnStart>("playOnStart");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::AudioSource::GetSpatial, &Handle::AudioSource::SetSpatial>("spatial");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::AudioSource::GetAttenuation, &Handle::AudioSource::SetAttenuation>("attenuation");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::AudioSource::GetMinDistance, &Handle::AudioSource::SetMinDistance>("minDistance");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::AudioSource::GetMaxDistance, &Handle::AudioSource::SetMaxDistance>("maxDistance");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::AudioSource::GetRolloff, &Handle::AudioSource::SetRolloff>("rolloff");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::AudioSource::GetDoppler, &Handle::AudioSource::SetDoppler>("doppler");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::AudioSource::GetPriority, &Handle::AudioSource::SetPriority>("priority");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::AudioSource::GetFadeIn, &Handle::AudioSource::SetFadeIn>("fadeIn");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::AudioSource::GetLowPass, &Handle::AudioSource::SetLowPass>("lowPass");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::AudioSource::GetHighPass, &Handle::AudioSource::SetHighPass>("highPass");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareProperty<&Handle::AudioSource::GetState>("state");
        type.Members.Add(std::move(member));
    }
    }
}
}
