#include "pch.h"
#include "EditorObjectCommands.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/Core/EngineCore.h"
#include "Engine/Core/Logging/LoggerInternal.h"
#include "Engine/GameFramework/Component/Physics2DComponents.h"
#include "Engine/GameFramework/Scripting/GameScript.h"
#include "Engine/GameFramework/Component/Transform2D.h"
#include "Engine/GameFramework/Object/GameObject.h"
#include "Engine/GameFramework/Reflection/ReflectionRegistry.h"
#include "Engine/GameFramework/Canvas/GameLayer.h"
#include "Engine/GameFramework/Canvas/Canvas.h"
#include "Engine/GameFramework/Canvas/CanvasRuntimeAccess.h"
#include "Engine/GameFramework/Canvas/CanvasTransformUtils.h"
#include "Engine/GameFramework/Serialization/ComponentSerializer.h"
#include "Engine/GameFramework/Serialization/ObjectSerializer.h"
#include "Engine/GameFramework/Prefab/PrefabSerializer.h"

#include <cmath>
#include <utility>

namespace
{
	// 명령은 오브젝트를 InstanceGuid 로 보관한다(포인터/정수 id 아님).
	// 실제 조작 시점에 활성 캔버스에서 guid 로 다시 해석한다(파괴→재생성 후에도 안전).
	CGameObject* Resolve(const SafePtr<CGameCanvas>& canvas, const File::Guid& guid)
	{
		return canvas.IsValid() ? canvas->FindByInstanceGuid(guid).TryGet() : nullptr;
	}

	File::Guid GuidOf(const CGameObject* object)
	{
		return object ? object->GetInstanceGuid() : File::Guid();
	}

	File::Guid GuidOf(const CComponent* component)
	{
		return component ? component->GetInstanceGuid() : File::Guid();
	}

	File::Guid GuidOf(const CGameLayer* layer)
	{
		return layer ? layer->GetInstanceGuid() : File::Guid();
	}
}

CAddComponentCommand::CAddComponentCommand(SafePtr<CGameCanvas> canvas, CGameObject* object, TypeId componentTypeId)
	: m_canvas(canvas)
	, m_objectGuid(GuidOf(object))
	, m_componentTypeId(componentTypeId)
{
}

const char* CAddComponentCommand::GetName() const
{
	return "Add Component";
}

bool CAddComponentCommand::Execute()
{
	CGameObject* object = Resolve(m_canvas, m_objectGuid);
	if (nullptr == object || false == Engine.Reflection.IsValid())
	{
		return false;
	}

	CReflectionRegistry& reflection = *Engine.Reflection;
	if (nullptr == reflection.FindComponent(m_componentTypeId))
	{
		return false;
	}

	if (false == reflection.CanAddComponent(*object, m_componentTypeId))
	{
		return false;
	}
	m_added = reflection.AddComponent(*m_canvas, *object, m_componentTypeId);
	if (m_added)
	{
		const std::vector<void*> instances = reflection.GetComponentAddresses(*object, m_componentTypeId);
		if (false == instances.empty())
		{
			m_componentGuid = GuidOf(static_cast<CComponent*>(instances.back()));
		}
	}

	if (false == m_added)
	{
		CSystemLog::Warning("Failed to add reflected component.");
	}
	return m_added;
}

void CAddComponentCommand::Undo()
{
	CGameObject* object = Resolve(m_canvas, m_objectGuid);
	if (false == m_added || nullptr == object || false == Engine.Reflection.IsValid())
	{
		return;
	}

	Engine.Reflection->RemoveComponentByGuid(*m_canvas, *object, m_componentTypeId, m_componentGuid);
	m_added = false;
}

void CAddComponentCommand::Redo()
{
	if (false == m_added)
	{
		Execute();
	}
}

CAddScriptCommand::CAddScriptCommand(SafePtr<CGameCanvas> canvas, CGameObject* object, TypeId scriptTypeId)
	: m_canvas(canvas)
	, m_objectGuid(GuidOf(object))
	, m_scriptTypeId(scriptTypeId)
{
}

const char* CAddScriptCommand::GetName() const
{
	return "Add Script";
}

bool CAddScriptCommand::Execute()
{
	CGameObject* object = Resolve(m_canvas, m_objectGuid);
	if (nullptr == object || false == Engine.Reflection.IsValid())
	{
		return false;
	}

	CReflectionRegistry& reflection = *Engine.Reflection;
	if (nullptr == reflection.FindScript(m_scriptTypeId))
	{
		CSystemLog::Warning("Failed to add script component. Script type is not registered.");
		return false;
	}

	CGameScript* scriptComponent = CCanvasRuntimeAccess::AddScript(*m_canvas, *object, m_scriptTypeId, reflection);
	if (nullptr == scriptComponent)
	{
		CSystemLog::Warning("Failed to resolve added script component.");
		return false;
	}
	m_scriptComponentGuid = scriptComponent->GetInstanceGuid();

	m_added = true;
	return true;
}

void CAddScriptCommand::Undo()
{
	CGameObject* object = Resolve(m_canvas, m_objectGuid);
	if (false == m_added || nullptr == object || false == Engine.Reflection.IsValid())
	{
		return;
	}

	if (CGameScript* script = CCanvasRuntimeAccess::FindScript(
		*m_canvas,
		*object,
		m_scriptComponentGuid))
	{
		CCanvasRuntimeAccess::DestroyComponent(*m_canvas, script);
	}

	m_added = false;
}

void CAddScriptCommand::Redo()
{
	if (false == m_added)
	{
		Execute();
	}
}

CRemoveScriptCommand::CRemoveScriptCommand(SafePtr<CGameCanvas> canvas, CGameObject* object, const File::Guid& componentGuid)
	: m_canvas(canvas), m_objectGuid(GuidOf(object)), m_componentGuid(componentGuid)
{
	if (object)
	{
		if (CComponent* component = CCanvasRuntimeAccess::FindComponentByGuid(*object, componentGuid))
		{
			m_snapshot = Serialization::SerializeComponent(*component);
		}
		const auto& components = object->GetComponents();
		for (std::size_t i = 0; i < components.size(); ++i)
		{
			const CComponent* component = components[i].TryGet();
			if (component && component->GetInstanceGuid() == componentGuid)
			{
				m_componentIndex = i;
				break;
			}
		}
	}
}

