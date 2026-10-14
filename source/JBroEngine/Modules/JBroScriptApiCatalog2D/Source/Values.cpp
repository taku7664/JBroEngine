#include "CatalogBuilder.h"
#include <JBro/Reflection/PropertyInfo.h>
namespace JBro::ScriptApiCatalog2D::Internal
{
static String ReflectedType(const TypeDescriptor& descriptor)
{
    const String name = NameTable::Get().Resolve(descriptor.typeName);
    if (name == "float" || name == "double")
    {
        return "Float";
    }
    if (name == "bool")
    {
        return "Bool";
    }
    if (name == "string")
    {
        return "String";
    }
    if (name == "int8" || name == "int16" || name == "int32" || name == "int64" || name == "uint8" || name == "uint16" || name == "uint32" || name == "uint64")
    {
        return "Int";
    }
    if (name.StartsWith("JBro."))
    {
        return name.Substr(5);
    }
    if (name.StartsWith("Component::"))
    {
        return name.Substr(11);
    }
    throw std::runtime_error("reflected public API field type has no language mapping: " + name.Std());
}
template<class T> static void AddReflectedFields(ApiType& type)
{
    const auto& descriptor = TypeDescriptorOf<T>::Get();
    if (descriptor.fields == nullptr)
    {
        return;
    }
    for (UInt32 index = 0; index < descriptor.fields->count; ++index)
    {
        const auto& field = descriptor.fields->properties[index];
        ApiMember member;
        member.Name = NameTable::Get().Resolve(field.name);
        member.NativeName = member.Name;
        member.Kind = ApiMemberKind::Field;
        member.Type = ReflectedType(*field.type);
        // Address is actual mutability. Inspector editability has no language meaning.
        member.Writable = field.Address != nullptr;
        type.Members.Add(std::move(member));
    }
}
void AddValues(ApiCatalog& catalog)
{
    {
    auto& type = AddType(catalog, "Vector2", "JBro::Vector2", ApiTypeKind::Value);
    AddReflectedFields<Vector2>(type);
    }
    {
    auto& type = AddType(catalog, "Rect", "JBro::Rect", ApiTypeKind::Value);
    AddReflectedFields<Rect>(type);
    }
    {
    auto& type = AddType(catalog, "Matrix3x2", "JBro::Matrix3x2", ApiTypeKind::Value);
    AddReflectedFields<Matrix3x2>(type);
    }
    {
    auto& type = AddType(catalog, "Color", "JBro::Color", ApiTypeKind::Value);
    AddReflectedFields<Color>(type);
    }
    {
    auto& type = AddType(catalog, "AssetHandle", "JBro::AssetHandle", ApiTypeKind::Value);
    AddReflectedFields<AssetHandle>(type);
    }
    {
    auto& type = AddType(catalog, "Collision2D", "JBro::Collision2D", ApiTypeKind::Value);
    {
        auto member = DeclareField<&Collision2D::other>("other");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&Collision2D::bodyType>("bodyType");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&Collision2D::point>("point");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&Collision2D::normal>("normal");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "RaycastHit2D", "JBro::RaycastHit2D", ApiTypeKind::Value);
    {
        auto member = DeclareField<&RaycastHit2D::other>("other");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&RaycastHit2D::bodyType>("bodyType");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&RaycastHit2D::point>("point");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&RaycastHit2D::normal>("normal");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&RaycastHit2D::distance>("distance");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "RandomState", "JBro::RandomState", ApiTypeKind::Value);
    {
        auto member = DeclareField<&RandomState::state>("state");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&RandomState::increment>("increment");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "AudioBusEffects", "JBro::AudioBusEffects", ApiTypeKind::Value);
    {
        auto member = DeclareField<&AudioBusEffects::lowPassHz>("lowPassHz");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&AudioBusEffects::highPassHz>("highPassHz");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&AudioBusEffects::echoDelay>("echoDelay");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&AudioBusEffects::echoFeedback>("echoFeedback");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&AudioBusEffects::echoMix>("echoMix");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&AudioBusEffects::reverbRoom>("reverbRoom");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&AudioBusEffects::reverbDamping>("reverbDamping");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&AudioBusEffects::reverbMix>("reverbMix");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&AudioBusEffects::dry>("dry");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&AudioBusEffects::eqLowHz>("eqLowHz");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&AudioBusEffects::eqLowGain>("eqLowGain");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&AudioBusEffects::eqMidHz>("eqMidHz");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&AudioBusEffects::eqMidGain>("eqMidGain");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&AudioBusEffects::eqHighHz>("eqHighHz");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&AudioBusEffects::eqHighGain>("eqHighGain");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&AudioBusEffects::distortion>("distortion");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&AudioBusEffects::distortionMix>("distortionMix");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&AudioBusEffects::chorusMix>("chorusMix");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&AudioBusEffects::chorusRate>("chorusRate");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&AudioBusEffects::chorusDepth>("chorusDepth");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&AudioBusEffects::pitchShift>("pitchShift");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&AudioBusEffects::compRatio>("compRatio");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&AudioBusEffects::compThreshold>("compThreshold");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&AudioBusEffects::compAttack>("compAttack");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&AudioBusEffects::compRelease>("compRelease");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&AudioBusEffects::compMakeup>("compMakeup");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "ButtonState", "JBro::ButtonState", ApiTypeKind::Value);
    {
        auto member = DeclareField<&ButtonState::down>("down");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&ButtonState::pressCount>("pressCount");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&ButtonState::releaseCount>("releaseCount");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "MouseState", "JBro::MouseState", ApiTypeKind::Value);
    {
        auto member = DeclareField<&MouseState::x>("x");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&MouseState::y>("y");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&MouseState::deltaX>("deltaX");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&MouseState::deltaY>("deltaY");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&MouseState::wheelX>("wheelX");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&MouseState::wheelY>("wheelY");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&MouseState::buttons>("buttons");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "GamepadState", "JBro::GamepadState", ApiTypeKind::Value);
    {
        auto member = DeclareField<&GamepadState::connected>("connected");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&GamepadState::buttons>("buttons");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&GamepadState::axes>("axes");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "TouchPoint", "JBro::TouchPoint", ApiTypeKind::Value);
    {
        auto member = DeclareField<&TouchPoint::id>("id");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&TouchPoint::x>("x");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&TouchPoint::y>("y");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&TouchPoint::phase>("phase");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "TouchState", "JBro::TouchState", ApiTypeKind::Value);
    {
        auto member = DeclareField<&TouchState::count>("count");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&TouchState::points>("points");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "InputBinding", "JBro::InputBinding", ApiTypeKind::Value);
    {
        auto member = DeclareField<&InputBinding::source>("source");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&InputBinding::composite>("composite");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&InputBinding::gamepad>("gamepad");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&InputBinding::code>("code");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "InputVector2", "JBro::InputVector2", ApiTypeKind::Value);
    {
        auto member = DeclareField<&InputVector2::x>("x");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&InputVector2::y>("y");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "InputActionValue", "JBro::InputActionValue", ApiTypeKind::Value);
    {
        auto member = DeclareField<&InputActionValue::x>("x");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&InputActionValue::y>("y");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&InputActionValue::down>("down");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&InputActionValue::pressed>("pressed");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&InputActionValue::released>("released");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "Size", "JBro::Size", ApiTypeKind::Value);
    {
        auto member = DeclareField<&Size::width>("width");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&Size::height>("height");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "SizeU", "JBro::SizeU", ApiTypeKind::Value);
    {
        auto member = DeclareField<&SizeU::width>("width");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&SizeU::height>("height");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "SizeI", "JBro::SizeI", ApiTypeKind::Value);
    {
        auto member = DeclareField<&SizeI::width>("width");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&SizeI::height>("height");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "PhysicsLayerMask", "JBro::PhysicsLayerMask", ApiTypeKind::Value);
    {
        auto member = DeclareField<&PhysicsLayerMask::bits>("bits");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "PrefabAsset", "JBro::PrefabAsset", ApiTypeKind::Value);
    {
        auto member = DeclareField<&PrefabAsset::id>("id");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&PrefabAsset::rootObjectName>("rootObjectName");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "PrefabSpawnParams", "JBro::PrefabSpawnParams", ApiTypeKind::Value);
    {
        auto member = DeclareField<&PrefabSpawnParams::parent>("parent");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&PrefabSpawnParams::preserveSourceIdentity>("preserveSourceIdentity");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "MessageView", "JBro::Network::MessageView", ApiTypeKind::Value);
    {
        auto member = DeclareField<&Network::MessageView::connection>("connection");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&Network::MessageView::data>("data");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&Network::MessageView::size>("size");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&Network::MessageView::messageId>("messageId");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&Network::MessageView::channel>("channel");
        type.Members.Add(std::move(member));
    }
    }
    {
    auto& type = AddType(catalog, "NetworkEvent", "JBro::Network::NetworkEvent", ApiTypeKind::Value);
    {
        auto member = DeclareField<&Network::NetworkEvent::kind>("kind");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&Network::NetworkEvent::reason>("reason");
        type.Members.Add(std::move(member));
    }
    {
        auto member = DeclareField<&Network::NetworkEvent::connection>("connection");
        type.Members.Add(std::move(member));
    }
    }
    {
        auto& type = AddType(catalog, "InputCapture", "JBro::ScriptApiCatalog2D::InputCapture", ApiTypeKind::Value);
        type.Members.Add(DeclareField<&InputCapture::result>("result"));
        type.Members.Add(DeclareField<&InputCapture::binding>("binding"));
    }
    AddType(catalog, "Degree", "JBro::Degree", ApiTypeKind::Value);
    AddType(catalog, "Radian", "JBro::Radian", ApiTypeKind::Value);
    AddType(catalog, "Uuid", "JBro::Uuid", ApiTypeKind::Value);
    AddType(catalog, "AudioBusName", "JBro::AudioBusName", ApiTypeKind::Value);
    AddType(catalog, "RandomStream", "JBro::RandomStream", ApiTypeKind::Value);
    AddType(catalog, "InputView", "JBro::InputView", ApiTypeKind::Class);
    AddType(catalog, "KeyboardState", "JBro::KeyboardState", ApiTypeKind::Value);
    AddType(catalog, "PrefabSpawner", "JBro::PrefabSpawner", ApiTypeKind::Class);
    AddType(catalog, "Layer2D", "JBro::Layer2D", ApiTypeKind::Value);
    AddType(catalog, "BitFlag", "JBro::BitFlag", ApiTypeKind::Value);
    AddType(catalog, "SafeAreaInsets", "JBro::SafeAreaInsets", ApiTypeKind::Value);
    {
    auto& type = AddType(catalog,"SpriteImportOptions","JBro::SpriteImportOptions",ApiTypeKind::Value);
    AddReflectedFields<SpriteImportOptions>(type);
    }
    {
    auto& type = AddType(catalog,"TextureImportOptions","JBro::TextureImportOptions",ApiTypeKind::Value);
    AddReflectedFields<TextureImportOptions>(type);
    }
    {
    auto& type = AddType(catalog,"AudioImportOptions","JBro::AudioImportOptions",ApiTypeKind::Value);
    AddReflectedFields<AudioImportOptions>(type);
    }
    {
    auto& type = AddType(catalog,"FontImportOptions","JBro::FontImportOptions",ApiTypeKind::Value);
    AddReflectedFields<FontImportOptions>(type);
    }
    {
    auto& type = AddType(catalog,"StringTableOptions","JBro::StringTableOptions",ApiTypeKind::Value);
    AddReflectedFields<StringTableOptions>(type);
    }
    {
    auto& type = AddType(catalog,"FontFamilyOptions","JBro::FontFamilyOptions",ApiTypeKind::Value);
    AddReflectedFields<FontFamilyOptions>(type);
    }
    {
    auto& type = AddType(catalog,"AudioEffectParam","JBro::AudioEffectParam",ApiTypeKind::Value);
    type.Members.Add(DeclareField<&AudioEffectParam::name>("name"));
    type.Members.Add(DeclareField<&AudioEffectParam::defaultValue>("defaultValue"));
    type.Members.Add(DeclareField<&AudioEffectParam::minValue>("minValue"));
    type.Members.Add(DeclareField<&AudioEffectParam::maxValue>("maxValue"));
    type.Members.Add(DeclareField<&AudioEffectParam::format>("format"));
    type.Members.Add(DeclareField<&AudioEffectParam::logarithmic>("logarithmic"));
    }
    {
    auto& type = AddType(catalog,"AudioEffectKindInfo","JBro::AudioEffectKindInfo",ApiTypeKind::Value);
    type.Members.Add(DeclareField<&AudioEffectKindInfo::name>("name"));
    type.Members.Add(DeclareField<&AudioEffectKindInfo::paramCount>("paramCount"));
    type.Members.Add(DeclareField<&AudioEffectKindInfo::params>("params"));
    }
    {
    auto& type = AddType(catalog,"AudioEffectSettings","JBro::AudioEffectSettings",ApiTypeKind::Value);
    type.Members.Add(DeclareField<&AudioEffectSettings::kind>("kind"));
    type.Members.Add(DeclareField<&AudioEffectSettings::values>("values"));
    }
    {
    auto& type = AddType(catalog,"SpriteFrame","JBro::SpriteFrame",ApiTypeKind::Value);
    type.Members.Add(DeclareField<&SpriteFrame::x>("x"));
    type.Members.Add(DeclareField<&SpriteFrame::y>("y"));
    type.Members.Add(DeclareField<&SpriteFrame::width>("width"));
    type.Members.Add(DeclareField<&SpriteFrame::height>("height"));
    type.Members.Add(DeclareField<&SpriteFrame::pivotX>("pivotX"));
    type.Members.Add(DeclareField<&SpriteFrame::pivotY>("pivotY"));
    }
    {
    auto& type = AddType(catalog,"AssetMetadata","JBro::AssetMetadata",ApiTypeKind::Value);
    type.Members.Add(DeclareField<&AssetMetadata::id>("id"));
    type.Members.Add(DeclareField<&AssetMetadata::type>("type"));
    type.Members.Add(DeclareField<&AssetMetadata::sourcePath>("sourcePath"));
    }
    {
    auto& type = AddType(catalog,"Vector3","JBro::Vector3",ApiTypeKind::Value);
    AddReflectedFields<Vector3>(type);
    }
    {
    auto& type = AddType(catalog,"Vector4","JBro::Vector4",ApiTypeKind::Value);
    AddReflectedFields<Vector4>(type);
    }
    {
    auto& type = AddType(catalog,"Quaternion","JBro::Quaternion",ApiTypeKind::Value);
    AddReflectedFields<Quaternion>(type);
    }
    {
    auto& type = AddType(catalog,"Matrix4x4","JBro::Matrix4x4",ApiTypeKind::Value);
    type.Members.Add(DeclareField<&Matrix4x4::values>("values"));
    }
}
}
