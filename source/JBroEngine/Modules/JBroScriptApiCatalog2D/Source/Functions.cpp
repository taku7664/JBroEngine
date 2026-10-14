#include "CatalogBuilder.h"
namespace JBro::ScriptApiCatalog2D::Internal
{
template<auto Function, std::size_t N> static void AddGlobal(ApiCatalog& catalog, const char* name, const std::array<Parameter,N>& parameters)
{
    catalog.Functions.Add(DeclareFunction<Function>(name, parameters));
}
template<auto Function> static void AddLookup(ApiCatalog& catalog, const char* name, const char* outputName, const char* returnType)
{
    auto member = DeclareFunction<Function>(name, Parameters(In("name"), Out(outputName)));
    member.Type = returnType;
    member.Projection = ApiProjectionKind::NullableReturn;
    member.HasOutputProjection = true;
    member.SuccessFromNativeReturn = true;
    catalog.Functions.Add(std::move(member));
}
void AddFunctions(ApiCatalog& catalog)
{
    AddGlobal<&MakeRect>(catalog,"MakeRect",Parameters(In("corner"),In("size")));
    AddGlobal<&MakeRectFromCenter>(catalog,"MakeRectFromCenter",Parameters(In("center"),In("size")));
    AddGlobal<&MakeRectFromPoint>(catalog,"MakeRectFromPoint",Parameters(In("point")));
    AddGlobal<&IntersectRect>(catalog,"IntersectRect",Parameters(In("left"),In("right")));
    AddGlobal<static_cast<Rect(*)(const Rect&, const Rect&) noexcept>(&UnionRect)>(catalog,"UnionRect",Parameters(In("left"),In("right")));
    AddGlobal<static_cast<Rect(*)(const Rect&, const Vector2&) noexcept>(&UnionRect)>(catalog,"UnionRect",Parameters(In("rect"),In("point")));
    AddGlobal<&OffsetRect>(catalog,"OffsetRect",Parameters(In("rect"),In("offset")));
    AddGlobal<&ExpandRect>(catalog,"ExpandRect",Parameters(In("rect"),In("margin")));
    AddGlobal<&MakeTransformMatrix2D>(catalog,"MakeTransformMatrix2D",Parameters(In("position"),In("rotation"),In("scale")));
    AddGlobal<&MultiplyMatrix3x2>(catalog,"MultiplyMatrix3x2",Parameters(In("left"),In("right")));
    AddGlobal<&MakeNameId>(catalog,"MakeNameId",Parameters(In("text")));
    AddGlobal<&GetKeyName>(catalog,"GetKeyName",Parameters(In("key")));
    AddGlobal<&GetMouseButtonName>(catalog,"GetMouseButtonName",Parameters(In("button")));
    AddGlobal<&GetGamepadButtonName>(catalog,"GetGamepadButtonName",Parameters(In("button")));
    AddGlobal<&GetGamepadAxisName>(catalog,"GetGamepadAxisName",Parameters(In("axis")));
    AddLookup<&FindKeyByName>(catalog,"FindKeyByName","key","Key?");
    AddLookup<&FindMouseButtonByName>(catalog,"FindMouseButtonByName","button","MouseButton?");
    AddLookup<&FindGamepadButtonByName>(catalog,"FindGamepadButtonByName","button","GamepadButton?");
    AddLookup<&FindGamepadAxisByName>(catalog,"FindGamepadAxisByName","axis","GamepadAxis?");
    AddGlobal<&IsSameBinding>(catalog,"IsSameBinding",Parameters(In("a"),In("b")));
    {
        auto member = DeclareFunction<&CaptureBinding>("CaptureBinding", Parameters(In("view"),Out("binding")));
        member.Type = "InputCapture";
        member.Projection = ApiProjectionKind::ValueReturn;
        member.HasOutputProjection = true;
        member.NativeReturnField = "result";
        catalog.Functions.Add(std::move(member));
    }
    catalog.Globals.Add({"Pi","Float","JBro::Pi"});
    catalog.Globals.Add({"TwoPi","Float","JBro::TwoPi"});
    catalog.Globals.Add({"DegreesToRadians","Float","JBro::DegreesToRadians"});
    catalog.Globals.Add({"RadiansToDegrees","Float","JBro::RadiansToDegrees"});
    catalog.Globals.Add({"AllPhysicsLayers","Int","JBro::AllPhysicsLayers"});
    catalog.Globals.Add({"PhysicsLayerCount","Int","JBro::PhysicsLayerCount"});
    catalog.Globals.Add({"MaxTouches","Int","JBro::MaxTouches"});
    catalog.Globals.Add({"MaxGamepads","Int","JBro::MaxGamepads"});
    catalog.Globals.Add({"MaxInputBindingsPerAction","Int","JBro::MaxInputBindingsPerAction"});
    catalog.Globals.Add({"DefaultInputActionSet","Int","JBro::DefaultInputActionSet"});
    catalog.Globals.Add({"InvalidConnectionId","Int","JBro::Network::InvalidConnectionId"});
    catalog.Globals.Add({"ServerConnectionId","Int","JBro::Network::ServerConnectionId"});
    catalog.Globals.Add({"InvalidNetworkObjectId","Int","JBro::Network::InvalidNetworkObjectId"});
    catalog.Globals.Add({"KeyModifierNone","Int","JBro::KeyModifierNone"});
    catalog.Globals.Add({"KeyModifierShift","Int","JBro::KeyModifierShift"});
    catalog.Globals.Add({"KeyModifierControl","Int","JBro::KeyModifierControl"});
    catalog.Globals.Add({"KeyModifierAlt","Int","JBro::KeyModifierAlt"});
    catalog.Globals.Add({"KeyModifierSuper","Int","JBro::KeyModifierSuper"});
    AddGlobal<&GetAudioEffectKindInfo>(catalog,"GetAudioEffectKindInfo",Parameters(In("kind")));
    AddGlobal<&MakeAudioEffect>(catalog,"MakeAudioEffect",Parameters(In("kind")));
    AddGlobal<&ChangeAudioEffectKind>(catalog,"ChangeAudioEffectKind",Parameters(In("from"),In("kind")));
    catalog.Globals.Add({"DefaultPixelsPerUnit","Float","JBro::DefaultPixelsPerUnit"});
    catalog.Globals.Add({"AudioEffectMaxParams","Int","JBro::AudioEffectMaxParams"});
    AddGlobal<static_cast<Vector3(*)(const Vector3&,const Vector3&)>(&Add)>(catalog,"Add",Parameters(In("left"),In("right")));
    AddGlobal<static_cast<Vector3(*)(const Vector3&,const Vector3&)>(&Subtract)>(catalog,"Subtract",Parameters(In("left"),In("right")));
    AddGlobal<static_cast<Vector3(*)(const Vector3&,Float)>(&Scale)>(catalog,"Scale",Parameters(In("value"),In("factor")));
    AddGlobal<static_cast<Vector3(*)(const Vector3&,const Vector3&)>(&Multiply)>(catalog,"Multiply",Parameters(In("left"),In("right")));
    AddGlobal<static_cast<Float(*)(const Vector3&,const Vector3&)>(&Dot)>(catalog,"Dot",Parameters(In("left"),In("right")));
    AddGlobal<static_cast<Vector3(*)(const Vector3&,const Vector3&)>(&Cross)>(catalog,"Cross",Parameters(In("left"),In("right")));
    AddGlobal<static_cast<Float(*)(const Vector3&)>(&Length)>(catalog,"Length",Parameters(In("value")));
    AddGlobal<static_cast<Vector3(*)(const Vector3&)>(&Normalize)>(catalog,"Normalize",Parameters(In("value")));
    AddGlobal<static_cast<Quaternion(*)(const Quaternion&)>(&Normalize)>(catalog,"Normalize",Parameters(In("value")));
    AddGlobal<static_cast<Quaternion(*)(const Quaternion&)>(&Conjugate)>(catalog,"Conjugate",Parameters(In("value")));
    AddGlobal<static_cast<Quaternion(*)(const Quaternion&,const Quaternion&)>(&Multiply)>(catalog,"Multiply",Parameters(In("a"),In("b")));
    AddGlobal<static_cast<Vector3(*)(const Quaternion&,const Vector3&)>(&Rotate)>(catalog,"Rotate",Parameters(In("rotation"),In("value")));
    AddGlobal<static_cast<Quaternion(*)(const Vector3&,Radian)>(&FromAxisAngle)>(catalog,"FromAxisAngle",Parameters(In("axis"),In("angle")));
    AddGlobal<static_cast<Quaternion(*)(const Vector3&)>(&FromEuler)>(catalog,"FromEuler",Parameters(In("radians")));
    AddGlobal<static_cast<Bool(*)(const Vector3&,const Vector3&,Float)>(&NearlyEqual)>(catalog,"NearlyEqual",Parameters(In("left"),In("right"),In("tolerance","0.0001f")));
    AddGlobal<static_cast<Vector4(*)(const Vector4&,const Vector4&)>(&Add)>(catalog,"Add",Parameters(In("left"),In("right")));
    AddGlobal<static_cast<Vector4(*)(const Vector4&,const Vector4&)>(&Subtract)>(catalog,"Subtract",Parameters(In("left"),In("right")));
    AddGlobal<static_cast<Vector4(*)(const Vector4&,Float)>(&Scale)>(catalog,"Scale",Parameters(In("value"),In("factor")));
    AddGlobal<static_cast<Vector4(*)(const Vector4&,const Vector4&)>(&Multiply)>(catalog,"Multiply",Parameters(In("left"),In("right")));
    AddGlobal<static_cast<Float(*)(const Vector4&,const Vector4&)>(&Dot)>(catalog,"Dot",Parameters(In("left"),In("right")));
    AddGlobal<static_cast<Float(*)(const Vector4&)>(&Length)>(catalog,"Length",Parameters(In("value")));
    AddGlobal<static_cast<Vector4(*)(const Vector4&)>(&Normalize)>(catalog,"Normalize",Parameters(In("value")));
    AddGlobal<static_cast<Vector4(*)(const Vector3&)>(&MakePoint)>(catalog,"MakePoint",Parameters(In("value")));
    AddGlobal<static_cast<Vector4(*)(const Vector3&)>(&MakeDirection)>(catalog,"MakeDirection",Parameters(In("value")));
    AddGlobal<static_cast<Vector3(*)(const Vector4&)>(&ToVector3)>(catalog,"ToVector3",Parameters(In("value")));
    AddGlobal<static_cast<Bool(*)(const Vector4&,const Vector4&,Float)>(&NearlyEqual)>(catalog,"NearlyEqual",Parameters(In("left"),In("right"),In("tolerance","0.0001f")));
    AddGlobal<&MultiplyMatrix4x4>(catalog,"MultiplyMatrix4x4",Parameters(In("left"),In("right")));
}
}