const char* CRemoveScriptCommand::GetName() const { return "Remove Script"; }
bool CRemoveScriptCommand::Execute()
{
	CGameObject* object = Resolve(m_canvas, m_objectGuid);
	CGameScript* script = object
		? CCanvasRuntimeAccess::FindScript(*m_canvas, *object, m_componentGuid)
		: nullptr;
	m_removed = nullptr != script;
	if (script)
	{
		CCanvasRuntimeAccess::DestroyComponent(*m_canvas, script);
	}
	return m_removed;
}
void CRemoveScriptCommand::Undo()
{
	CGameObject* object = Resolve(m_canvas, m_objectGuid);
	if (false == m_removed || nullptr == object) return;
	if (Serialization::DeserializeComponentInto(*object, m_snapshot.c_str()))
	{
		CComponent* restored = object->GetComponents().empty() ? nullptr : object->GetComponents().back().TryGet();
		if (restored)
		{
			m_componentGuid = restored->GetInstanceGuid();
		}
		CCanvasRuntimeAccess::MoveComponent(*object, m_componentGuid, m_componentIndex);
		m_removed = false;
	}
}
void CRemoveScriptCommand::Redo() { if (false == m_removed) Execute(); }

CReorderComponentCommand::CReorderComponentCommand(SafePtr<CGameCanvas> canvas, CGameObject* object, const File::Guid& componentGuid, std::size_t oldIndex, std::size_t newIndex)
	: m_canvas(canvas), m_objectGuid(GuidOf(object)), m_componentGuid(componentGuid), m_oldIndex(oldIndex), m_newIndex(newIndex) {}
const char* CReorderComponentCommand::GetName() const { return "Reorder Component"; }
bool CReorderComponentCommand::Move(std::size_t index)
{
	CGameObject* object = Resolve(m_canvas, m_objectGuid);
	return object && CCanvasRuntimeAccess::MoveComponent(*object, m_componentGuid, index);
}
bool CReorderComponentCommand::Execute() { m_executed = Move(m_newIndex); return m_executed; }
void CReorderComponentCommand::Undo() { if (m_executed) { Move(m_oldIndex); m_executed = false; } }
void CReorderComponentCommand::Redo() { if (false == m_executed) m_executed = Move(m_newIndex); }

CCreateGameObjectCommand::CCreateGameObjectCommand(SafePtr<CGameCanvas> canvas, const char* name,
                                                   CGameObject* parent,
                                                   const Vector2* spawnWorldPos,
                                                   CGameLayer* layer)
	: m_canvas(canvas)
	, m_name(name ? name : "GameObject")
	, m_parentGuid(GuidOf(parent))
	, m_layerGuid(GuidOf(layer))
	, m_hasSpawnPos(nullptr != spawnWorldPos)
	, m_spawnWorldPos(spawnWorldPos ? *spawnWorldPos : Vector2(0.0f, 0.0f))
{
}

const char* CCreateGameObjectCommand::GetName() const
{
	return "Create GameObject";
}

bool CCreateGameObjectCommand::Execute()
{
	if (false == m_canvas.IsValid())
	{
		return false;
	}

	// 레이어 지정 생성 — 캔버스가 배정까지 해준다(미지정 = 기본 레이어). 자식으로 붙는 경우
	// 아래 SetParent 가 부모 레이어로 덮어쓰므로 여기 값은 루트일 때만 남는다.
	CGameLayer* layer = m_layerGuid.IsNull()
		? nullptr
		: m_canvas->FindLayerByInstanceGuid(m_layerGuid).TryGet();
	CGameObject* gameObject = m_canvas->CreateGameObject(m_name.c_str(), layer);
	m_created = (nullptr != gameObject);
	if (false == m_created)
	{
		return false;
	}

	// redo(재생성)면 첫 생성 때의 guid 를 강제 복원해 이후 명령이 동일 오브젝트로 해석되게 한다.
	if (false == m_objectGuid.IsNull())
	{
		CCanvasRuntimeAccess::SetObjectInstanceGuid(*m_canvas, *gameObject, m_objectGuid);
	}
	else
	{
		m_objectGuid = gameObject->GetInstanceGuid();
	}

	// parent 지정 시 자식으로 등록.
	if (false == m_parentGuid.IsNull())
	{
		if (CGameObject* parent = Resolve(m_canvas, m_parentGuid))
		{
			gameObject->SetParent(*parent);
		}
	}

	// 캔버스뷰 우클릭 위치 생성: 월드 좌표를 로컬 좌표로 환산해 배치한다. 부모가 있으면 부모
	// 월드 역행렬로 변환(부모의 캐시된 World 행렬 사용), 루트면 월드=로컬이므로 그대로.
	// 새 오브젝트는 scale=1/rot=0 이라 world 위치 = parentWorld.TransformPoint(localPos).
	if (m_hasSpawnPos)
	{
		Vector2 localPos = m_spawnWorldPos;
		if (CGameObject* parent = Resolve(m_canvas, m_parentGuid))
		{
			Matrix3x2 inverse;
			if (GetWorldTransform(*parent).TryInvert(inverse))
			{
				localPos = inverse.TransformPoint(m_spawnWorldPos);
			}
		}
		gameObject->GetTransform().Position = localPos;
	}

	return m_created;
}

void CCreateGameObjectCommand::Undo()
{
	CGameObject* object = Resolve(m_canvas, m_objectGuid);
	if (m_created && object)
	{
		m_canvas->DestroyGameObject(object);
		m_created = false;
	}
}

void CCreateGameObjectCommand::Redo()
{
	if (false == m_created)
	{
		Execute();
	}
}

CGameObject* CCreateGameObjectCommand::GetEntity() const
{
	return Resolve(m_canvas, m_objectGuid);
}

CSetComponentPropertyCommand::CSetComponentPropertyCommand(
	SafePtr<CGameCanvas> canvas,
	CGameObject* object,
	TypeId componentTypeId,
	std::size_t propertyOffset,
	std::vector<std::uint8_t> oldValue,
	std::vector<std::uint8_t> newValue,
	const File::Guid& componentGuid)
	: m_canvas(canvas)
	, m_objectGuid(GuidOf(object))
	, m_componentTypeId(componentTypeId)
	, m_propertyOffset(propertyOffset)
	, m_componentGuid(componentGuid)
	, m_oldValue(std::move(oldValue))
	, m_newValue(std::move(newValue))
{
}

const char* CSetComponentPropertyCommand::GetName() const
{
	return "Set Component Property";
}

bool CSetComponentPropertyCommand::Execute()
{
	return WriteValue(m_newValue);
}

void CSetComponentPropertyCommand::Undo()
{
	WriteValue(m_oldValue);
}

