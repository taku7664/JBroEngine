#include "pch.h"
#include "EditorLayerCommands.h"

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/Editor.h"
#include "Editor/EditorContext.h"
#include "Editor/Gui/EditorMessagePopup.h"
#include "Engine/Core/Asset/IAssetManager.h"
#include "Engine/Core/Asset/IAssetRegistry.h"
#include "Engine/Editor/Project/ProjectManager.h"
#include "Engine/GameFramework/Object/GameObject.h"
#include "Engine/GameFramework/Prefab/PrefabSerializer.h"
#include "Engine/GameFramework/Canvas/Canvas.h"
#include "Engine/GameFramework/Canvas/CanvasRuntimeAccess.h"
#include "Engine/GameFramework/Serialization/LayerSerializer.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>

namespace
{
	CGameLayer* ResolveLayer(const SafePtr<CGameCanvas>& scene, const File::Guid& guid)
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
	snapshot.KeepOnCanvasChange = layer.KeepOnCanvasChange;
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
	layer.KeepOnCanvasChange = KeepOnCanvasChange;
}

// ── CSetLayerPropertyCommand ─────────────────────────────────────────────────

CSetLayerPropertyCommand::CSetLayerPropertyCommand(
	SafePtr<CGameCanvas> scene,
	CGameLayer* layer,
	EField field,
	LayerPropertySnapshot oldProperties,
	LayerPropertySnapshot newProperties)
	: m_canvas(scene)
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
	CGameLayer* layer = ResolveLayer(m_canvas, m_layerGuid);
	if (nullptr == layer)
	{
		return false;
	}
	properties.ApplyTo(*layer);
	return true;
}

bool EditorLayerActions::SetLayerProperty(
	CGameCanvas& scene,
	CGameLayer& layer,
	CSetLayerPropertyCommand::EField field,
	const LayerPropertySnapshot& newProperties)
{
	auto command = MakeOwnerPtr<CSetLayerPropertyCommand>(
		scene.SafeFromThis(), &layer, field, LayerPropertySnapshot::Capture(layer), newProperties);
	return Editor::CommandManager.ExecuteCommand(std::move(command));
}

// ── CCreateLayerCommand ──────────────────────────────────────────────────────

CCreateLayerCommand::CCreateLayerCommand(SafePtr<CGameCanvas> scene, const char* name)
	: m_canvas(scene)
	, m_name(name ? name : "")
{
}

const char* CCreateLayerCommand::GetName() const
{
	return "Create Layer";
}

bool CCreateLayerCommand::Execute()
{
	if (false == m_canvas.IsValid())
	{
		return false;
	}

	CGameLayer* layer = m_canvas->CreateLayer(m_name.empty() ? nullptr : m_name.c_str());
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
		CCanvasRuntimeAccess::SetLayerInstanceGuid(*m_canvas, *layer, m_layerGuid);
	}

	m_created = true;
	return true;
}

