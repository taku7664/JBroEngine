#include "CatalogBuilder.h"
namespace JBro::ScriptApiCatalog2D::Internal
{
template<auto Function, std::size_t N> static void AddValueMember(ApiType& type, const char* name, const std::array<Parameter,N>& parameters, Bool mutates = false)
{
    auto member = DeclareFunction<Function>(name, parameters);
    member.MutatesReceiver = mutates;
    type.Members.Add(std::move(member));
}
template<class T, class... A> static void AddConstructor(ApiType& type, const std::array<Parameter, sizeof...(A)>& parameters)
{
    static_assert(std::is_constructible_v<T,A...>, "projected constructor must be valid for the actual engine value type");
    ApiMember member;
    member.Name = type.Name;
    member.NativeName = type.Name;
    member.Kind = ApiMemberKind::Constructor;
    member.Type = type.Name;
    member.NativeSignature = NativeTypeName<T>();
    std::size_t index = 0;
    auto parameter = [&](const String& languageType)
    {
        const auto& annotation = parameters[index++];
        member.NativeParameterNames.Add(annotation.Name);
        ParameterInfo value;
        value.Name = annotation.Name;
        value.Type = languageType;
        value.HasDefault = annotation.Default != nullptr;
        if (annotation.Default != nullptr)
        {
            value.DefaultValue = annotation.Default;
        }
        member.Parameters.Add(std::move(value));
    };
    (parameter(TypeName<A>()),...);
    type.Members.Add(std::move(member));
}
void AddValueMembers(ApiCatalog& catalog)
{
    {
    auto& type = FindType(catalog, "KeyboardState");
    AddValueMember<&KeyboardState::IsDown>(type, "IsDown", Parameters(In("key")), false);
    AddValueMember<&KeyboardState::IsPressed>(type, "IsPressed", Parameters(In("key")), false);
    AddValueMember<&KeyboardState::IsReleased>(type, "IsReleased", Parameters(In("key")), false);
    AddValueMember<&KeyboardState::GetTextLength>(type, "GetTextLength", Parameters(), false);
    AddValueMember<&KeyboardState::GetText>(type, "GetText", Parameters(In("index")), false);
    }
    {
    auto& type = FindType(catalog, "MouseState");
    AddValueMember<&MouseState::IsDown>(type, "IsDown", Parameters(In("button")), false);
    AddValueMember<&MouseState::IsPressed>(type, "IsPressed", Parameters(In("button")), false);
    AddValueMember<&MouseState::IsReleased>(type, "IsReleased", Parameters(In("button")), false);
    }
    {
    auto& type = FindType(catalog, "GamepadState");
    AddValueMember<&GamepadState::IsDown>(type, "IsDown", Parameters(In("button")), false);
    AddValueMember<&GamepadState::IsPressed>(type, "IsPressed", Parameters(In("button")), false);
    AddValueMember<&GamepadState::IsReleased>(type, "IsReleased", Parameters(In("button")), false);
    AddValueMember<&GamepadState::GetAxis>(type, "GetAxis", Parameters(In("axis")), false);
    }
    {
    auto& type = FindType(catalog, "TouchPoint");
    AddValueMember<&TouchPoint::IsActive>(type, "IsActive", Parameters(), false);
    }
    {
    auto& type = FindType(catalog, "TouchState");
    AddValueMember<&TouchState::Get>(type, "Get", Parameters(In("index")), false);
    }
    {
    auto& type = FindType(catalog, "InputView");
    AddValueMember<&InputView::Keyboard>(type, "Keyboard", Parameters(), false);
    AddValueMember<&InputView::Mouse>(type, "Mouse", Parameters(), false);
    AddValueMember<&InputView::Gamepad>(type, "Gamepad", Parameters(In("index")), false);
    AddValueMember<&InputView::Touch>(type, "Touch", Parameters(), false);
    AddValueMember<&InputView::Action>(type, "Action", Parameters(In("action")), false);
    AddValueMember<&InputView::IsActionDown>(type, "IsActionDown", Parameters(In("action")), false);
    AddValueMember<&InputView::IsActionPressed>(type, "IsActionPressed", Parameters(In("action")), false);
    AddValueMember<&InputView::IsActionReleased>(type, "IsActionReleased", Parameters(In("action")), false);
    AddValueMember<&InputView::GetActionFloat>(type, "GetActionFloat", Parameters(In("action")), false);
    AddValueMember<&InputView::GetActionVector>(type, "GetActionVector", Parameters(In("action")), false);
    AddValueMember<&InputView::Consume>(type, "Consume", Parameters(In("device")), true);
    AddValueMember<&InputView::ConsumeAll>(type, "ConsumeAll", Parameters(), true);
    AddValueMember<&InputView::IsConsumed>(type, "IsConsumed", Parameters(In("device")), false);
    }
    {
    auto& type = FindType(catalog, "RandomStream");
    AddValueMember<&RandomStream::Seed>(type, "Seed", Parameters(In("seed"),In("stream","0")), true);
    AddValueMember<&RandomStream::NextUInt32>(type, "NextUInt32", Parameters(), true);
    AddValueMember<&RandomStream::NextUInt64>(type, "NextUInt64", Parameters(), true);
    AddValueMember<&RandomStream::Value>(type, "Value", Parameters(), true);
    AddValueMember<&RandomStream::Chance>(type, "Chance", Parameters(In("probability")), true);
    AddValueMember<&RandomStream::GetState>(type, "GetState", Parameters(), false);
    AddValueMember<&RandomStream::SetState>(type, "SetState", Parameters(In("state")), true);
    AddValueMember<static_cast<Int32(RandomStream::*)(Int32,Int32)>(&RandomStream::Range)>(type, "Range", Parameters(In("min"),In("max")), true);
    AddValueMember<static_cast<Float(RandomStream::*)(Float,Float)>(&RandomStream::Range)>(type, "Range", Parameters(In("min"),In("max")), true);
    }
    {
    auto& type = FindType(catalog, "Degree");
    AddValueMember<&Degree::Get>(type, "Get", Parameters(), false);
    AddValueMember<&Degree::Set>(type, "Set", Parameters(In("value")), true);
    AddValueMember<&Degree::ToRadian>(type, "ToRadian", Parameters(), false);
    AddValueMember<&Degree::ToDegree>(type, "ToDegree", Parameters(), false);
    AddValueMember<&Degree::Normalized360>(type, "Normalized360", Parameters(), false);
    AddValueMember<&Degree::Normalized180>(type, "Normalized180", Parameters(), false);
    }
    {
    auto& type = FindType(catalog, "Radian");
    AddValueMember<&Radian::Get>(type, "Get", Parameters(), false);
    AddValueMember<&Radian::Set>(type, "Set", Parameters(In("value")), true);
    AddValueMember<&Radian::ToRadian>(type, "ToRadian", Parameters(), false);
    AddValueMember<&Radian::ToDegree>(type, "ToDegree", Parameters(), false);
    AddValueMember<&Radian::NormalizedTwoPi>(type, "NormalizedTwoPi", Parameters(), false);
    AddValueMember<&Radian::NormalizedPi>(type, "NormalizedPi", Parameters(), false);
    }
    {
    auto& type = FindType(catalog, "Size");
    AddValueMember<&Size::IsEmpty>(type, "IsEmpty", Parameters(), false);
    AddValueMember<&Size::Area>(type, "Area", Parameters(), false);
    AddValueMember<&Size::AspectRatio>(type, "AspectRatio", Parameters(), false);
    }
    {
    auto& type = FindType(catalog, "SizeU");
    AddValueMember<&SizeU::IsEmpty>(type, "IsEmpty", Parameters(), false);
    AddValueMember<&SizeU::Area>(type, "Area", Parameters(), false);
    AddValueMember<&SizeU::AspectRatio>(type, "AspectRatio", Parameters(), false);
    }
    {
    auto& type = FindType(catalog, "SizeI");
    AddValueMember<&SizeI::IsEmpty>(type, "IsEmpty", Parameters(), false);
    AddValueMember<&SizeI::Area>(type, "Area", Parameters(), false);
    AddValueMember<&SizeI::AspectRatio>(type, "AspectRatio", Parameters(), false);
    }
    {
    auto& type = FindType(catalog, "Rect");
    AddValueMember<&Rect::Width>(type, "Width", Parameters(), false);
    AddValueMember<&Rect::Height>(type, "Height", Parameters(), false);
    AddValueMember<&Rect::GetSize>(type, "GetSize", Parameters(), false);
    AddValueMember<&Rect::Center>(type, "Center", Parameters(), false);
    AddValueMember<&Rect::IsEmpty>(type, "IsEmpty", Parameters(), false);
    AddValueMember<&Rect::Intersects>(type, "Intersects", Parameters(In("other")), false);
    AddValueMember<static_cast<Bool(Rect::*)(const Vector2&) const noexcept>(&Rect::Contains)>(type, "Contains", Parameters(In("point")));
    AddValueMember<static_cast<Bool(Rect::*)(const Rect&) const noexcept>(&Rect::Contains)>(type, "Contains", Parameters(In("other")));
    }
    {
    auto& type = FindType(catalog, "Uuid");
    AddValueMember<&Uuid::IsNull>(type, "IsNull", Parameters(), false);
    AddValueMember<&Uuid::GetVersion>(type, "GetVersion", Parameters(), false);
    AddValueMember<&Uuid::Generate>(type, "Generate", Parameters(), false);
    AddValueMember<&Uuid::FromName>(type, "FromName", Parameters(In("name")), false);
    }
    {
    auto& type = FindType(catalog, "AudioBusName");
    AddValueMember<&AudioBusName::FromText>(type, "FromText", Parameters(In("text")), false);
    AddValueMember<&AudioBusName::IsMaster>(type, "IsMaster", Parameters(), false);
    }
    {
    auto& type = FindType(catalog, "PrefabSpawner");
    AddValueMember<&PrefabSpawner::Spawn>(type, "Spawn", Parameters(In("prefabAsset"),In("params")), true);
    AddValueMember<&PrefabSpawner::ApplyOverrides>(type, "ApplyOverrides", Parameters(In("instance"),In("prefabAsset")), true);
    AddValueMember<&PrefabSpawner::DestroyInstance>(type, "DestroyInstance", Parameters(In("instance")), true);
    }
    {
    auto& type = FindType(catalog, "Layer2D");
    AddValueMember<&Layer2D::ForcesOwnTexture>(type, "ForcesOwnTexture", Parameters(), false);
    AddValueMember<&Layer2D::SetForceOwnTexture>(type, "SetForceOwnTexture", Parameters(In("enabled")), true);
    }
    {
    auto& type = FindType(catalog, "BitFlag");
    AddValueMember<&BitFlag::Get>(type, "Get", Parameters(), false);
    AddValueMember<&BitFlag::IsEmpty>(type, "IsEmpty", Parameters(), false);
    AddValueMember<&BitFlag::HasAll>(type, "HasAll", Parameters(In("bits")), false);
    AddValueMember<&BitFlag::HasAny>(type, "HasAny", Parameters(In("bits")), false);
    AddValueMember<&BitFlag::HasNone>(type, "HasNone", Parameters(In("bits")), false);
    AddValueMember<&BitFlag::Clear>(type, "Clear", Parameters(), true);
    AddValueMember<&BitFlag::Set>(type, "Set", Parameters(In("bits")), true);
    AddValueMember<&BitFlag::Add>(type, "Add", Parameters(In("bits")), true);
    AddValueMember<&BitFlag::Remove>(type, "Remove", Parameters(In("bits")), true);
    AddValueMember<&BitFlag::Toggle>(type, "Toggle", Parameters(In("bits")), true);
    AddValueMember<&BitFlag::SetTo>(type, "SetTo", Parameters(In("bits"),In("on")), true);
    }
    {
    auto& type = FindType(catalog, "SafeAreaInsets");
    AddValueMember<&SafeAreaInsets::IsAny>(type, "IsAny", Parameters(), false);
    }
    {
    auto& type = FindType(catalog, "KeyboardState");
    type.Members.Add(DeclareField<&KeyboardState::keys>("keys"));
    type.Members.Add(DeclareField<&KeyboardState::modifiers>("modifiers"));
    type.Members.Add(DeclareField<&KeyboardState::textLength>("textLength"));
    type.Members.Add(DeclareField<&KeyboardState::text>("text"));
    }
    {
    auto& type = FindType(catalog, "MouseState");
    type.Members.Add(DeclareField<&MouseState::hasPosition>("hasPosition"));
    }
    {
    auto& type = FindType(catalog, "Uuid");
    type.Members.Add(DeclareField<&Uuid::high>("high"));
    type.Members.Add(DeclareField<&Uuid::low>("low"));
    }
    {
    auto& type = FindType(catalog, "AudioBusName");
    type.Members.Add(DeclareField<&AudioBusName::id>("id"));
    }
    {
    auto& type = FindType(catalog, "BitFlag");
    type.Members.Add(DeclareField<&BitFlag::Value>("Value"));
    }
    {
    auto& type = FindType(catalog, "SafeAreaInsets");
    type.Members.Add(DeclareField<&SafeAreaInsets::left>("left"));
    type.Members.Add(DeclareField<&SafeAreaInsets::top>("top"));
    type.Members.Add(DeclareField<&SafeAreaInsets::right>("right"));
    type.Members.Add(DeclareField<&SafeAreaInsets::bottom>("bottom"));
    }
    AddConstructor<Vector2>(FindType(catalog,"Vector2"), Parameters());
    AddConstructor<Rect>(FindType(catalog,"Rect"), Parameters());
    AddConstructor<Matrix3x2>(FindType(catalog,"Matrix3x2"), Parameters());
    AddConstructor<Color>(FindType(catalog,"Color"), Parameters());
    AddConstructor<Degree>(FindType(catalog,"Degree"), Parameters());
    AddConstructor<Radian>(FindType(catalog,"Radian"), Parameters());
    AddConstructor<Size>(FindType(catalog,"Size"), Parameters());
    AddConstructor<SizeU>(FindType(catalog,"SizeU"), Parameters());
    AddConstructor<SizeI>(FindType(catalog,"SizeI"), Parameters());
    AddConstructor<Uuid>(FindType(catalog,"Uuid"), Parameters());
    AddConstructor<AssetHandle>(FindType(catalog,"AssetHandle"), Parameters());
    AddConstructor<AudioBusName>(FindType(catalog,"AudioBusName"), Parameters());
    AddConstructor<AudioBusEffects>(FindType(catalog,"AudioBusEffects"), Parameters());
    AddConstructor<RandomState>(FindType(catalog,"RandomState"), Parameters());
    AddConstructor<RandomStream>(FindType(catalog,"RandomStream"), Parameters());
    AddConstructor<Collision2D>(FindType(catalog,"Collision2D"), Parameters());
    AddConstructor<RaycastHit2D>(FindType(catalog,"RaycastHit2D"), Parameters());
    AddConstructor<PhysicsLayerMask>(FindType(catalog,"PhysicsLayerMask"), Parameters());
    AddConstructor<KeyboardState>(FindType(catalog,"KeyboardState"), Parameters());
    AddConstructor<MouseState>(FindType(catalog,"MouseState"), Parameters());
    AddConstructor<GamepadState>(FindType(catalog,"GamepadState"), Parameters());
    AddConstructor<TouchPoint>(FindType(catalog,"TouchPoint"), Parameters());
    AddConstructor<TouchState>(FindType(catalog,"TouchState"), Parameters());
    AddConstructor<ButtonState>(FindType(catalog,"ButtonState"), Parameters());
    AddConstructor<InputBinding>(FindType(catalog,"InputBinding"), Parameters());
    AddConstructor<InputVector2>(FindType(catalog,"InputVector2"), Parameters());
    AddConstructor<InputActionValue>(FindType(catalog,"InputActionValue"), Parameters());
    AddConstructor<PrefabSpawnParams>(FindType(catalog,"PrefabSpawnParams"), Parameters());
    AddConstructor<PrefabSpawner>(FindType(catalog,"PrefabSpawner"), Parameters());
    AddConstructor<Layer2D>(FindType(catalog,"Layer2D"), Parameters());
    AddConstructor<BitFlag>(FindType(catalog,"BitFlag"), Parameters());
    AddConstructor<SafeAreaInsets>(FindType(catalog,"SafeAreaInsets"), Parameters());
    AddConstructor<Vector2,Float,Float>(FindType(catalog,"Vector2"), Parameters(In("x"),In("y")));
    AddConstructor<Color,Float,Float,Float,Float>(FindType(catalog,"Color"), Parameters(In("R"),In("G"),In("B"),In("A", "1")));
    AddConstructor<Rect,Vector2,Vector2>(FindType(catalog,"Rect"), Parameters(In("min"),In("max")));
    AddConstructor<Degree,Float>(FindType(catalog,"Degree"), Parameters(In("value")));
    AddConstructor<Radian,Float>(FindType(catalog,"Radian"), Parameters(In("value")));
    AddConstructor<Size,Float,Float>(FindType(catalog,"Size"), Parameters(In("width"),In("height")));
    AddConstructor<SizeU,UInt32,UInt32>(FindType(catalog,"SizeU"), Parameters(In("width"),In("height")));
    AddConstructor<SizeI,Int32,Int32>(FindType(catalog,"SizeI"), Parameters(In("width"),In("height")));
    AddConstructor<PhysicsLayerMask,UInt32>(FindType(catalog,"PhysicsLayerMask"), Parameters(In("bits")));
    AddConstructor<RandomStream,UInt64,UInt64>(FindType(catalog,"RandomStream"), Parameters(In("seed"),In("stream", "0")));
    {
    auto& type = FindType(catalog,"Uuid");
    auto parse = DeclareFunction<&Uuid::Parse>("Parse", Parameters(In("text"),Count("length"),Out("result")));
    parse.Type = "Uuid?";
    parse.Projection = ApiProjectionKind::NullableReturn;
    parse.HasOutputProjection = true;
    parse.SuccessFromNativeReturn = true;
    type.Members.Add(std::move(parse));
    auto text = DeclareFunction<&Uuid::ToText>("ToText", Parameters(Out("buffer"),Count("capacity")));
    text.Type = "String?";
    text.Projection = ApiProjectionKind::NullableReturn;
    text.HasOutputProjection = true;
    text.SuccessFromNativeReturn = true;
    type.Members.Add(std::move(text));
    }
    AddConstructor<SpriteImportOptions>(FindType(catalog,"SpriteImportOptions"), Parameters());
    AddConstructor<TextureImportOptions>(FindType(catalog,"TextureImportOptions"), Parameters());
    AddConstructor<AudioImportOptions>(FindType(catalog,"AudioImportOptions"), Parameters());
    AddConstructor<FontImportOptions>(FindType(catalog,"FontImportOptions"), Parameters());
    AddConstructor<StringTableOptions>(FindType(catalog,"StringTableOptions"), Parameters());
    AddConstructor<FontFamilyOptions>(FindType(catalog,"FontFamilyOptions"), Parameters());
    AddConstructor<AudioEffectParam>(FindType(catalog,"AudioEffectParam"), Parameters());
    AddConstructor<AudioEffectKindInfo>(FindType(catalog,"AudioEffectKindInfo"), Parameters());
    AddConstructor<AudioEffectSettings>(FindType(catalog,"AudioEffectSettings"), Parameters());
    AddConstructor<SpriteFrame>(FindType(catalog,"SpriteFrame"), Parameters());
    AddConstructor<AssetMetadata>(FindType(catalog,"AssetMetadata"), Parameters());
    AddConstructor<Vector3>(FindType(catalog,"Vector3"),Parameters());
    AddConstructor<Vector4>(FindType(catalog,"Vector4"),Parameters());
    AddConstructor<Quaternion>(FindType(catalog,"Quaternion"),Parameters());
    AddConstructor<Matrix4x4>(FindType(catalog,"Matrix4x4"),Parameters());
    AddConstructor<Vector3,Float,Float,Float>(FindType(catalog,"Vector3"),Parameters(In("x"),In("y"),In("z")));
    AddConstructor<Vector4,Float,Float,Float,Float>(FindType(catalog,"Vector4"),Parameters(In("x"),In("y"),In("z"),In("w")));
    AddConstructor<Quaternion,Float,Float,Float,Float>(FindType(catalog,"Quaternion"),Parameters(In("x"),In("y"),In("z"),In("w","1")));
}
}