void CSetComponentPropertyCommand::Redo()
{
	WriteValue(m_newValue);
}

bool CSetComponentPropertyCommand::TryMerge(const IEditorCommand& newer)
{
	const CSetComponentPropertyCommand* other = dynamic_cast<const CSetComponentPropertyCommand*>(&newer);
	if (nullptr == other)
	{
		return false;
	}
	// 같은 대상(오브젝트·컴포넌트·오프셋·인스턴스)일 때만 병합. 값 크기도 일치해야 함.
	const bool sameTarget =
		(m_objectGuid == other->m_objectGuid)
		&& (m_componentTypeId == other->m_componentTypeId)
		&& (m_propertyOffset == other->m_propertyOffset)
		&& (m_componentGuid == other->m_componentGuid)
		&& (m_newValue.size() == other->m_newValue.size());
	if (false == sameTarget)
	{
		return false;
	}
	// old 는 드래그 시작값 유지, new 만 최신값으로 교체 → undo 1개로 시작↔끝 복원.
	m_newValue = other->m_newValue;
	return true;
}

bool CSetComponentPropertyCommand::WriteValue(const std::vector<std::uint8_t>& value)
{
	CGameObject* object = Resolve(m_canvas, m_objectGuid);
	if (value.empty() || nullptr == object || nullptr == m_canvas.TryGet())
	{
		return false;
	}

	CComponent* component = CCanvasRuntimeAccess::FindComponentByGuid(*object, m_componentGuid);
	if (nullptr == component)
	{
		return false;
	}

	std::memcpy(reinterpret_cast<std::uint8_t*>(component) + m_propertyOffset, value.data(), value.size());
	return true;
}

CSetComponentEnabledCommand::CSetComponentEnabledCommand(
	SafePtr<CGameCanvas> canvas,
	CGameObject* object,
	const File::Guid& componentGuid,
	bool oldValue,
	bool newValue)
	: m_canvas(canvas)
	, m_objectGuid(GuidOf(object))
	, m_componentGuid(componentGuid)
	, m_oldValue(oldValue)
	, m_newValue(newValue)
{
}

const char* CSetComponentEnabledCommand::GetName() const
{
	return "Set Component Enabled";
}

bool CSetComponentEnabledCommand::Execute()
{
	return Apply(m_newValue);
}

void CSetComponentEnabledCommand::Undo()
{
	Apply(m_oldValue);
}

void CSetComponentEnabledCommand::Redo()
{
	Apply(m_newValue);
}

bool CSetComponentEnabledCommand::Apply(bool value)
{
	CGameObject* object = Resolve(m_canvas, m_objectGuid);
	CComponent* component = object
		? CCanvasRuntimeAccess::FindComponentByGuid(*object, m_componentGuid)
		: nullptr;
	if (nullptr == component)
	{
		return false;
	}
	component->SetEnabled(value);
	return true;
}

CSetComponentStringPropertyCommand::CSetComponentStringPropertyCommand(SafePtr<CGameCanvas> canvas,
	CGameObject* object, TypeId componentTypeId, std::size_t propertyOffset, std::string oldValue, std::string newValue, const File::Guid& componentGuid)
	: m_canvas(canvas), m_objectGuid(GuidOf(object)), m_componentTypeId(componentTypeId),
	  m_componentGuid(componentGuid), m_propertyOffset(propertyOffset), m_oldValue(std::move(oldValue)), m_newValue(std::move(newValue)) {}

const char* CSetComponentStringPropertyCommand::GetName() const { return "Set Component String Property"; }
bool CSetComponentStringPropertyCommand::Execute() { return WriteValue(m_newValue); }
void CSetComponentStringPropertyCommand::Undo() { WriteValue(m_oldValue); }
void CSetComponentStringPropertyCommand::Redo() { WriteValue(m_newValue); }
bool CSetComponentStringPropertyCommand::TryMerge(const IEditorCommand& newer)
{
	const auto* other = dynamic_cast<const CSetComponentStringPropertyCommand*>(&newer);
	if (!other || m_objectGuid != other->m_objectGuid || m_componentTypeId != other->m_componentTypeId
		|| m_propertyOffset != other->m_propertyOffset || m_componentGuid != other->m_componentGuid) return false;
	m_newValue = other->m_newValue; return true;
}
bool CSetComponentStringPropertyCommand::WriteValue(const std::string& value)
{
	CGameObject* object = Resolve(m_canvas, m_objectGuid);
	if (!object || nullptr == m_canvas.TryGet()) return false;
	CComponent* component = CCanvasRuntimeAccess::FindComponentByGuid(*object, m_componentGuid);
	if (!component) return false;
	*reinterpret_cast<std::string*>(reinterpret_cast<std::uint8_t*>(component) + m_propertyOffset) = value;
	return true;
}

CSetComponentSerializedPropertyCommand::CSetComponentSerializedPropertyCommand(
	SafePtr<CGameCanvas> canvas, CGameObject* object, TypeId componentTypeId,
	std::size_t propertyOffset, std::string oldValue, std::string newValue,
	const File::Guid& componentGuid)
	: m_canvas(canvas), m_objectGuid(GuidOf(object)), m_componentGuid(componentGuid),
	  m_componentTypeId(componentTypeId), m_propertyOffset(propertyOffset),
	  m_oldValue(std::move(oldValue)), m_newValue(std::move(newValue)) {}

const char* CSetComponentSerializedPropertyCommand::GetName() const { return "Set Component Property"; }
bool CSetComponentSerializedPropertyCommand::Execute() { return WriteValue(m_newValue); }
void CSetComponentSerializedPropertyCommand::Undo() { WriteValue(m_oldValue); }
void CSetComponentSerializedPropertyCommand::Redo() { WriteValue(m_newValue); }
bool CSetComponentSerializedPropertyCommand::TryMerge(const IEditorCommand& newer)
{
	const auto* other = dynamic_cast<const CSetComponentSerializedPropertyCommand*>(&newer);
	if (!other || m_objectGuid != other->m_objectGuid || m_componentGuid != other->m_componentGuid
		|| m_componentTypeId != other->m_componentTypeId || m_propertyOffset != other->m_propertyOffset) return false;
	m_newValue = other->m_newValue;
	return true;
}
bool CSetComponentSerializedPropertyCommand::WriteValue(const std::string& value)
{
	CGameObject* object = Resolve(m_canvas, m_objectGuid);
	CComponent* component = object ? CCanvasRuntimeAccess::FindComponentByGuid(*object, m_componentGuid) : nullptr;
	if (!component || !Engine.Reflection.IsValid()) return false;
	const ComponentTypeInfo* type = Engine.Reflection->FindComponent(m_componentTypeId);
	const ScriptTypeInfo* script = type ? nullptr : Engine.Reflection->FindScript(m_componentTypeId);
	const std::vector<ReflectPropertyInfo>* properties = type ? &type->Properties : (script ? &script->Properties : nullptr);
	if (!properties) return false;
	for (const ReflectPropertyInfo& property : *properties)
	{
		if (property.Offset != m_propertyOffset) continue;
		void* field = CReflectionRegistry::GetPropertyAddress(component, property);
		return Serialization::DeserializeReflectedPropertyValue(field, property, value.c_str());
	}
	return false;
}

