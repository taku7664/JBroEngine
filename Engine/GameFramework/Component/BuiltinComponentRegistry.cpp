#include "pch.h"
#include "BuiltinComponentRegistry.h"

#include "GameFramework/Component/Camera2D.h"
#include "GameFramework/Component/GameObject.h"
#include "GameFramework/Component/Light2D.h"
#include "GameFramework/Component/Physics2DComponents.h"
#include "GameFramework/Component/PrefabInstance.h"
#include "GameFramework/Component/ScriptComponent.h"
#include "GameFramework/Component/SpriteRenderer2D.h"
#include "GameFramework/Component/Transform2D.h"
#include "GameFramework/Component/TransformHierarchy2D.h"
#include "GameFramework/Reflection/ReflectionRegistry.h"

#include <cstddef>

void RegisterBuiltinComponents(CReflectionRegistry& registry)
{
	registry.RegisterComponent<GameObject>({ "GameObject", "Game Object", "Common", false })
		.AddProperty("Name", EReflectPropertyType::String, offsetof(GameObject, Name), sizeof(GameObject::Name), GameObject::MAX_NAME_LENGTH + 1)
		.AddProperty("IsActive", EReflectPropertyType::Bool, offsetof(GameObject, IsActive), sizeof(bool))
		.AddProperty("Layer", EReflectPropertyType::UInt32, offsetof(GameObject, Layer), sizeof(std::uint32_t));

	registry.RegisterComponent<Transform2D>({ "Transform2D", "Transform 2D", "Common", false })
		.AddProperty("Position", EReflectPropertyType::Vector2Float, offsetof(Transform2D, Position), sizeof(Vector2<float>))
		.AddProperty("RotationRadians", EReflectPropertyType::Float, offsetof(Transform2D, RotationRadians), sizeof(float))
		.AddProperty("Scale", EReflectPropertyType::Vector2Float, offsetof(Transform2D, Scale), sizeof(Vector2<float>));

	registry.RegisterComponent<TransformHierarchy2D>({ "TransformHierarchy2D", "Transform Hierarchy 2D", "Common", false });

	registry.RegisterComponent<SpriteRenderer2D>({ "SpriteRenderer2D", "Sprite Renderer 2D", "Rendering", true })
		.AddProperty("SpriteGuid", EReflectPropertyType::AssetGuid, offsetof(SpriteRenderer2D, SpriteGuid), sizeof(AssetGuid))
		.AddProperty("MaterialGuid", EReflectPropertyType::AssetGuid, offsetof(SpriteRenderer2D, MaterialGuid), sizeof(AssetGuid))
		.AddProperty("Color", EReflectPropertyType::ColorFloat4, offsetof(SpriteRenderer2D, Color), sizeof(float), 4)
		.AddProperty("SortOrder", EReflectPropertyType::Int32, offsetof(SpriteRenderer2D, SortOrder), sizeof(std::int32_t))
		.AddProperty("LayerMask", EReflectPropertyType::UInt32, offsetof(SpriteRenderer2D, LayerMask), sizeof(RenderLayerMask));

	registry.RegisterComponent<Camera2D>({ "Camera2D", "Camera 2D", "Rendering", true })
		.AddProperty("ProjectionMode", EReflectPropertyType::Enum, offsetof(Camera2D, ProjectionMode), sizeof(ECameraProjectionMode2D))
		.AddProperty("OrthographicSize", EReflectPropertyType::Float, offsetof(Camera2D, OrthographicSize), sizeof(float))
		.AddProperty("PerspectiveFovDegrees", EReflectPropertyType::Float, offsetof(Camera2D, PerspectiveFovDegrees), sizeof(float))
		.AddProperty("NearClip", EReflectPropertyType::Float, offsetof(Camera2D, NearClip), sizeof(float))
		.AddProperty("FarClip", EReflectPropertyType::Float, offsetof(Camera2D, FarClip), sizeof(float))
		.AddProperty("ClearColor", EReflectPropertyType::ColorFloat4, offsetof(Camera2D, ClearColor), sizeof(float), 4)
		.AddProperty("LayerMask", EReflectPropertyType::UInt32, offsetof(Camera2D, LayerMask), sizeof(std::uint32_t))
		.AddProperty("Priority", EReflectPropertyType::Int32, offsetof(Camera2D, Priority), sizeof(std::int32_t))
		.AddProperty("IsMainCamera", EReflectPropertyType::Bool, offsetof(Camera2D, IsMainCamera), sizeof(bool));

	registry.RegisterComponent<Light2D>({ "Light2D", "Light 2D", "Rendering", true })
		.AddProperty("Type", EReflectPropertyType::Enum, offsetof(Light2D, Type), sizeof(ELight2DType))
		.AddProperty("Color", EReflectPropertyType::ColorFloat4, offsetof(Light2D, Color), sizeof(float), 4)
		.AddProperty("Intensity", EReflectPropertyType::Float, offsetof(Light2D, Intensity), sizeof(float))
		.AddProperty("Range", EReflectPropertyType::Float, offsetof(Light2D, Range), sizeof(float))
		.AddProperty("LayerMask", EReflectPropertyType::UInt32, offsetof(Light2D, LayerMask), sizeof(std::uint32_t))
		.AddProperty("CastShadows", EReflectPropertyType::Bool, offsetof(Light2D, CastShadows), sizeof(bool));

	registry.RegisterComponent<Rigidbody2D>({ "Rigidbody2D", "Rigidbody 2D", "Physics", true })
		.AddProperty("BodyType", EReflectPropertyType::Enum, offsetof(Rigidbody2D, BodyType), sizeof(EPhysics2DBodyType))
		.AddProperty("Velocity", EReflectPropertyType::Vector2Float, offsetof(Rigidbody2D, Velocity), sizeof(Vector2<float>))
		.AddProperty("Force", EReflectPropertyType::Vector2Float, offsetof(Rigidbody2D, Force), sizeof(Vector2<float>))
		.AddProperty("Mass", EReflectPropertyType::Float, offsetof(Rigidbody2D, Mass), sizeof(float))
		.AddProperty("Friction", EReflectPropertyType::Float, offsetof(Rigidbody2D, Friction), sizeof(float))
		.AddProperty("Restitution", EReflectPropertyType::Float, offsetof(Rigidbody2D, Restitution), sizeof(float))
		.AddProperty("LinearDamping", EReflectPropertyType::Float, offsetof(Rigidbody2D, LinearDamping), sizeof(float))
		.AddProperty("GravityScale", EReflectPropertyType::Float, offsetof(Rigidbody2D, GravityScale), sizeof(float))
		.AddProperty("UseGravity", EReflectPropertyType::Bool, offsetof(Rigidbody2D, UseGravity), sizeof(bool))
		.AddProperty("FreezePositionX", EReflectPropertyType::Bool, offsetof(Rigidbody2D, FreezePositionX), sizeof(bool))
		.AddProperty("FreezePositionY", EReflectPropertyType::Bool, offsetof(Rigidbody2D, FreezePositionY), sizeof(bool));

	registry.RegisterComponent<PolygonCollider2D>({ "PolygonCollider2D", "Polygon Collider 2D", "Physics", true })
		.AddProperty("IsTrigger", EReflectPropertyType::Bool, offsetof(PolygonCollider2D, IsTrigger), sizeof(bool))
		.AddProperty("IsConvex", EReflectPropertyType::Bool, offsetof(PolygonCollider2D, IsConvex), sizeof(bool));

	registry.RegisterComponent<CircleCollider2D>({ "CircleCollider2D", "Circle Collider 2D", "Physics", true })
		.AddProperty("LocalCenter", EReflectPropertyType::Vector2Float, offsetof(CircleCollider2D, LocalCenter), sizeof(Vector2<float>))
		.AddProperty("Radius", EReflectPropertyType::Float, offsetof(CircleCollider2D, Radius), sizeof(float))
		.AddProperty("IsTrigger", EReflectPropertyType::Bool, offsetof(CircleCollider2D, IsTrigger), sizeof(bool));

	registry.RegisterComponent<PrefabInstance>({ "PrefabInstance", "Prefab Instance", "Prefab", false })
		.AddProperty("SourcePrefabGuid", EReflectPropertyType::AssetGuid, offsetof(PrefabInstance, SourcePrefabGuid), sizeof(AssetGuid));

	registry.RegisterComponent<ScriptComponent>({ "ScriptComponent", "Script Component", "Scripting", true })
		.AddProperty("IsEnabled", EReflectPropertyType::Bool, offsetof(ScriptComponent, IsEnabled), sizeof(bool));
}
