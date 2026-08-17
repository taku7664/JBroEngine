#include "pch.h"
#include "BuiltinComponentRegistry.h"

#include "GameFramework/Component/AudioComponents.h"
#include "GameFramework/Component/Button2D.h"
#include "GameFramework/Component/Camera2D.h"
#include "GameFramework/Component/Light2D.h"
#include "GameFramework/Component/Physics2DComponents.h"
#include "GameFramework/Component/Physics2DJoints.h"
#include "GameFramework/Component/PrefabInstance.h"
#include "GameFramework/Component/ShapeRenderers2D.h"
#include "GameFramework/Component/SpriteAnimator2D.h"
#include "GameFramework/Component/SpriteRenderer2D.h"
#include "GameFramework/Component/Text2D.h"
#include "GameFramework/Reflection/ReflectionRegistry.h"
#include "GameFramework/Reflection/ReflectionEnumRegister.h"

#include <cstddef>

// ── 베이스가 보유한 공통 프로퍼티 ────────────────────────────────────────────
// 필드는 베이스로 합쳤지만 **등록은 타입마다 해야 한다**(offsetof 는 구체 타입 기준).
// 그대로 두면 새 렌더러를 넣을 때마다 같은 줄을 복붙하게 되고, 하나 빠뜨리면
// 컴파일도 되고 에러도 없이 **인스펙터에서만 사라진다**. 그래서 한 곳에 모은다.
//
// 함수가 아니라 매크로인 이유: offsetof 가 private 멤버를 보려면 각 컴포넌트의 friend
// 안에서 평가돼야 하고, 그 friend 는 RegisterBuiltinComponents 다. 별도 헬퍼 함수로 빼면
// 컴포넌트마다 friend 를 하나씩 더 달아야 한다.
#define JBRO_RENDERER2D_PROPERTIES(TYPE)                                                                       \
	.AddProperty("Offset", EReflectPropertyType::Vector2Float, offsetof(TYPE, m_offset), sizeof(Vector2))       \
	.AddProperty("SortOrder", EReflectPropertyType::Int32, offsetof(TYPE, SortOrder), sizeof(std::int32_t))

#define JBRO_SHAPE_PROPERTIES(TYPE)                                                                            \
	.AddProperty("FillEnabled", EReflectPropertyType::Bool, offsetof(TYPE, FillEnabled), sizeof(bool))          \
	.AddProperty("FillColor", EReflectPropertyType::ColorFloat4, offsetof(TYPE, FillColor), sizeof(Color))      \
	.AddProperty("OutlineEnabled", EReflectPropertyType::Bool, offsetof(TYPE, OutlineEnabled), sizeof(bool))    \
	.AddProperty("OutlineColor", EReflectPropertyType::ColorFloat4, offsetof(TYPE, OutlineColor), sizeof(Color))\
	.AddProperty("OutlineWidth", EReflectPropertyType::Float, offsetof(TYPE, OutlineWidth), sizeof(float))