// ── CDeleteGameObjectCommand ──────────────────────────────────────────────────

CDeleteGameObjectCommand::CDeleteGameObjectCommand(SafePtr<CGameCanvas> canvas, CGameObject* object)
	: m_canvas(canvas)
	, m_objectGuid(GuidOf(object))
{
	if (object && m_canvas.IsValid())
	{
		m_parentGuid = GuidOf(object->GetParent().TryGet());
		m_layerGuid = GuidOf(object->GetLayer().TryGet());
		CPrefabSerializer serializer;
		serializer.SerializePrefabToText(*m_canvas, object, m_snapshot);
	}
}

const char* CDeleteGameObjectCommand::GetName() const
{
	return "Delete GameObject";
}

bool CDeleteGameObjectCommand::Execute()
{
	CGameObject* object = Resolve(m_canvas, m_objectGuid);
	if (nullptr == object)
	{
		return false;
	}
	m_deleted = m_canvas->DestroyGameObject(object);
	return m_deleted;
}

void CDeleteGameObjectCommand::Undo()
{
	if (false == m_deleted || false == m_canvas.IsValid() || m_snapshot.empty())
	{
		return;
	}

	// 직렬화는 InstanceGuid 를 보존하므로 복원된 오브젝트는 동일 guid → 이후 redo/refs 일관.
	CPrefabSerializer serializer;
	CGameObject* root = nullptr;
	if (EPrefabSerializeResult::Success !=
	    serializer.DeserializePrefabFromText(*m_canvas, m_snapshot.c_str(), &root))
	{
		return;
	}

	// 부모 복원(null = 루트).
	if (root && false == m_parentGuid.IsNull())
	{
		if (CGameObject* parent = Resolve(m_canvas, m_parentGuid))
		{
			root->SetParent(*parent);
		}
	}
	// 레이어 복원은 부모 배정 뒤 — SetParent 가 부모 레이어를 서브트리에 전파하므로 순서가
	// 뒤집히면 덮어쓴다. 루트로 복원될 때만 의미가 있다(자식은 부모 레이어를 따른다).
	else if (root && false == m_layerGuid.IsNull())
	{
		if (CGameLayer* layer = m_canvas->FindLayerByInstanceGuid(m_layerGuid).TryGet())
		{
			m_canvas->MoveObjectToLayer(*root, *layer);
		}
	}
	m_deleted = false;
}

void CDeleteGameObjectCommand::Redo()
{
	if (false == m_deleted)
	{
		Execute();
	}
}

// ── CDeleteGameObjectsCommand ─────────────────────────────────────────────────

CDeleteGameObjectsCommand::CDeleteGameObjectsCommand(
	SafePtr<CGameCanvas> canvas,
	const std::vector<CGameObject*>& objects)
	: m_canvas(canvas)
{
	if (false == m_canvas.IsValid())
	{
		return;
	}

	m_entries.reserve(objects.size());
	CPrefabSerializer serializer;
	for (CGameObject* object : objects)
	{
		if (nullptr == object)
		{
			continue;
		}

		Entry entry;
		entry.ObjectGuid = GuidOf(object);
		entry.ParentGuid = GuidOf(object->GetParent().TryGet());
		entry.LayerGuid = GuidOf(object->GetLayer().TryGet());
		serializer.SerializePrefabToText(*m_canvas, object, entry.Snapshot);
		if (false == entry.ObjectGuid.IsNull() && false == entry.Snapshot.empty())
		{
			m_entries.push_back(std::move(entry));
		}
	}
}

const char* CDeleteGameObjectsCommand::GetName() const
{
	return "Delete GameObjects";
}

bool CDeleteGameObjectsCommand::Execute()
{
	if (false == m_canvas.IsValid() || m_entries.empty())
	{
		return false;
	}

	bool deletedAny = false;
	for (Entry& entry : m_entries)
	{
		CGameObject* object = Resolve(m_canvas, entry.ObjectGuid);
		if (nullptr == object)
		{
			entry.Deleted = false;
			continue;
		}

		entry.Deleted = m_canvas->DestroyGameObject(object);
		deletedAny = deletedAny || entry.Deleted;
	}
	return deletedAny;
}

void CDeleteGameObjectsCommand::Undo()
{
	if (false == m_canvas.IsValid())
	{
		return;
	}

	CPrefabSerializer serializer;
	for (Entry& entry : m_entries)
	{
		if (false == entry.Deleted || entry.Snapshot.empty())
		{
			continue;
		}

		CGameObject* root = nullptr;
		if (EPrefabSerializeResult::Success !=
		    serializer.DeserializePrefabFromText(*m_canvas, entry.Snapshot.c_str(), &root))
		{
			continue;
		}

		if (root && false == entry.ParentGuid.IsNull())
		{
			if (CGameObject* parent = Resolve(m_canvas, entry.ParentGuid))
			{
				root->SetParent(*parent);
			}
		}
		// 단일 삭제 undo 와 같은 규칙 — 부모 배정 뒤, 루트일 때만.
		else if (root && false == entry.LayerGuid.IsNull())
		{
			if (CGameLayer* layer = m_canvas->FindLayerByInstanceGuid(entry.LayerGuid).TryGet())
			{
				m_canvas->MoveObjectToLayer(*root, *layer);
			}
		}
		entry.Deleted = false;
	}
}

void CDeleteGameObjectsCommand::Redo()
{
	Execute();
}

// ── CPasteObjectsCommand ──────────────────────────────────────────────────────

