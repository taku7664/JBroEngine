#include "pch.h"
#include "BuiltinComponentRegistry.h"

#include "GameFramework/Component/AudioComponents.h"
#include "GameFramework/Component/Camera2D.h"
#include "GameFramework/Component/Light2D.h"
#include "GameFramework/Component/Physics2DComponents.h"
#include "GameFramework/Component/PrefabInstance.h"
#include "GameFramework/Component/ScriptComponent.h"
#include "GameFramework/Component/ShapeRenderers2D.h"
#include "GameFramework/Component/SpriteAnimator2D.h"
#include "GameFramework/Component/SpriteRenderer2D.h"
#include "GameFramework/Component/Text2D.h"
#include "GameFramework/Reflection/ReflectionRegistry.h"
#include "GameFramework/Reflection/ReflectionEnumRegister.h"

#include <cstddef>

// GameObject(Name/Active/Layer) 와 Transform2D 는 더 이상 컴포넌트가 아니다(CGameObject 멤버).
// 인스펙터/직렬화는 이를 오브젝트 헤더로 직접 처리한다. 계층도 CGameObject 멤버(폐지된
// TransformHierarchy2D 없음). 컴포넌트 공통 IsEnabled 는 CComponent 베이스 → 제네릭 처리.
void RegisterBuiltinComponents(CReflectionRegistry& registry)
{
	registry.RegisterComponent<SpriteRenderer2D>({ "SpriteRenderer2D", "Sprite Renderer 2D", "Rendering", true })
		.AddProperty("SpriteGuid", EReflectPropertyType::AssetGuid, offsetof(SpriteRenderer2D, SpriteGuid), sizeof(AssetGuid))
		.AddProperty("MaterialGuid", EReflectPropertyType::AssetGuid, offsetof(SpriteRenderer2D, MaterialGuid), sizeof(AssetGuid))
		.AddProperty("Size", EReflectPropertyType::Vector2Float, offsetof(SpriteRenderer2D, Size), sizeof(Vector2))
		.AddProperty("Offset", EReflectPropertyType::Vector2Float, offsetof(SpriteRenderer2D, Offset), sizeof(Vector2))
		.AddProperty("FlipX", EReflectPropertyType::Bool, offsetof(SpriteRenderer2D, FlipX), sizeof(bool))
		.AddProperty("FlipY", EReflectPropertyType::Bool, offsetof(SpriteRenderer2D, FlipY), sizeof(bool))
		.AddProperty("FrameIndex", EReflectPropertyType::UInt32, offsetof(SpriteRenderer2D, FrameIndex), sizeof(std::uint32_t))
		.AddProperty("Color", EReflectPropertyType::ColorFloat4, offsetof(SpriteRenderer2D, Color), sizeof(float), 4)
		.AddProperty("SortOrder", EReflectPropertyType::Int32, offsetof(SpriteRenderer2D, SortOrder), sizeof(std::int32_t))
		.AddProperty("LayerMask", EReflectPropertyType::UInt32, offsetof(SpriteRenderer2D, LayerMask), sizeof(RenderLayerMask));

	registry.RegisterComponent<Text2D>({ "Text2D", "Text 2D", "Rendering", true })
		.AddProperty("Text", EReflectPropertyType::String, offsetof(Text2D, Text), sizeof(String))
		.AddProperty("FontFamilyGuid", EReflectPropertyType::AssetGuid, offsetof(Text2D, FontFamilyGuid), sizeof(AssetGuid))
		.AddEnumProperty<EFontStyle>("FontStyle", offsetof(Text2D, FontStyle))
		.AddProperty("FontSizePixels", EReflectPropertyType::Float, offsetof(Text2D, FontSizePixels), sizeof(float))
		.AddProperty("WidthPixels", EReflectPropertyType::Float, offsetof(Text2D, WidthPixels), sizeof(float))
		.AddProperty("HeightPixels", EReflectPropertyType::Float, offsetof(Text2D, HeightPixels), sizeof(float))
		.AddEnumProperty<ETextOverflowMode>("OverflowMode", offsetof(Text2D, OverflowMode))
		.AddProperty("AutoSizeEnabled", EReflectPropertyType::Bool, offsetof(Text2D, AutoSizeEnabled), sizeof(bool))
		.AddProperty("MinFontSizePixels", EReflectPropertyType::Float, offsetof(Text2D, MinFontSizePixels), sizeof(float))
		.AddProperty("MaxFontSizePixels", EReflectPropertyType::Float, offsetof(Text2D, MaxFontSizePixels), sizeof(float))
		.AddEnumProperty<ETextHorizontalAlignment>("HorizontalAlignment", offsetof(Text2D, HorizontalAlignment))
		.AddEnumProperty<ETextVerticalAlignment>("VerticalAlignment", offsetof(Text2D, VerticalAlignment))
		.AddProperty("LineSpacing", EReflectPropertyType::Float, offsetof(Text2D, LineSpacing), sizeof(float))
		.AddProperty("LetterSpacingPixels", EReflectPropertyType::Float, offsetof(Text2D, LetterSpacingPixels), sizeof(float))
		.AddProperty("FillEnabled", EReflectPropertyType::Bool, offsetof(Text2D, FillEnabled), sizeof(bool))
		.AddProperty("FillColor", EReflectPropertyType::ColorFloat4, offsetof(Text2D, FillColor), sizeof(float), 4)
		.AddProperty("OutlineEnabled", EReflectPropertyType::Bool, offsetof(Text2D, OutlineEnabled), sizeof(bool))
		.AddProperty("OutlineColor", EReflectPropertyType::ColorFloat4, offsetof(Text2D, OutlineColor), sizeof(float), 4)
		.AddProperty("OutlineWidthPixels", EReflectPropertyType::Float, offsetof(Text2D, OutlineWidthPixels), sizeof(float))
		.AddProperty("PixelSnap", EReflectPropertyType::Bool, offsetof(Text2D, PixelSnap), sizeof(bool))
		.AddProperty("Offset", EReflectPropertyType::Vector2Float, offsetof(Text2D, Offset), sizeof(Vector2))
		.AddProperty("SortOrder", EReflectPropertyType::Int32, offsetof(Text2D, SortOrder), sizeof(std::int32_t))
		.AddProperty("LayerMask", EReflectPropertyType::UInt32, offsetof(Text2D, LayerMask), sizeof(RenderLayerMask));

	registry.RegisterComponent<Square2D>({ "Square2D", "Square 2D", "Rendering", true })
		.AddProperty("Size", EReflectPropertyType::Vector2Float, offsetof(Square2D, Size), sizeof(Vector2))
		.AddProperty("Offset", EReflectPropertyType::Vector2Float, offsetof(Square2D, Offset), sizeof(Vector2))
		.AddProperty("FillEnabled", EReflectPropertyType::Bool, offsetof(Square2D, FillEnabled), sizeof(bool))
		.AddProperty("FillColor", EReflectPropertyType::ColorFloat4, offsetof(Square2D, FillColor), sizeof(float), 4)
		.AddProperty("OutlineEnabled", EReflectPropertyType::Bool, offsetof(Square2D, OutlineEnabled), sizeof(bool))
		.AddProperty("OutlineColor", EReflectPropertyType::ColorFloat4, offsetof(Square2D, OutlineColor), sizeof(float), 4)
		.AddProperty("OutlineWidth", EReflectPropertyType::Float, offsetof(Square2D, OutlineWidth), sizeof(float))
		.AddProperty("SortOrder", EReflectPropertyType::Int32, offsetof(Square2D, SortOrder), sizeof(std::int32_t))
		.AddProperty("LayerMask", EReflectPropertyType::UInt32, offsetof(Square2D, LayerMask), sizeof(RenderLayerMask));

	registry.RegisterComponent<Circle2D>({ "Circle2D", "Circle 2D", "Rendering", true })
		.AddProperty("Radius", EReflectPropertyType::Float, offsetof(Circle2D, Radius), sizeof(float))
		.AddProperty("Segments", EReflectPropertyType::UInt32, offsetof(Circle2D, Segments), sizeof(std::uint32_t))
		.AddProperty("Offset", EReflectPropertyType::Vector2Float, offsetof(Circle2D, Offset), sizeof(Vector2))
		.AddProperty("FillEnabled", EReflectPropertyType::Bool, offsetof(Circle2D, FillEnabled), sizeof(bool))
		.AddProperty("FillColor", EReflectPropertyType::ColorFloat4, offsetof(Circle2D, FillColor), sizeof(float), 4)
		.AddProperty("OutlineEnabled", EReflectPropertyType::Bool, offsetof(Circle2D, OutlineEnabled), sizeof(bool))
		.AddProperty("OutlineColor", EReflectPropertyType::ColorFloat4, offsetof(Circle2D, OutlineColor), sizeof(float), 4)
		.AddProperty("OutlineWidth", EReflectPropertyType::Float, offsetof(Circle2D, OutlineWidth), sizeof(float))
		.AddProperty("SortOrder", EReflectPropertyType::Int32, offsetof(Circle2D, SortOrder), sizeof(std::int32_t))
		.AddProperty("LayerMask", EReflectPropertyType::UInt32, offsetof(Circle2D, LayerMask), sizeof(RenderLayerMask));

	registry.RegisterComponent<Polygon2D>({ "Polygon2D", "Polygon 2D", "Rendering", true })
		.AddProperty("Radius", EReflectPropertyType::Float, offsetof(Polygon2D, Radius), sizeof(float))
		.AddProperty("VertexCount", EReflectPropertyType::UInt32, offsetof(Polygon2D, VertexCount), sizeof(std::uint32_t))
		.AddProperty("StartAngle", EReflectPropertyType::AngleDegrees, offsetof(Polygon2D, StartAngle), sizeof(float))
		.AddProperty("Offset", EReflectPropertyType::Vector2Float, offsetof(Polygon2D, Offset), sizeof(Vector2))
		.AddProperty("FillEnabled", EReflectPropertyType::Bool, offsetof(Polygon2D, FillEnabled), sizeof(bool))
		.AddProperty("FillColor", EReflectPropertyType::ColorFloat4, offsetof(Polygon2D, FillColor), sizeof(float), 4)
		.AddProperty("OutlineEnabled", EReflectPropertyType::Bool, offsetof(Polygon2D, OutlineEnabled), sizeof(bool))
		.AddProperty("OutlineColor", EReflectPropertyType::ColorFloat4, offsetof(Polygon2D, OutlineColor), sizeof(float), 4)
		.AddProperty("OutlineWidth", EReflectPropertyType::Float, offsetof(Polygon2D, OutlineWidth), sizeof(float))
		.AddProperty("SortOrder", EReflectPropertyType::Int32, offsetof(Polygon2D, SortOrder), sizeof(std::int32_t))
		.AddProperty("LayerMask", EReflectPropertyType::UInt32, offsetof(Polygon2D, LayerMask), sizeof(RenderLayerMask));

	registry.RegisterComponent<SpriteAnimator2D>({ "SpriteAnimator2D", "Sprite Animator 2D", "Rendering", true })
		.AddProperty("FramesPerSecond", EReflectPropertyType::Float, offsetof(SpriteAnimator2D, FramesPerSecond), sizeof(float))
		.AddProperty("Loop", EReflectPropertyType::Bool, offsetof(SpriteAnimator2D, Loop), sizeof(bool))
		.AddProperty("Playing", EReflectPropertyType::Bool, offsetof(SpriteAnimator2D, Playing), sizeof(bool))
		.AddProperty("StartFrame", EReflectPropertyType::UInt32, offsetof(SpriteAnimator2D, StartFrame), sizeof(std::uint32_t))
		.AddProperty("FrameCount", EReflectPropertyType::UInt32, offsetof(SpriteAnimator2D, FrameCount), sizeof(std::uint32_t));

	registry.RegisterComponent<Camera2D>({ "Camera2D", "Camera 2D", "Rendering", true })
		.AddEnumProperty<ECameraProjectionMode2D>("ProjectionMode", offsetof(Camera2D, ProjectionMode))
		.AddProperty("OrthographicSize", EReflectPropertyType::Float, offsetof(Camera2D, OrthographicSize), sizeof(float))
		.AddProperty("PerspectiveFovDegrees", EReflectPropertyType::Float, offsetof(Camera2D, PerspectiveFovDegrees), sizeof(float))
		.AddProperty("NearClip", EReflectPropertyType::Float, offsetof(Camera2D, NearClip), sizeof(float))
		.AddProperty("FarClip", EReflectPropertyType::Float, offsetof(Camera2D, FarClip), sizeof(float))
		.AddProperty("Position", EReflectPropertyType::Layout2D, offsetof(Camera2D, Position), sizeof(Layout2D))
		.AddProperty("Size", EReflectPropertyType::Layout2D, offsetof(Camera2D, Size), sizeof(Layout2D))
		.AddProperty("ClearColor", EReflectPropertyType::ColorFloat4, offsetof(Camera2D, ClearColor), sizeof(float), 4)
		.AddProperty("LayerMask", EReflectPropertyType::UInt32, offsetof(Camera2D, LayerMask), sizeof(std::uint32_t))
		.AddProperty("Priority", EReflectPropertyType::Int32, offsetof(Camera2D, Priority), sizeof(std::int32_t));

	registry.RegisterComponent<Light2D>({ "Light2D", "Light 2D", "Rendering", true })
		.AddEnumProperty<ELight2DType>("Type", offsetof(Light2D, Type))
		.AddProperty("Color", EReflectPropertyType::ColorFloat4, offsetof(Light2D, Color), sizeof(float), 4)
		.AddProperty("Intensity", EReflectPropertyType::Float, offsetof(Light2D, Intensity), sizeof(float))
		.AddProperty("Range", EReflectPropertyType::Float, offsetof(Light2D, Range), sizeof(float))
		.AddProperty("LayerMask", EReflectPropertyType::UInt32, offsetof(Light2D, LayerMask), sizeof(std::uint32_t))
		.AddProperty("CastShadows", EReflectPropertyType::Bool, offsetof(Light2D, CastShadows), sizeof(bool));

	registry.RegisterComponent<Rigidbody2D>({ "Rigidbody2D", "Rigidbody 2D", "Physics", true })
		.AddEnumProperty<EPhysics2DBodyType>("BodyType", offsetof(Rigidbody2D, BodyType))
		.AddProperty("Velocity", EReflectPropertyType::Vector2Float, offsetof(Rigidbody2D, Velocity), sizeof(Vector2))
		.AddProperty("Force", EReflectPropertyType::Vector2Float, offsetof(Rigidbody2D, Force), sizeof(Vector2))
		.AddProperty("AngularVelocity", EReflectPropertyType::Float, offsetof(Rigidbody2D, AngularVelocity), sizeof(float))
		.AddProperty("Torque", EReflectPropertyType::Float, offsetof(Rigidbody2D, Torque), sizeof(float))
		.AddProperty("Mass", EReflectPropertyType::Float, offsetof(Rigidbody2D, Mass), sizeof(float))
		.AddProperty("Inertia", EReflectPropertyType::Float, offsetof(Rigidbody2D, Inertia), sizeof(float))
		.AddProperty("Friction", EReflectPropertyType::Float, offsetof(Rigidbody2D, Friction), sizeof(float))
		.AddProperty("Restitution", EReflectPropertyType::Float, offsetof(Rigidbody2D, Restitution), sizeof(float))
		.AddProperty("LinearDamping", EReflectPropertyType::Float, offsetof(Rigidbody2D, LinearDamping), sizeof(float))
		.AddProperty("AngularDamping", EReflectPropertyType::Float, offsetof(Rigidbody2D, AngularDamping), sizeof(float))
		.AddProperty("GravityScale", EReflectPropertyType::Float, offsetof(Rigidbody2D, GravityScale), sizeof(float))
		.AddProperty("UseGravity", EReflectPropertyType::Bool, offsetof(Rigidbody2D, UseGravity), sizeof(bool))
		.AddProperty("FreezePositionX", EReflectPropertyType::Bool, offsetof(Rigidbody2D, FreezePositionX), sizeof(bool))
		.AddProperty("FreezePositionY", EReflectPropertyType::Bool, offsetof(Rigidbody2D, FreezePositionY), sizeof(bool))
		.AddProperty("FreezeRotation", EReflectPropertyType::Bool, offsetof(Rigidbody2D, FreezeRotation), sizeof(bool))
		.AddProperty("StabilizeRestingContacts", EReflectPropertyType::Bool, offsetof(Rigidbody2D, StabilizeRestingContacts), sizeof(bool))
		.AddProperty("RestingLinearVelocityThreshold", EReflectPropertyType::Float, offsetof(Rigidbody2D, RestingLinearVelocityThreshold), sizeof(float))
		.AddProperty("RestingAngularVelocityThreshold", EReflectPropertyType::Float, offsetof(Rigidbody2D, RestingAngularVelocityThreshold), sizeof(float));

	registry.RegisterComponent<PolygonCollider2D>({ "PolygonCollider2D", "Polygon Collider 2D", "Physics", true })
		.AddProperty("VertexCount", EReflectPropertyType::UInt32, offsetof(PolygonCollider2D, VertexCount), sizeof(std::uint32_t))
		.AddProperty("Offset", EReflectPropertyType::Vector2Float, offsetof(PolygonCollider2D, Offset), sizeof(Vector2))
		.AddProperty("IsTrigger", EReflectPropertyType::Bool, offsetof(PolygonCollider2D, IsTrigger), sizeof(bool));

	registry.RegisterComponent<CircleCollider2D>({ "CircleCollider2D", "Circle Collider 2D", "Physics", true })
		.AddProperty("Radius", EReflectPropertyType::Float, offsetof(CircleCollider2D, Radius), sizeof(float))
		.AddProperty("Offset", EReflectPropertyType::Vector2Float, offsetof(CircleCollider2D, Offset), sizeof(Vector2))
		.AddProperty("IsTrigger", EReflectPropertyType::Bool, offsetof(CircleCollider2D, IsTrigger), sizeof(bool));

	registry.RegisterComponent<PrefabInstance>({ "PrefabInstance", "Prefab Instance", "Prefab", false })
		.AddProperty("SourcePrefabGuid", EReflectPropertyType::AssetGuid, offsetof(PrefabInstance, SourcePrefabGuid), sizeof(AssetGuid));

	registry.RegisterComponent<ScriptComponent>({ "ScriptComponent", "Script Component", "Scripting", true });

	registry.RegisterComponent<AudioListener>({ "AudioListener", "Audio Listener", "Audio", true })
		.AddProperty("MasterVolume", EReflectPropertyType::Float, offsetof(AudioListener, MasterVolume), sizeof(float));

	registry.RegisterComponent<AudioPlayer>({ "AudioPlayer", "Audio Player", "Audio", true })
		.AddProperty("AudioGuid",   EReflectPropertyType::AssetGuid, offsetof(AudioPlayer, AudioGuid),   sizeof(AssetGuid))
		// EffectGuids(체인)는 가변 길이라 리플렉션 밖 — 인스펙터/직렬화에서 수동 처리.
		.AddProperty("Volume",      EReflectPropertyType::Float,     offsetof(AudioPlayer, Volume),      sizeof(float))
		.AddProperty("Pitch",       EReflectPropertyType::Float,     offsetof(AudioPlayer, Pitch),       sizeof(float))
		.AddProperty("Loop",        EReflectPropertyType::Bool,      offsetof(AudioPlayer, Loop),        sizeof(bool))
		.AddProperty("Is3D",        EReflectPropertyType::Bool,      offsetof(AudioPlayer, Is3D),        sizeof(bool))
		.AddProperty("MinDistance", EReflectPropertyType::Float,     offsetof(AudioPlayer, MinDistance), sizeof(float))
		.AddProperty("MaxDistance", EReflectPropertyType::Float,     offsetof(AudioPlayer, MaxDistance), sizeof(float))
		.AddProperty("PlayOnStart", EReflectPropertyType::Bool,      offsetof(AudioPlayer, PlayOnStart), sizeof(bool));
}