// GameObject(Name/Active/Layer) 와 Transform2D 는 더 이상 컴포넌트가 아니다(CGameObject 멤버).
// 인스펙터/직렬화는 이를 오브젝트 헤더로 직접 처리한다. 계층도 CGameObject 멤버(폐지된
// TransformHierarchy2D 없음). 컴포넌트 공통 IsEnabled 는 CComponent 베이스 → 제네릭 처리.
void RegisterBuiltinComponents(CReflectionRegistry& registry)
{
	registry.RegisterComponent<SpriteRenderer2D>({ "SpriteRenderer2D", "Sprite Renderer 2D", "Rendering", true })
		.AddAssetProperty("SpriteGuid", offsetof(SpriteRenderer2D, m_spriteGuid), EAssetType::Sprite)
		.AddAssetProperty("MaterialGuid", offsetof(SpriteRenderer2D, MaterialGuid), EAssetType::Material)
		.AddProperty("Size", EReflectPropertyType::Vector2Float, offsetof(SpriteRenderer2D, m_size), sizeof(Vector2))
		JBRO_RENDERER2D_PROPERTIES(SpriteRenderer2D)
		.AddProperty("FlipX", EReflectPropertyType::Bool, offsetof(SpriteRenderer2D, m_flipX), sizeof(bool))
		.AddProperty("FlipY", EReflectPropertyType::Bool, offsetof(SpriteRenderer2D, m_flipY), sizeof(bool))
		.AddProperty("FrameIndex", EReflectPropertyType::UInt32, offsetof(SpriteRenderer2D, m_frameIndex), sizeof(std::uint32_t))
		.AddProperty("Color", EReflectPropertyType::ColorFloat4, offsetof(SpriteRenderer2D, Color), sizeof(Color))
		.AddProperty("CastShadow", EReflectPropertyType::Bool, offsetof(SpriteRenderer2D, CastShadow), sizeof(bool));

	registry.RegisterComponent<Button2D>({ "Button2D", "Button 2D", "UI", true })
		.AddProperty("Size", EReflectPropertyType::Vector2Float, offsetof(Button2D, Size), sizeof(Vector2))
		.AddProperty("Offset", EReflectPropertyType::Vector2Float, offsetof(Button2D, Offset), sizeof(Vector2))
		.AddProperty("Interactable", EReflectPropertyType::Bool, offsetof(Button2D, Interactable), sizeof(bool))
		.AddRefProperty<SpriteRenderer2D>("TargetGraphic", offsetof(Button2D, TargetGraphic), "SpriteRenderer2D")
		.AddEnumProperty<EButtonTransition>("Transition", offsetof(Button2D, Transition))
		.AddProperty("HoverTint", EReflectPropertyType::ColorFloat4, offsetof(Button2D, HoverTint), sizeof(Color))
		.AddProperty("PressedTint", EReflectPropertyType::ColorFloat4, offsetof(Button2D, PressedTint), sizeof(Color))
		.AddProperty("DisabledTint", EReflectPropertyType::ColorFloat4, offsetof(Button2D, DisabledTint), sizeof(Color))
		.AddRefProperty<CSpriteAsset>("HoverSprite", offsetof(Button2D, HoverSprite), "CSpriteAsset")
		.AddRefProperty<CSpriteAsset>("PressedSprite", offsetof(Button2D, PressedSprite), "CSpriteAsset")
		.AddRefProperty<CSpriteAsset>("DisabledSprite", offsetof(Button2D, DisabledSprite), "CSpriteAsset");

	registry.RegisterComponent<Text2D>({ "Text2D", "Text 2D", "Rendering", true })
		.AddProperty("Text", EReflectPropertyType::String, offsetof(Text2D, Text), sizeof(String))
		.AddAssetProperty("FontFamilyGuid", offsetof(Text2D, FontFamilyGuid), EAssetType::FontFamily)
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
		.AddProperty("FillColor", EReflectPropertyType::ColorFloat4, offsetof(Text2D, FillColor), sizeof(Color))
		.AddProperty("OutlineEnabled", EReflectPropertyType::Bool, offsetof(Text2D, OutlineEnabled), sizeof(bool))
		.AddProperty("OutlineColor", EReflectPropertyType::ColorFloat4, offsetof(Text2D, OutlineColor), sizeof(Color))
		.AddProperty("OutlineWidthPixels", EReflectPropertyType::Float, offsetof(Text2D, OutlineWidthPixels), sizeof(float))
		.AddProperty("PixelSnap", EReflectPropertyType::Bool, offsetof(Text2D, PixelSnap), sizeof(bool))
		JBRO_RENDERER2D_PROPERTIES(Text2D);

	registry.RegisterComponent<Square2D>({ "Square2D", "Square 2D", "Rendering", true })
		.AddProperty("Size", EReflectPropertyType::Vector2Float, offsetof(Square2D, m_size), sizeof(Vector2))
		JBRO_RENDERER2D_PROPERTIES(Square2D)
		JBRO_SHAPE_PROPERTIES(Square2D);

	registry.RegisterComponent<Circle2D>({ "Circle2D", "Circle 2D", "Rendering", true })
		.AddProperty("Radius", EReflectPropertyType::Float, offsetof(Circle2D, m_radius), sizeof(float))
		.AddProperty("Segments", EReflectPropertyType::UInt32, offsetof(Circle2D, Segments), sizeof(std::uint32_t))
		JBRO_RENDERER2D_PROPERTIES(Circle2D)
		JBRO_SHAPE_PROPERTIES(Circle2D);

	registry.RegisterComponent<Polygon2D>({ "Polygon2D", "Polygon 2D", "Rendering", true })
		.AddProperty("Radius", EReflectPropertyType::Float, offsetof(Polygon2D, m_radius), sizeof(float))
		.AddProperty("VertexCount", EReflectPropertyType::UInt32, offsetof(Polygon2D, m_vertexCount), sizeof(std::uint32_t))
		.AddProperty("StartAngle", EReflectPropertyType::Radian, offsetof(Polygon2D, m_startAngle), sizeof(Radian))
		JBRO_RENDERER2D_PROPERTIES(Polygon2D)
		JBRO_SHAPE_PROPERTIES(Polygon2D);

	registry.RegisterComponent<SpriteAnimator2D>({ "SpriteAnimator2D", "Sprite Animator 2D", "Rendering", true })
		.AddArrayProperty<AssetGuid, EReflectPropertyType::AssetGuid>(
			"ClipGuids", offsetof(SpriteAnimator2D, ClipGuids), true, EAssetType::AnimationClip)
		.AddProperty("DefaultClip", EReflectPropertyType::String, offsetof(SpriteAnimator2D, DefaultClip), sizeof(String))
		.AddProperty("FramesPerSecond", EReflectPropertyType::Float, offsetof(SpriteAnimator2D, FramesPerSecond), sizeof(float))
		.AddProperty("Loop", EReflectPropertyType::Bool, offsetof(SpriteAnimator2D, Loop), sizeof(bool))
		.AddProperty("Playing", EReflectPropertyType::Bool, offsetof(SpriteAnimator2D, Playing), sizeof(bool))
		.AddProperty("StartFrame", EReflectPropertyType::UInt32, offsetof(SpriteAnimator2D, StartFrame), sizeof(std::uint32_t))
		.AddProperty("FrameCount", EReflectPropertyType::UInt32, offsetof(SpriteAnimator2D, FrameCount), sizeof(std::uint32_t));

	// 조인트 — 상대 오브젝트를 Ref 로 지목한다. 비워 두면 ConnectedAnchor 가 월드 고정점이다.
	registry.RegisterComponent<DistanceJoint2D>({ "DistanceJoint2D", "Distance Joint 2D", "Physics", true })
		.AddRefProperty<CGameObject>("ConnectedObject", offsetof(DistanceJoint2D, ConnectedObject), "CGameObject")
		.AddProperty("LocalAnchor", EReflectPropertyType::Vector2Float, offsetof(DistanceJoint2D, LocalAnchor), sizeof(Vector2))
		.AddProperty("ConnectedAnchor", EReflectPropertyType::Vector2Float, offsetof(DistanceJoint2D, ConnectedAnchor), sizeof(Vector2))
		.AddProperty("AutoConfigureDistance", EReflectPropertyType::Bool, offsetof(DistanceJoint2D, AutoConfigureDistance), sizeof(bool))
		.AddProperty("Distance", EReflectPropertyType::Float, offsetof(DistanceJoint2D, Distance), sizeof(float))
		.AddProperty("MaxDistanceOnly", EReflectPropertyType::Bool, offsetof(DistanceJoint2D, MaxDistanceOnly), sizeof(bool))
		.AddProperty("Frequency", EReflectPropertyType::Float, offsetof(DistanceJoint2D, Frequency), sizeof(float))
		.AddProperty("DampingRatio", EReflectPropertyType::Float, offsetof(DistanceJoint2D, DampingRatio), sizeof(float));

	registry.RegisterComponent<HingeJoint2D>({ "HingeJoint2D", "Hinge Joint 2D", "Physics", true })
		.AddRefProperty<CGameObject>("ConnectedObject", offsetof(HingeJoint2D, ConnectedObject), "CGameObject")
		.AddProperty("LocalAnchor", EReflectPropertyType::Vector2Float, offsetof(HingeJoint2D, LocalAnchor), sizeof(Vector2))
		.AddProperty("ConnectedAnchor", EReflectPropertyType::Vector2Float, offsetof(HingeJoint2D, ConnectedAnchor), sizeof(Vector2))
		.AddProperty("AutoConfigureConnectedAnchor", EReflectPropertyType::Bool, offsetof(HingeJoint2D, AutoConfigureConnectedAnchor), sizeof(bool));

	registry.RegisterComponent<Camera2D>({ "Camera2D", "Camera 2D", "Rendering", true, EComponentMultiplicity::Single })
		.AddEnumProperty<ECameraProjectionMode2D>("ProjectionMode", offsetof(Camera2D, ProjectionMode))
		.AddProperty("OrthographicSize", EReflectPropertyType::Float, offsetof(Camera2D, OrthographicSize), sizeof(float))
		.AddProperty("PerspectiveFovDegrees", EReflectPropertyType::Float, offsetof(Camera2D, PerspectiveFovDegrees), sizeof(float))
		.AddProperty("NearClip", EReflectPropertyType::Float, offsetof(Camera2D, NearClip), sizeof(float))
		.AddProperty("FarClip", EReflectPropertyType::Float, offsetof(Camera2D, FarClip), sizeof(float));

	registry.RegisterComponent<Light2D>({ "Light2D", "Light 2D", "Rendering", true })
		.AddEnumProperty<ELight2DType>("LightType", offsetof(Light2D, Type))   // 키 "Type" 은 컴포넌트 판별자와 충돌 → LightType
		.AddProperty("Color", EReflectPropertyType::ColorFloat4, offsetof(Light2D, Color), sizeof(Color))
		.AddProperty("Intensity", EReflectPropertyType::Float, offsetof(Light2D, Intensity), sizeof(float))
		.AddProperty("Range", EReflectPropertyType::Float, offsetof(Light2D, Range), sizeof(float))
		.AddProperty("InnerAngleRadians", EReflectPropertyType::Float, offsetof(Light2D, InnerAngleRadians), sizeof(float))
		.AddProperty("OuterAngleRadians", EReflectPropertyType::Float, offsetof(Light2D, OuterAngleRadians), sizeof(float))
		.AddProperty("CastShadows", EReflectPropertyType::Bool, offsetof(Light2D, CastShadows), sizeof(bool))
		.AddProperty("ShadowLength", EReflectPropertyType::Float, offsetof(Light2D, ShadowLength), sizeof(float))
		.AddProperty("ShadowSoftness", EReflectPropertyType::Float, offsetof(Light2D, ShadowSoftness), sizeof(float));

	registry.RegisterComponent<Rigidbody2D>({ "Rigidbody2D", "Rigidbody 2D", "Physics", true, EComponentMultiplicity::Single })
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
		.AddProperty("IsTrigger", EReflectPropertyType::Bool, offsetof(PolygonCollider2D, IsTrigger), sizeof(bool))
		.AddProperty("CollisionLayer", EReflectPropertyType::UInt32, offsetof(PolygonCollider2D, CollisionLayer), sizeof(std::uint32_t))
		.AddProperty("CollisionMask", EReflectPropertyType::UInt32, offsetof(PolygonCollider2D, CollisionMask), sizeof(std::uint32_t));

	registry.RegisterComponent<CircleCollider2D>({ "CircleCollider2D", "Circle Collider 2D", "Physics", true })
		.AddProperty("Radius", EReflectPropertyType::Float, offsetof(CircleCollider2D, Radius), sizeof(float))
		.AddProperty("Offset", EReflectPropertyType::Vector2Float, offsetof(CircleCollider2D, Offset), sizeof(Vector2))
		.AddProperty("IsTrigger", EReflectPropertyType::Bool, offsetof(CircleCollider2D, IsTrigger), sizeof(bool))
		.AddProperty("CollisionLayer", EReflectPropertyType::UInt32, offsetof(CircleCollider2D, CollisionLayer), sizeof(std::uint32_t))
		.AddProperty("CollisionMask", EReflectPropertyType::UInt32, offsetof(CircleCollider2D, CollisionMask), sizeof(std::uint32_t));

	registry.RegisterComponent<PrefabInstance>({ "PrefabInstance", "Prefab Instance", "Prefab", false })
		.AddAssetProperty("SourcePrefabGuid", offsetof(PrefabInstance, SourcePrefabGuid), EAssetType::Prefab);


	registry.RegisterComponent<AudioListener>({ "AudioListener", "Audio Listener", "Audio", true, EComponentMultiplicity::Single })
		.AddProperty("MasterVolume", EReflectPropertyType::Float, offsetof(AudioListener, MasterVolume), sizeof(float));

	registry.RegisterComponent<AudioPlayer>({ "AudioPlayer", "Audio Player", "Audio", true })
		.AddAssetProperty("AudioGuid", offsetof(AudioPlayer, AudioGuid), EAssetType::Audio)
		.AddArrayProperty<AssetGuid, EReflectPropertyType::AssetGuid>(
			"EffectGuids", offsetof(AudioPlayer, EffectGuids), true, EAssetType::AudioEffect)
		.AddAudioBusProperty("Bus", offsetof(AudioPlayer, Bus), sizeof(String))
		.AddProperty("Volume",      EReflectPropertyType::Float,     offsetof(AudioPlayer, Volume),      sizeof(float))
		.AddProperty("Pitch",       EReflectPropertyType::Float,     offsetof(AudioPlayer, Pitch),       sizeof(float))
		.AddProperty("Loop",        EReflectPropertyType::Bool,      offsetof(AudioPlayer, Loop),        sizeof(bool))
		.AddProperty("Is3D",        EReflectPropertyType::Bool,      offsetof(AudioPlayer, Is3D),        sizeof(bool))
		.AddProperty("MinDistance", EReflectPropertyType::Float,     offsetof(AudioPlayer, MinDistance), sizeof(float))
		.AddProperty("MaxDistance", EReflectPropertyType::Float,     offsetof(AudioPlayer, MaxDistance), sizeof(float))
		.AddEnumProperty<EAudioAttenuationModel>("AttenuationModel", offsetof(AudioPlayer, AttenuationModel))
		.AddProperty("Rolloff",     EReflectPropertyType::Float,     offsetof(AudioPlayer, Rolloff),     sizeof(float))
		.AddProperty("PlayOnStart", EReflectPropertyType::Bool,      offsetof(AudioPlayer, PlayOnStart), sizeof(bool));
}