namespace
{
	// 붙여넣은 서브트리 전체에 새 InstanceGuid 를 발급한다(오브젝트 + 컴포넌트 + 자식 재귀).
	// 원본과 guid 가 겹치면 캔버스에 같은 guid 가 둘이 되어 Ref/해석이 깨지므로 필수.
	// 오브젝트 guid 는 반드시 캔버스 API(SetObjectInstanceGuid)로 재발급해야 m_objectByGuid 인덱스가
	// 함께 갱신된다. 직접 대입하면 인덱스가 옛 guid 로 남아 FindByInstanceGuid 가 실패
	// → 붙여넣은 오브젝트를 기즈모/커맨드가 못 찾아 이동 등 편집이 안 된다.
	void ReissuePastedGuids(CGameCanvas& canvas, CGameObject& object)
	{
		CCanvasRuntimeAccess::SetObjectInstanceGuid(canvas, object, File::GenerateGuid());
		for (const SafePtr<CComponent>& cref : object.GetComponents())
		{
			if (CComponent* comp = cref.TryGet())
			{
				// 컴포넌트는 캔버스 guid 인덱스가 없어 직접 대입으로 충분.
				CCanvasRuntimeAccess::SetComponentInstanceGuid(*comp, File::GenerateGuid());
			}
		}
		for (const SafePtr<CGameObject>& childRef : object.GetChildren())
		{
			if (CGameObject* child = childRef.TryGet())
			{
				ReissuePastedGuids(canvas, *child);
			}
		}
	}
}

CPasteObjectsCommand::CPasteObjectsCommand(SafePtr<CGameCanvas> canvas, std::string clipboardText,
                                           const Vector2* spawnWorldPos,
                                           CGameObject* parent,
                                           CGameLayer* layer)
	: m_canvas(canvas)
	, m_clipboard(std::move(clipboardText))
	, m_parentGuid(GuidOf(parent))
	, m_layerGuid(GuidOf(layer))
	, m_hasSpawnPos(nullptr != spawnWorldPos)
	, m_spawnWorldPos(spawnWorldPos ? *spawnWorldPos : Vector2(0.0f, 0.0f))
{
}

const char* CPasteObjectsCommand::GetName() const
{
	return "Paste Objects";
}

bool CPasteObjectsCommand::Execute()
{
	if (false == m_canvas.IsValid())
	{
		return false;
	}

	std::vector<CGameObject*> roots = Serialization::DeserializeObjects(*m_canvas, m_clipboard.c_str());
	if (roots.empty())
	{
		return false;
	}

	// 최초 붙여넣기에서만 새 guid 발급 + 위치 이동 + 정규화 스냅샷 재보관.
	// 이후 redo 는 정규화 스냅샷을 그대로 복원하므로 guid/위치가 동일하게 재현된다.
	std::vector<Vector2> targetWorldPositions;
	if (false == m_firstDone)
	{
		for (CGameObject* root : roots)
		{
			if (root) ReissuePastedGuids(*m_canvas, *root);
		}

		// 위치: 역직렬화 직후 루트는 캔버스 루트에 있으므로 local==world 로 취급한다.
		// parent 가 있으면 최종 월드 위치를 parent local 로 환산한 뒤 자식으로 붙인다.
		targetWorldPositions.reserve(roots.size());
		for (CGameObject* root : roots)
		{
			targetWorldPositions.push_back(root ? root->GetTransform().Position : Vector2(0.0f, 0.0f));
		}

		// spawn 좌표가 있으면 루트들의 위치 평균(중심)을 spawn 좌표로 이동(상대 배치 보존).
		if (m_hasSpawnPos)
		{
			Vector2 anchor(0.0f, 0.0f);
			int count = 0;
			for (std::size_t i = 0; i < roots.size(); ++i)
			{
				if (roots[i])
				{
					anchor += targetWorldPositions[i];
					++count;
				}
			}
			if (count > 0)
			{
				anchor = anchor / static_cast<float>(count);
				const Vector2 delta = m_spawnWorldPos - anchor;
				for (std::size_t i = 0; i < roots.size(); ++i)
				{
					if (roots[i])
					{
						targetWorldPositions[i] += delta;
					}
				}
			}
		}
	}

	// 부모·레이어 배정은 redo 에서도 매번 한다 — 정규화 스냅샷(오브젝트 직렬화)은 계층도
	// 레이어도 담지 않아서(둘 다 캔버스 레벨 관심사) 역직렬화 직후 루트는 늘 캔버스 루트 + 기본
	// 레이어로 되살아난다. 최초 실행에서만 하면 undo→redo 가 부모와 레이어를 잃는다.
	CGameObject* parent = Resolve(m_canvas, m_parentGuid);
	Matrix3x2 parentInverse;
	const bool hasParentInverse =
		parent && GetWorldTransform(*parent).TryInvert(parentInverse);
	// 부모가 있으면 레이어 인자는 무시된다 — SetParent 가 부모 레이어를 서브트리에 전파한다.
	CGameLayer* layer = (nullptr == parent && false == m_layerGuid.IsNull())
		? m_canvas->FindLayerByInstanceGuid(m_layerGuid).TryGet()
		: nullptr;

	m_pastedGuids.clear();
	std::vector<const CGameObject*> created;
	created.reserve(roots.size());
	for (std::size_t i = 0; i < roots.size(); ++i)
	{
		CGameObject* root = roots[i];
		if (nullptr == root)
		{
			continue;
		}

		if (parent)
		{
			root->SetParent(*parent);
			if (false == m_firstDone && hasParentInverse)
			{
				root->GetTransform().Position = parentInverse.TransformPoint(targetWorldPositions[i]);
			}
		}
		else
		{
			if (layer)
			{
				m_canvas->MoveObjectToLayer(*root, *layer);
			}
			if (false == m_firstDone)
			{
				root->GetTransform().Position = targetWorldPositions[i];
			}
		}

		m_pastedGuids.push_back(root->GetInstanceGuid());
		created.push_back(root);
	}

	if (false == m_firstDone)
	{
		m_clipboard = Serialization::SerializeObjects(created);
		m_firstDone = true;
	}

	return false == m_pastedGuids.empty();
}

void CPasteObjectsCommand::Undo()
{
	if (false == m_canvas.IsValid())
	{
		return;
	}
	for (const File::Guid& guid : m_pastedGuids)
	{
		if (CGameObject* object = Resolve(m_canvas, guid))
		{
			m_canvas->DestroyGameObject(object);
		}
	}
}

void CPasteObjectsCommand::Redo()
{
	Execute();
}

std::vector<CGameObject*> CPasteObjectsCommand::GetPastedRoots() const
{
	std::vector<CGameObject*> result;
	for (const File::Guid& guid : m_pastedGuids)
	{
		if (CGameObject* object = Resolve(m_canvas, guid))
		{
			result.push_back(object);
		}
	}
	return result;
}