void CCreateLayerCommand::Undo()
{
	if (false == m_created || false == m_canvas.IsValid())
	{
		return;
	}
	if (CGameLayer* layer = ResolveLayer(m_canvas, m_layerGuid))
	{
		m_canvas->DestroyLayer(layer);
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
	return ResolveLayer(m_canvas, m_layerGuid);
}

// ── CDeleteLayerCommand ──────────────────────────────────────────────────────

CDeleteLayerCommand::CDeleteLayerCommand(SafePtr<CGameCanvas> scene, CGameLayer* layer)
	: m_canvas(scene)
	, m_layerGuid(GuidOfLayer(layer))
{
	if (nullptr == layer || false == m_canvas.IsValid())
	{
		return;
	}

	m_properties = LayerPropertySnapshot::Capture(*layer);
	const int index = m_canvas->GetLayerIndex(layer);
	m_index = index > 0 ? static_cast<std::size_t>(index) : 0;

	// 소속 루트만 직렬화한다 — 자식은 서브트리 스냅샷에 포함된다(자식=부모 레이어 불변식).
	std::vector<CGameObject*> roots;
	m_canvas->ForEachObject([layer, &roots](CGameObject& object)
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
		serializer.SerializePrefabToText(*m_canvas, root, entry.Snapshot);
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
	CGameLayer* layer = ResolveLayer(m_canvas, m_layerGuid);
	if (nullptr == layer)
	{
		return false;
	}
	// 마지막 남은 레이어면 씬이 거부한다("레이어 0개" 불허).
	m_deleted = m_canvas->DestroyLayer(layer);
	return m_deleted;
}

void CDeleteLayerCommand::Undo()
{
	if (false == m_deleted || false == m_canvas.IsValid())
	{
		return;
	}

	CGameLayer* layer = m_canvas->CreateLayer(m_properties.Name.c_str());
	if (nullptr == layer)
	{
		return;
	}
	// guid 복원이 먼저 — 뷰포트 LayerFilter 가 이 guid 로 레이어를 찾는다.
	CCanvasRuntimeAccess::SetLayerInstanceGuid(*m_canvas, *layer, m_layerGuid);
	m_properties.ApplyTo(*layer);
	m_canvas->MoveLayer(layer, m_index);

	// 오브젝트 복원: 직렬화가 InstanceGuid 를 보존하므로 Ref/뷰포트 카메라 참조가 되살아난다.
	CPrefabSerializer serializer;
	for (const RootEntry& entry : m_roots)
	{
		CGameObject* root = nullptr;
		if (EPrefabSerializeResult::Success !=
		    serializer.DeserializePrefabFromText(*m_canvas, entry.Snapshot.c_str(), &root))
		{
			continue;
		}
		// 프리팹 역직렬화는 기본 레이어에 만든다 — 원래 레이어로 되돌린다.
		if (root)
		{
			m_canvas->MoveObjectToLayer(*root, *layer);
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

// ── CAddLayerFromAssetCommand ────────────────────────────────────────────────

CAddLayerFromAssetCommand::CAddLayerFromAssetCommand(SafePtr<CGameCanvas> scene, const File::Guid& assetGuid)
	: m_canvas(scene)
	, m_assetGuid(assetGuid)
{
}

const char* CAddLayerFromAssetCommand::GetName() const
{
	return "Add Layer From Asset";
}

bool CAddLayerFromAssetCommand::Execute()
{
	if (false == m_canvas.IsValid() || m_assetGuid.IsNull())
	{
		return false;
	}

	SafePtr<CProjectManager> projectManager = EditorContext::GetProjectManager();
	SafePtr<IAssetManager> assetManager = EditorContext::GetAssetManager();
	if (false == projectManager.IsValid() || false == assetManager.IsValid())
	{
		return false;
	}

	AssetMetaData metaData;
	if (false == assetManager->GetRegistry().TryGetAsset(m_assetGuid, metaData))
	{
		return false;
	}

	// 경로는 Execute 마다 다시 푼다 — 에셋이 이동/리네임돼도 guid 로 따라간다.
	const File::Path absolutePath(projectManager->GetAssetPath() / metaData.Path);
	std::ifstream file(absolutePath);
	if (false == file.is_open())
	{
		EditorMessagePopup::ShowInfo(
			Loc::Text(EditorLocKeys::LayerAssetLoadFailedTitle),
			Loc::Text(EditorLocKeys::LayerAssetLoadFailedMessage));
		return false;
	}
	std::stringstream buffer;
	buffer << file.rdbuf();

	CGameLayer* layer = nullptr;
	const ELayerSerializeResult result =
		Serialization::DeserializeLayer(*m_canvas, buffer.str().c_str(), &layer);
	if (ELayerSerializeResult::DuplicateInstance == result)
	{
		// 같은 레이어 파일을 한 캔버스에 두 번 넣으려는 경우 — guid 가 겹쳐 조용히 재발급되면
		// 승계/Ref 가 어긋나므로 아예 막는다. 사용자가 놓치지 않게 로그가 아니라 모달로.
		EditorMessagePopup::ShowInfo(
			Loc::Text(EditorLocKeys::LayerAssetDuplicateTitle),
			Loc::Text(EditorLocKeys::LayerAssetDuplicateMessage));
		return false;
	}
	if (ELayerSerializeResult::Success != result || nullptr == layer)
	{
		EditorMessagePopup::ShowInfo(
			Loc::Text(EditorLocKeys::LayerAssetLoadFailedTitle),
			Loc::Text(EditorLocKeys::LayerAssetLoadFailedMessage));
		return false;
	}

	layer->SourceAssetGuid = m_assetGuid;
	m_layerGuid = layer->GetInstanceGuid();
	m_created = true;
	return true;
}

void CAddLayerFromAssetCommand::Undo()
{
	if (false == m_created || false == m_canvas.IsValid())
	{
		return;
	}
	if (CGameLayer* layer = ResolveLayer(m_canvas, m_layerGuid))
	{
		m_canvas->DestroyLayer(layer);
	}
	m_created = false;
}

void CAddLayerFromAssetCommand::Redo()
{
	if (false == m_created)
	{
		// undo 가 레이어와 그 오브젝트를 파괴했으므로 guid 충돌 없이 파일 그대로 되살아난다.
		Execute();
	}
}

CGameLayer* CAddLayerFromAssetCommand::GetLayer() const
{
	return ResolveLayer(m_canvas, m_layerGuid);
}

// ── CMoveLayerCommand ────────────────────────────────────────────────────────

CMoveLayerCommand::CMoveLayerCommand(SafePtr<CGameCanvas> scene, CGameLayer* layer, std::size_t newIndex)
	: m_canvas(scene)
	, m_layerGuid(GuidOfLayer(layer))
	, m_newIndex(newIndex)
{
	if (layer && m_canvas.IsValid())
	{
		const int index = m_canvas->GetLayerIndex(layer);
		m_oldIndex = index > 0 ? static_cast<std::size_t>(index) : 0;
	}
}

const char* CMoveLayerCommand::GetName() const
{
	return "Move Layer";
}

bool CMoveLayerCommand::Execute()
{
	CGameLayer* layer = ResolveLayer(m_canvas, m_layerGuid);
	if (nullptr == layer)
	{
		return false;
	}
	m_executed = m_canvas->MoveLayer(layer, m_newIndex);
	return m_executed;
}

void CMoveLayerCommand::Undo()
{
	if (false == m_executed)
	{
		return;
	}
	if (CGameLayer* layer = ResolveLayer(m_canvas, m_layerGuid))
	{
		m_canvas->MoveLayer(layer, m_oldIndex);
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
