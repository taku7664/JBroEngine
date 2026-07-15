#include "pch.h"
#include "EditorLayerCommands.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/Editor.h"
#include "Engine/GameFramework/Object/GameObject.h"
#include "Engine/GameFramework/Prefab/PrefabSerializer.h"
#include "Engine/GameFramework/Scene/Scene.h"
#include "Engine/GameFramework/Scene/SceneRuntimeAccess.h"

#include <algorithm>
#include <utility>

namespace
{
	CGameLayer* ResolveLayer(const SafePtr<CGameScene>& scene, const File::Guid& guid)
	{
		return scene.IsValid() ? scene->FindLayerByInstanceGuid(guid).TryGet() : nullptr;
	}

	File::Guid GuidOfLayer(const CGameLayer* layer)
	{
		return layer ? layer->GetInstanceGuid() : File::Guid();
	}
}

// ── LayerPropertySnapshot ────────────────────────────────────────────────────

LayerPropertySnapshot LayerPropertySnapshot::Capture(const CGameLayer& layer)
{
	LayerPropertySnapshot snapshot;
	snapshot.Name = layer.Name;
	snapshot.BlendMode = layer.BlendMode;
	snapshot.Opacity = layer.Opacity;
	snapshot.Visible = layer.Visible;
	snapshot.Static = layer.Static;
	snapshot.ForceOwnTexture = layer.ForceOwnTexture;
	snapshot.ParallaxFactor = layer.ParallaxFactor;
	return snapshot;
}

void LayerPropertySnapshot::ApplyTo(CGameLayer& layer) const
{
	layer.Name = Name;
	layer.BlendMode = BlendMode;
	layer.Opacity = Opacity;
	layer.Visible = Visible;
	layer.Static = Static;
	layer.ForceOwnTexture = ForceOwnTexture;
	layer.ParallaxFactor = ParallaxFactor;
}

// ── CSetLayerPropertyCommand ─────────────────────────────────────────────────

CSetLayerPropertyCommand::CSetLayerPropertyCommand(
	SafePtr<CGameScene> scene,
	CGameLayer* layer,
	EField field,
	LayerPropertySnapshot oldProperties,
	LayerPropertySnapshot newProperties)
	: m_scene(scene)
	, m_layerGuid(GuidOfLayer(layer))
	, m_field(field)
	, m_oldProperties(std::move(oldProperties))
	, m_newProperties(std::move(newProperties))
{
}

const char* CSetLayerPropertyCommand::GetName() const
{
	return "Set Layer Property";
}

bool CSetLayerPropertyCommand::Execute()
{
	return Apply(m_newProperties);
}

void CSetLayerPropertyCommand::Undo()
{
	Apply(m_oldProperties);
}

void CSetLayerPropertyCommand::Redo()
{
	Apply(m_newProperties);
}

bool CSetLayerPropertyCommand::TryMerge(const IEditorCommand& newer)
{
	const CSetLayerPropertyCommand* other = dynamic_cast<const CSetLayerPropertyCommand*>(&newer);
	if (nullptr == other)
	{
		return false;
	}
	if (m_layerGuid != other->m_layerGuid || m_field != other->m_field)
	{
		return false;
	}
	// old 는 드래그 시작값 유지, new 만 최신값으로 교체 → undo 1개로 시작↔끝 복원.
	m_newProperties = other->m_newProperties;
	return true;
}

bool CSetLayerPropertyCommand::Apply(const LayerPropertySnapshot& properties)
{
	CGameLayer* layer = ResolveLayer(m_scene, m_layerGuid);
	if (nullptr == layer)
	{
		return false;
	}
	properties.ApplyTo(*layer);
	return true;
}

bool EditorLayerActions::SetLayerProperty(
	CGameScene& scene,
	CGameLayer& layer,
	CSetLayerPropertyCommand::EField field,
	const LayerPropertySnapshot& newProperties)
{
	auto command = MakeOwnerPtr<CSetLayerPropertyCommand>(
		scene.SafeFromThis(), &layer, field, LayerPropertySnapshot::Capture(layer), newProperties);
	return Editor::CommandManager.ExecuteCommand(std::move(command));
}

// ── CCreateLayerCommand ──────────────────────────────────────────────────────

CCreateLayerCommand::CCreateLayerCommand(SafePtr<CGameScene> scene, const char* name)
	: m_scene(scene)
	, m_name(name ? name : "")
{
}

const char* CCreateLayerCommand::GetName() const
{
	return "Create Layer";
}

bool CCreateLayerCommand::Execute()
{
	if (false == m_scene.IsValid())
	{
		return false;
	}

	CGameLayer* layer = m_scene->CreateLayer(m_name.empty() ? nullptr : m_name.c_str());
	if (nullptr == layer)
	{
		return false;
	}

	if (m_layerGuid.IsNull())
	{
		// 최초 실행 — 생성된 guid 를 기억해 redo 가 같은 레이어를 재현하게 한다.
		m_layerGuid = layer->GetInstanceGuid();
		m_name = layer->Name; // 자동 이름("Layer N")도 redo 시 동일하게.
	}
	else
	{
		CSceneRuntimeAccess::SetLayerInstanceGuid(*m_scene, *layer, m_layerGuid);
	}

	m_created = true;
	return true;
}