// ── CRemoveComponentCommand ───────────────────────────────────────────────────

CRemoveComponentCommand::CRemoveComponentCommand(SafePtr<CGameCanvas> canvas, CGameObject* object, TypeId componentTypeId, const File::Guid& componentGuid)
	: m_canvas(canvas)
	, m_objectGuid(GuidOf(object))
	, m_componentGuid(componentGuid)
	, m_componentTypeId(componentTypeId)
{
	// 제거 전 스냅샷 — undo 시 재부착+값 복원.
	if (object && Engine.Reflection.IsValid())
	{
		if (void* addr = Engine.Reflection->GetComponentAddressByGuid(*object, m_componentTypeId, m_componentGuid))
		{
			// 컴포넌트는 CComponent 를 단일 1차 베이스(offset 0)로 상속 → reinterpret 안전.
			m_snapshot = Serialization::SerializeComponent(*reinterpret_cast<CComponent*>(addr));
		}
	}
}

const char* CRemoveComponentCommand::GetName() const
{
	return "Remove Component";
}

bool CRemoveComponentCommand::RemoveNow()
{
	CGameObject* object = Resolve(m_canvas, m_objectGuid);
	if (nullptr == object || false == Engine.Reflection.IsValid())
	{
		return false;
	}
	return Engine.Reflection->RemoveComponentByGuid(*m_canvas, *object, m_componentTypeId, m_componentGuid);
}

bool CRemoveComponentCommand::Execute()
{
	m_removed = RemoveNow();
	return m_removed;
}

void CRemoveComponentCommand::Undo()
{
	CGameObject* object = Resolve(m_canvas, m_objectGuid);
	if (false == m_removed || nullptr == object || m_snapshot.empty())
	{
		return;
	}
	Serialization::DeserializeComponentInto(*object, m_snapshot.c_str());
	m_removed = false;
}

void CRemoveComponentCommand::Redo()
{
	if (false == m_removed)
	{
		m_removed = RemoveNow();
	}
}

// ── CSetObjectTransformCommand ────────────────────────────────────────────────

CSetObjectTransformCommand::CSetObjectTransformCommand(SafePtr<CGameCanvas> canvas,
                                                       const std::vector<CGameObject*>& objects,
                                                       const Transform2D& delta)
	: m_canvas(canvas)
	, m_delta(delta)
{
	// 각 대상의 현재 Transform 을 시작값으로 캡처(병렬). guid 로 보관해 파괴/재생성에도 안전.
	for (CGameObject* obj : objects)
	{
		if (nullptr == obj)
		{
			continue;
		}
		m_objectGuids.push_back(obj->GetInstanceGuid());
		m_oldTransforms.push_back(obj->GetTransform());
	}
}

const char* CSetObjectTransformCommand::GetName() const
{
	return "Set Transform";
}

void CSetObjectTransformCommand::Apply(bool withDelta)
{
	for (std::size_t i = 0; i < m_objectGuids.size(); ++i)
	{
		CGameObject* object = Resolve(m_canvas, m_objectGuids[i]);
		if (nullptr == object)
		{
			continue;
		}
		Transform2D r = m_oldTransforms[i];
		if (withDelta)
		{
			r.Position.x          += m_delta.Position.x;
			r.Position.y          += m_delta.Position.y;
			r.RotationRadians.Value += m_delta.RotationRadians.Value;
			r.Scale.x             += m_delta.Scale.x;
			r.Scale.y             += m_delta.Scale.y;
		}
		object->GetTransform() = r;
	}
}

bool CSetObjectTransformCommand::Execute() { Apply(true);  return false == m_objectGuids.empty(); }
void CSetObjectTransformCommand::Undo()    { Apply(false); }
void CSetObjectTransformCommand::Redo()    { Apply(true); }

bool CSetObjectTransformCommand::TryMerge(const IEditorCommand& newer)
{
	const CSetObjectTransformCommand* other = dynamic_cast<const CSetObjectTransformCommand*>(&newer);
	if (nullptr == other || m_objectGuids.size() != other->m_objectGuids.size())
	{
		return false;
	}
	for (std::size_t i = 0; i < m_objectGuids.size(); ++i)
	{
		if (false == (m_objectGuids[i] == other->m_objectGuids[i]))
		{
			return false;
		}
	}
	// 같은 대상 집합 → 델타만 누적(시작값 유지) → undo 1개로 시작↔끝.
	m_delta.Position.x          += other->m_delta.Position.x;
	m_delta.Position.y          += other->m_delta.Position.y;
	m_delta.RotationRadians.Value += other->m_delta.RotationRadians.Value;
	m_delta.Scale.x             += other->m_delta.Scale.x;
	m_delta.Scale.y             += other->m_delta.Scale.y;
	return true;
}

// ── CSetObjectTransformsCommand ───────────────────────────────────────────────

CSetObjectTransformsCommand::CSetObjectTransformsCommand(
	SafePtr<CGameCanvas> canvas,
	const std::vector<CGameObject*>& objects,
	const std::vector<Transform2D>& oldTransforms,
	const std::vector<Transform2D>& newTransforms)
	: m_canvas(canvas)
{
	const std::size_t count = std::min(objects.size(), std::min(oldTransforms.size(), newTransforms.size()));
	m_objectGuids.reserve(count);
	m_oldTransforms.reserve(count);
	m_newTransforms.reserve(count);

	for (std::size_t i = 0; i < count; ++i)
	{
		CGameObject* object = objects[i];
		if (nullptr == object)
		{
			continue;
		}
		m_objectGuids.push_back(object->GetInstanceGuid());
		m_oldTransforms.push_back(oldTransforms[i]);
		m_newTransforms.push_back(newTransforms[i]);
	}
}

const char* CSetObjectTransformsCommand::GetName() const
{
	return "Set Transforms";
}

bool CSetObjectTransformsCommand::Execute()
{
	Apply(m_newTransforms);
	return false == m_objectGuids.empty();
}

void CSetObjectTransformsCommand::Undo()
{
	Apply(m_oldTransforms);
}

void CSetObjectTransformsCommand::Redo()
{
	Apply(m_newTransforms);
}

bool CSetObjectTransformsCommand::TryMerge(const IEditorCommand& newer)
{
	const CSetObjectTransformsCommand* other = dynamic_cast<const CSetObjectTransformsCommand*>(&newer);
	if (nullptr == other || m_objectGuids != other->m_objectGuids)
	{
		return false;
	}
	// old 는 드래그 시작값을 유지하고 new 만 최신으로 교체 → undo 1개로 시작↔끝을 오간다.
	// 절대값 스냅샷이라 몇 번을 교체해도 시작값이 오염되지 않는다(델타였다면 누적됐다).
	m_newTransforms = other->m_newTransforms;
	return true;
}

void CSetObjectTransformsCommand::Apply(const std::vector<Transform2D>& transforms)
{
	const std::size_t count = std::min(m_objectGuids.size(), transforms.size());
	for (std::size_t i = 0; i < count; ++i)
	{
		CGameObject* object = Resolve(m_canvas, m_objectGuids[i]);
		if (nullptr == object)
		{
			continue;
		}
		object->GetTransform() = transforms[i];
	}
}

// ── CSetParentCommand ─────────────────────────────────────────────────────────

namespace
{
	// World matrix → Transform2D 분해. 전제: scale > 0, shear 없음.
	Transform2D DecomposeMatrix(const Matrix3x2& m)
	{
		Transform2D t;
		t.Position.x       = m.Dx;
		t.Position.y       = m.Dy;
		t.Scale.x          = std::sqrt(m.M11 * m.M11 + m.M12 * m.M12);
		t.Scale.y          = std::sqrt(m.M21 * m.M21 + m.M22 * m.M22);
		t.RotationRadians  = std::atan2(m.M12, m.M11);
		return t;
	}
} // anonymous namespace

CSetParentCommand::CSetParentCommand(SafePtr<CGameCanvas> canvas, CGameObject* child, CGameObject* newParent)
	: m_canvas(canvas)
	, m_childGuid(GuidOf(child))
	, m_newParentGuid(GuidOf(newParent))
{
	// 생성 시점에 현재 부모와 로컬 Transform 스냅샷 — Undo 복원용.
	if (child)
	{
		CGameObject* oldParent = child->GetParent().TryGet();
		m_oldParentGuid = GuidOf(oldParent);
		m_oldLocalTransform = child->GetTransform();
	}
}

const char* CSetParentCommand::GetName() const
{
	return "Set Parent";
}

namespace
{
	// child 의 부모를 newParent(null guid 면 루트)로 설정. 성공 시 true.
	bool ApplyParent(CGameCanvas& canvas, CGameObject& child, const File::Guid& newParentGuid)
	{
		if (newParentGuid.IsNull())
		{
			child.ClearParent();
			return true;
		}
		CGameObject* parent = canvas.FindByInstanceGuid(newParentGuid).TryGet();
		return parent ? child.SetParent(*parent) : false;
	}
}

bool CSetParentCommand::Execute()
{
	CGameObject* childObject = Resolve(m_canvas, m_childGuid);
	if (nullptr == childObject)
	{
		return false;
	}
	CGameCanvas& canvas = *m_canvas;

	// ── WorldStay: SetParent 이전 world transform 캡처 ───────────────────────
	const Matrix3x2 childWorld = GetWorldTransform(*childObject);
	CGameObject* newParentObject = m_newParentGuid.IsNull() ? nullptr : canvas.FindByInstanceGuid(m_newParentGuid).TryGet();
	const Matrix3x2 newParentWorld = newParentObject ? GetWorldTransform(*newParentObject) : Matrix3x2::Identity();

	if (false == ApplyParent(canvas, *childObject, m_newParentGuid))
	{
		return false;   // 사이클/자기자신 등 거부
	}

	// childWorld = newLocal × newParentWorld → newLocal = childWorld × inverse(newParentWorld)
	Matrix3x2 newLocal = childWorld;
	if (newParentObject)
	{
		Matrix3x2 parentInv;
		if (newParentWorld.TryInvert(parentInv))
		{
			newLocal = childWorld * parentInv;
		}
	}

	m_newLocalTransform = DecomposeMatrix(newLocal);
	childObject->GetTransform() = m_newLocalTransform;
	m_executed = true;
	return true;
}

void CSetParentCommand::Undo()
{
	CGameObject* childObject = Resolve(m_canvas, m_childGuid);
	if (false == m_executed || nullptr == childObject)
	{
		return;
	}

	ApplyParent(*m_canvas, *childObject, m_oldParentGuid);
	childObject->GetTransform() = m_oldLocalTransform;
	m_executed = false;
}

void CSetParentCommand::Redo()
{
	CGameObject* childObject = Resolve(m_canvas, m_childGuid);
	if (m_executed || nullptr == childObject)
	{
		return;
	}

	ApplyParent(*m_canvas, *childObject, m_newParentGuid);
	childObject->GetTransform() = m_newLocalTransform;
	m_executed = true;
}

// ── CMoveGameObjectInHierarchyCommand ────────────────────────────────────────

CMoveGameObjectInHierarchyCommand::CMoveGameObjectInHierarchyCommand(
	SafePtr<CGameCanvas> canvas,
	CGameObject* object,
	CGameObject* newParent,
	CGameObject* insertNear,
	bool insertAfter,
	CGameLayer* newLayer)
	: m_canvas(canvas)
	, m_objectGuid(GuidOf(object))
	, m_newParentGuid(GuidOf(newParent))
	, m_insertNearGuid(GuidOf(insertNear))
	, m_newLayerGuid(newLayer ? newLayer->GetInstanceGuid() : File::Guid())
	, m_insertAfter(insertAfter)
{
	if (object)
	{
		m_oldParentGuid = GuidOf(object->GetParent().TryGet());
		m_oldLayerGuid = GuidOf(object->GetLayer().TryGet());
		m_oldLocalTransform = object->GetTransform();
	}
	CaptureOrders(m_oldOrders);
}

const char* CMoveGameObjectInHierarchyCommand::GetName() const
{
	return "Move GameObject In Hierarchy";
}

bool CMoveGameObjectInHierarchyCommand::Execute()
{
	CGameObject* object = Resolve(m_canvas, m_objectGuid);
	if (nullptr == object || false == m_canvas.IsValid())
	{
		return false;
	}

	if (false == Apply(m_newParentGuid, m_newLayerGuid, nullptr, true))
	{
		return false;
	}

	RebuildOrder(*object);
	CaptureOrders(m_newOrders);
	m_executed = true;
	return true;
}

void CMoveGameObjectInHierarchyCommand::Undo()
{
	if (false == m_executed || false == m_canvas.IsValid())
	{
		return;
	}

	Apply(m_oldParentGuid, m_oldLayerGuid, &m_oldLocalTransform, false);
	RestoreOrders(m_oldOrders);
	m_executed = false;
}