void CCreateLayerCommand::Undo()
{
	if (false == m_created || false == m_scene.IsValid())
	{
		return;
	}
	if (CGameLayer* layer = ResolveLayer(m_scene, m_layerGuid))
	{
		m_scene->DestroyLayer(layer);
	}
	m_created = false;
}

void CCreateLayerCommand::Redo()
{
	if (false == m_created)
	{
		Execute();
	}
}

CGameLayer* CCreateLayerCommand::GetLayer() const
{
	return ResolveLayer(m_scene, m_layerGuid);
}

// ── CDeleteLayerCommand ──────────────────────────────────────────────────────

CDeleteLayerCommand::CDeleteLayerCommand(SafePtr<CGameScene> scene, CGameLayer* layer)
	: m_scene(scene)
	, m_layerGuid(GuidOfLayer(layer))
{
	if (nullptr == layer || false == m_scene.IsValid())
	{
		return;
	}

	m_properties = LayerPropertySnapshot::Capture(*layer);
	const int index = m_scene->GetLayerIndex(layer);
	m_index = index > 0 ? static_cast<std::size_t>(index) : 0;

	// 소속 루트만 직렬화한다 — 자식은 서브트리 스냅샷에 포함된다(자식=부모 레이어 불변식).
	std::vector<CGameObject*> roots;
	m_scene->ForEachObject([layer, &roots](CGameObject& object)
	{
		if (object.GetLayer().TryGet() == layer && false == object.GetParent().IsValid())
		{
			roots.push_back(&object);
		}
	});
	// 복원 순서를 결정적으로: 생성 순서대로 되살려야 하이어라키 표시 순서가 유지된다.
	std::sort(roots.begin(), roots.end(), [](const CGameObject* lhs, const CGameObject* rhs)
	{
		return lhs->GetCreationOrder() < rhs->GetCreationOrder();
	});

	CPrefabSerializer serializer;
	m_roots.reserve(roots.size());
	for (CGameObject* root : roots)
	{
		RootEntry entry;
		entry.ObjectGuid = root->GetInstanceGuid();
		serializer.SerializePrefabToText(*m_scene, root, entry.Snapshot);
		if (false == entry.Snapshot.empty())
		{
			m_roots.push_back(std::move(entry));
		}
	}
}

const char* CDeleteLayerCommand::GetName() const
{
	return "Delete Layer";
}

bool CDeleteLayerCommand::Execute()
{
	CGameLayer* layer = ResolveLayer(m_scene, m_layerGuid);
	if (nullptr == layer)
	{
		return false;
	}
	// 마지막 남은 레이어면 씬이 거부한다("레이어 0개" 불허).
	m_deleted = m_scene->DestroyLayer(layer);
	return m_deleted;
}

void CDeleteLayerCommand::Undo()
{
	if (false == m_deleted || false == m_scene.IsValid())
	{
		return;
	}

	CGameLayer* layer = m_scene->CreateLayer(m_properties.Name.c_str());
	if (nullptr == layer)
	{
		return;
	}
	// guid 복원이 먼저 — 뷰포트 LayerFilter 가 이 guid 로 레이어를 찾는다.
	CSceneRuntimeAccess::SetLayerInstanceGuid(*m_scene, *layer, m_layerGuid);
	m_properties.ApplyTo(*layer);
	m_scene->MoveLayer(layer, m_index);

	// 오브젝트 복원: 직렬화가 InstanceGuid 를 보존하므로 Ref/뷰포트 카메라 참조가 되살아난다.
	CPrefabSerializer serializer;
	for (const RootEntry& entry : m_roots)
	{
		CGameObject* root = nullptr;
		if (EPrefabSerializeResult::Success !=
		    serializer.DeserializePrefabFromText(*m_scene, entry.Snapshot.c_str(), &root))
		{
			continue;
		}
		// 프리팹 역직렬화는 기본 레이어에 만든다 — 원래 레이어로 되돌린다.
		if (root)
		{
			m_scene->MoveObjectToLayer(*root, *layer);
		}
	}

	m_deleted = false;
}

void CDeleteLayerCommand::Redo()
{
	if (false == m_deleted)
	{
		Execute();
	}
}

// ── CMoveLayerCommand ────────────────────────────────────────────────────────

CMoveLayerCommand::CMoveLayerCommand(SafePtr<CGameScene> scene, CGameLayer* layer, std::size_t newIndex)
	: m_scene(scene)
	, m_layerGuid(GuidOfLayer(layer))
	, m_newIndex(newIndex)
{
	if (layer && m_scene.IsValid())
	{
		const int index = m_scene->GetLayerIndex(layer);
		m_oldIndex = index > 0 ? static_cast<std::size_t>(index) : 0;
	}
}

const char* CMoveLayerCommand::GetName() const
{
	return "Move Layer";
}

bool CMoveLayerCommand::Execute()
{
	CGameLayer* layer = ResolveLayer(m_scene, m_layerGuid);
	if (nullptr == layer)
	{
		return false;
	}
	m_executed = m_scene->MoveLayer(layer, m_newIndex);
	return m_executed;
}

void CMoveLayerCommand::Undo()
{
	if (false == m_executed)
	{
		return;
	}
	if (CGameLayer* layer = ResolveLayer(m_scene, m_layerGuid))
	{
		m_scene->MoveLayer(layer, m_oldIndex);
	}
	m_executed = false;
}

void CMoveLayerCommand::Redo()
{
	if (false == m_executed)
	{
		Execute();
	}
}

#endif