void CMoveGameObjectInHierarchyCommand::Redo()
{
	if (m_executed || false == m_canvas.IsValid())
	{
		return;
	}

	Apply(m_newParentGuid, m_newLayerGuid, &m_newLocalTransform, false);
	RestoreOrders(m_newOrders);
	m_executed = true;
}

bool CMoveGameObjectInHierarchyCommand::Apply(
	const File::Guid& parentGuid,
	const File::Guid& layerGuid,
	const Transform2D* localTransform,
	bool computeWorldStay)
{
	CGameObject* object = Resolve(m_canvas, m_objectGuid);
	if (nullptr == object || false == m_canvas.IsValid())
	{
		return false;
	}

	CGameCanvas& canvas = *m_canvas;
	Transform2D targetLocalTransform = localTransform ? *localTransform : object->GetTransform();
	if (computeWorldStay)
	{
		const Matrix3x2 objectWorld = GetWorldTransform(*object);
		CGameObject* parent = parentGuid.IsNull() ? nullptr : Resolve(m_canvas, parentGuid);
		const Matrix3x2 parentWorld = parent ? GetWorldTransform(*parent) : Matrix3x2::Identity();
		Matrix3x2 local = objectWorld;
		if (parent)
		{
			Matrix3x2 parentInv;
			if (parentWorld.TryInvert(parentInv))
			{
				local = objectWorld * parentInv;
			}
		}
		targetLocalTransform = DecomposeMatrix(local);
		m_newLocalTransform = targetLocalTransform;
	}

	if (false == ApplyParent(canvas, *object, parentGuid))
	{
		return false;
	}
	// 레이어는 부모 배정 뒤에 — SetParent 가 서브트리에 부모 레이어를 전파하므로 순서가 뒤집히면
	// 지정 레이어가 덮어써진다. 루트일 때만 유효(자식은 부모 레이어를 따른다).
	if (parentGuid.IsNull() && false == layerGuid.IsNull())
	{
		if (CGameLayer* layer = canvas.FindLayerByInstanceGuid(layerGuid).TryGet())
		{
			canvas.MoveObjectToLayer(*object, *layer);
		}
	}
	object->GetTransform() = targetLocalTransform;
	return true;
}

void CMoveGameObjectInHierarchyCommand::CaptureOrders(std::vector<OrderSnapshot>& out) const
{
	out.clear();
	if (false == m_canvas.IsValid())
	{
		return;
	}

	m_canvas->ForEachObject([&out](CGameObject& object)
	{
		out.push_back({ object.GetInstanceGuid(), object.GetCreationOrder() });
	});
}

void CMoveGameObjectInHierarchyCommand::RestoreOrders(const std::vector<OrderSnapshot>& orders)
{
	if (false == m_canvas.IsValid())
	{
		return;
	}

	for (const OrderSnapshot& order : orders)
	{
		if (CGameObject* object = Resolve(m_canvas, order.ObjectGuid))
		{
			CCanvasRuntimeAccess::SetCreationOrder(*object, order.CreationOrder);
		}
	}
}

void CMoveGameObjectInHierarchyCommand::RebuildOrder(CGameObject& object)
{
	if (false == m_canvas.IsValid())
	{
		return;
	}

	std::vector<CGameObject*> ordered;
	m_canvas->ForEachObject([&ordered](CGameObject& canvasObject)
	{
		ordered.push_back(&canvasObject);
	});
	std::sort(ordered.begin(), ordered.end(), [](const CGameObject* lhs, const CGameObject* rhs)
	{
		return lhs->GetCreationOrder() < rhs->GetCreationOrder();
	});

	ordered.erase(std::remove(ordered.begin(), ordered.end(), &object), ordered.end());

	auto insertIt = ordered.end();
	if (CGameObject* nearObject = Resolve(m_canvas, m_insertNearGuid))
	{
		insertIt = std::find(ordered.begin(), ordered.end(), nearObject);
		if (insertIt != ordered.end() && m_insertAfter)
		{
			++insertIt;
		}
	}
	else
	{
		CGameObject* newParent = Resolve(m_canvas, m_newParentGuid);
		for (auto it = ordered.begin(); it != ordered.end(); ++it)
		{
			CGameObject* candidateParent = (*it)->GetParent().TryGet();
			if (candidateParent == newParent)
			{
				insertIt = it + 1;
			}
		}
		if (insertIt == ordered.end() && newParent)
		{
			auto parentIt = std::find(ordered.begin(), ordered.end(), newParent);
			if (parentIt != ordered.end())
			{
				insertIt = parentIt + 1;
			}
		}
	}

	ordered.insert(insertIt, &object);
	for (std::size_t i = 0; i < ordered.size(); ++i)
	{
		CCanvasRuntimeAccess::SetCreationOrder(*ordered[i], static_cast<std::uint64_t>(i));
	}
}

// ── CModifyPolygonVerticesCommand ─────────────────────────────────────────────

CModifyPolygonVerticesCommand::CModifyPolygonVerticesCommand(
	SafePtr<CGameCanvas>             canvas,
	CGameObject*                object,
	std::vector<Vector2> newPoints)
	: m_canvas(canvas)
	, m_objectGuid(GuidOf(object))
	, m_newPoints(std::move(newPoints))
{
	// 현재 상태 스냅샷 (Undo 복원용).
	if (object)
	{
		if (const PolygonCollider2D* poly = object->GetComponent<PolygonCollider2D>())
		{
			m_oldPoints = poly->LocalPoints;
			if (m_oldPoints.empty())
			{
				poly->BuildLocalPoints(m_oldPoints);
			}
		}
	}
}

const char* CModifyPolygonVerticesCommand::GetName() const
{
	return "Modify Polygon Vertices";
}

bool CModifyPolygonVerticesCommand::Execute()
{
	m_executed = Apply(m_newPoints);
	return m_executed;
}

void CModifyPolygonVerticesCommand::Undo()
{
	if (m_executed)
		Apply(m_oldPoints);
}

void CModifyPolygonVerticesCommand::Redo()
{
	if (!m_executed)
		m_executed = Apply(m_newPoints);
}

bool CModifyPolygonVerticesCommand::Apply(
	const std::vector<Vector2>& points)
{
	CGameObject* object = Resolve(m_canvas, m_objectGuid);
	if (nullptr == object)
		return false;

	PolygonCollider2D* poly = object->GetComponent<PolygonCollider2D>();
	if (!poly)
		return false;

	poly->LocalPoints = points;
	poly->MarkProceduralBuilt();
	poly->m_convexDirty = true;
	return true;
}

#endif
