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
#include "Engine/GameFramework/Canvas/CanvasViewProjection.h"
#include "Engine/GameFramework/Component/Camera2D.h"
#include "Engine/GameFramework/Serialization/LayerSerializer.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <utility>

namespace
{
	CGameLayer* ResolveLayer(const SafePtr<CGameCanvas>& canvas, const File::Guid& guid)
	{
		return canvas.IsValid() ? canvas->FindLayerByInstanceGuid(guid).TryGet() : nullptr;
	}

	File::Guid GuidOfLayer(const CGameLayer* layer)
	{
		return layer ? layer->GetInstanceGuid() : File::Guid();
	}

	// ── 스페이스 전환의 위치 보존 변환 ────────────────────────────────────────
	//
	// 월드 레이어는 카메라 뷰로, 화면 레이어는 원점 고정 뷰로 그려진다. 같은 NDC 에 떨어지게
	// 하려면 두 뷰 사이의 닮음변환을 트랜스폼에 구워 넣어야 한다.
	//
	//   k = 화면공간 반높이 / 카메라 반높이
	//   Position_scr = k · R(−camRot) · (Position_wld − camPos) − 앵커점
	//   Rotation_scr = Rotation_wld − camRot
	//   Scale_scr    = Scale_wld · k
	//
	// **Scale 을 빼먹으면 안 된다.** Camera2D::OrthographicSize 기본값이 10 이고 화면공간
	// 반높이는 1080/100/2 = 5.4 라, 위치만 옮기면 크기가 거의 2배로 튄다.
	//
	// k 가 균일한 이유: 두 공간의 종횡비가 같다(둘 다 뷰포트 렉트 기준). 그래서 비균등
	// 스케일이 생기지 않는다.
	//
	// **앵커점을 빼야 왕복이 맞는다.** 화면 공간에서 Position 은 "앵커점으로부터의 오프셋"
	// 이므로, 안 빼면 World→Screen→World 가 앵커점만큼 어긋난다.
	struct SpaceConversion
	{
		bool    Valid = false;
		float   Scale = 1.0f;     // k (월드→화면 방향)
		float   CamCos = 1.0f;
		float   CamSin = 0.0f;
		Vector2 CamPos = Vector2(0.0f, 0.0f);
	};

	// 이 레이어를 그리는 첫 뷰포트의 카메라를 기준으로 잡는다 — 그림자 패스가 primaryCamera 로
	// viewports.front() 를 쓰는 것과 같은 규약이다. 카메라가 없으면 Valid=false 로 두고
	// 호출자가 변환을 포기한다(항등으로 두면 위치가 통째로 튄다).
	SpaceConversion BuildSpaceConversion(CGameCanvas& canvas, const CGameLayer& layer, EScreenScaleMode screenScaleMode)
	{
		SpaceConversion conversion;

		const float renderWidth = canvas.GetLastRenderWidth();
		const float renderHeight = canvas.GetLastRenderHeight();
		if (renderWidth < 1.0f || renderHeight < 1.0f)
		{
			return conversion;   // 아직 렌더된 적이 없다 — 기준 뷰가 없다.
		}

		CanvasViewProjection cameraView;
		if (false == canvas.TryComputeLayerCameraView(layer, cameraView))
		{
			return conversion;
		}

		const ScreenSpaceReference reference = ScreenSpaceReference::FromResolution(
			canvas.GetScreenReferenceWidth(), canvas.GetScreenReferenceHeight(), canvas.GetScreenPixelsPerUnit());
		float screenHalfWidth = 0.0f;
		float screenHalfHeight = 0.0f;
		ComputeScreenSpaceExtents(screenScaleMode, reference,
			cameraView.RectPixelW, cameraView.RectPixelH, screenHalfWidth, screenHalfHeight);

		if (cameraView.OrthoSize <= 0.0001f)
		{
			return conversion;
		}

		conversion.Valid = true;
		conversion.Scale = screenHalfHeight / cameraView.OrthoSize;
		conversion.CamCos = cameraView.CosR;
		conversion.CamSin = cameraView.SinR;
		conversion.CamPos = Vector2(cameraView.PosX, cameraView.PosY);
		return conversion;
	}

	Transform2D ConvertWorldToScreen(const Transform2D& source, const SpaceConversion& conversion, const Vector2& anchorPoint)
	{
		const float dx = source.Position.x - conversion.CamPos.x;
		const float dy = source.Position.y - conversion.CamPos.y;
		// R(−camRot) 적용 — 카메라 로컬로 되돌린다.
		const float localX =  conversion.CamCos * dx + conversion.CamSin * dy;
		const float localY = -conversion.CamSin * dx + conversion.CamCos * dy;

		Transform2D result = source;
		result.Position = Vector2(localX * conversion.Scale - anchorPoint.x,
		                          localY * conversion.Scale - anchorPoint.y);
		result.RotationRadians = Radian(source.RotationRadians.Value - std::atan2(conversion.CamSin, conversion.CamCos));
		result.Scale = Vector2(source.Scale.x * conversion.Scale, source.Scale.y * conversion.Scale);
		return result;
	}

	Transform2D ConvertScreenToWorld(const Transform2D& source, const SpaceConversion& conversion, const Vector2& anchorPoint)
	{
		const float inverseScale = 1.0f / conversion.Scale;
		const float localX = (source.Position.x + anchorPoint.x) * inverseScale;
		const float localY = (source.Position.y + anchorPoint.y) * inverseScale;
		// R(+camRot) 적용 후 카메라 위치를 더한다.
		const float dx = conversion.CamCos * localX - conversion.CamSin * localY;
		const float dy = conversion.CamSin * localX + conversion.CamCos * localY;

		Transform2D result = source;
		result.Position = Vector2(conversion.CamPos.x + dx, conversion.CamPos.y + dy);
		result.RotationRadians = Radian(source.RotationRadians.Value + std::atan2(conversion.CamSin, conversion.CamCos));
		result.Scale = Vector2(source.Scale.x * inverseScale, source.Scale.y * inverseScale);
		return result;
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
	snapshot.ForceOwnTexture = layer.ForceOwnTexture;
	snapshot.ParallaxFactor = layer.ParallaxFactor;
	snapshot.KeepOnCanvasChange = layer.KeepOnCanvasChange;
	snapshot.Space = layer.Space;
	snapshot.ScaleMode = layer.ScaleMode;
	snapshot.AnchorToSafeArea = layer.AnchorToSafeArea;
	return snapshot;
}

void LayerPropertySnapshot::ApplyTo(CGameLayer& layer) const
{
	layer.Name = Name;
	layer.BlendMode = BlendMode;
	layer.Opacity = Opacity;
	layer.Visible = Visible;
	layer.ForceOwnTexture = ForceOwnTexture;
	layer.ParallaxFactor = ParallaxFactor;
	layer.KeepOnCanvasChange = KeepOnCanvasChange;
	layer.Space = Space;
	layer.ScaleMode = ScaleMode;
	layer.AnchorToSafeArea = AnchorToSafeArea;
}

// ── CSetLayerPropertyCommand ─────────────────────────────────────────────────

CSetLayerPropertyCommand::CSetLayerPropertyCommand(
	SafePtr<CGameCanvas> canvas,
	CGameLayer* layer,
	EField field,
	LayerPropertySnapshot oldProperties,
	LayerPropertySnapshot newProperties)
	: m_canvas(canvas)
	, m_layerGuid(GuidOfLayer(layer))
	, m_field(field)
	, m_oldProperties(std::move(oldProperties))
	, m_newProperties(std::move(newProperties))
{
	if (EField::Space == m_field && m_oldProperties.Space != m_newProperties.Space)
	{
		CaptureSpaceConversion();
	}
}

void CSetLayerPropertyCommand::CaptureSpaceConversion()
{
	CGameCanvas* canvas = m_canvas.TryGet();
	CGameLayer* layer = ResolveLayer(m_canvas, m_layerGuid);
	if (nullptr == canvas || nullptr == layer)
	{
		return;
	}

	const bool toScreen = (ELayerSpace::Screen == m_newProperties.Space);
	// 화면 쪽 스케일 모드는 어느 방향이든 "화면이었던/이 될" 스냅샷에서 읽는다.
	const EScreenScaleMode screenScaleMode = toScreen ? m_newProperties.ScaleMode : m_oldProperties.ScaleMode;

	const SpaceConversion conversion = BuildSpaceConversion(*canvas, *layer, screenScaleMode);
	if (false == conversion.Valid)
	{
		// 카메라가 없거나 아직 렌더 전 — 변환 기준이 없다. 트랜스폼은 건드리지 않고
		// 스페이스만 바꾼다(조용히 항등 변환을 적용하면 위치가 통째로 튄다).
		return;
	}

	// 앵커점은 **화면 상태의 레이어 설정**으로 계산해야 한다. GetRootAnchorOffset 은 레이어의
	// 현재 Space 를 보므로, 화면으로 가는 경우엔 먼저 새 설정을 적용해 두고 계산한다.
	const LayerPropertySnapshot restore = LayerPropertySnapshot::Capture(*layer);
	if (toScreen)
	{
		m_newProperties.ApplyTo(*layer);
	}

	canvas->ForEachObject([&](CGameObject& object)
	{
		// 루트만. 자식은 부모 기준 로컬이라 부모를 따라온다 — 자식까지 변환하면 이중 적용이다.
		if (object.GetParent().IsValid() || object.GetLayer().TryGet() != layer)
		{
			return;
		}

		const Transform2D source = object.GetTransform();
		const Vector2 anchorPoint = canvas->GetRootAnchorOffset(object);
		const Transform2D converted = toScreen
			? ConvertWorldToScreen(source, conversion, anchorPoint)
			: ConvertScreenToWorld(source, conversion, anchorPoint);

		m_convertedObjectGuids.push_back(object.GetInstanceGuid());
		m_oldTransforms.push_back(source);
		m_newTransforms.push_back(converted);
	});

	// 계산용으로 잠깐 바꿔 둔 레이어 설정을 되돌린다 — 실제 적용은 Execute 가 한다.
	restore.ApplyTo(*layer);
}

const char* CSetLayerPropertyCommand::GetName() const
{
	return "Set Layer Property";
}

bool CSetLayerPropertyCommand::Execute()
{
	return Apply(m_newProperties, true);
}

void CSetLayerPropertyCommand::Undo()
{
	Apply(m_oldProperties, false);
}

void CSetLayerPropertyCommand::Redo()
{
	Apply(m_newProperties, true);
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
	// 스페이스 전환은 병합하지 않는다 — 트랜스폼 변환 스냅샷이 각 전환마다 따로 계산돼서,
	// 속성만 최신으로 갈아끼우면 변환 결과와 짝이 안 맞는다. (드래그로 연속 편집되는 값도 아니다.)
	if (EField::Space == m_field)
	{
		return false;
	}
	// old 는 드래그 시작값 유지, new 만 최신값으로 교체 → undo 1개로 시작↔끝 복원.
	m_newProperties = other->m_newProperties;
	return true;
}

bool CSetLayerPropertyCommand::Apply(const LayerPropertySnapshot& properties, bool toNew)
{
	CGameCanvas* canvas = m_canvas.TryGet();
	CGameLayer* layer = ResolveLayer(m_canvas, m_layerGuid);
	if (nullptr == layer)
	{
		return false;
	}
	properties.ApplyTo(*layer);

	// 스페이스 전환에 딸린 트랜스폼 변환도 같은 undo 단위로 함께 되돌린다. 절대값 스냅샷이라
	// 순서·중복 적용에 무해하다(델타였다면 Undo→Redo 마다 누적됐다).
	if (nullptr != canvas && false == m_convertedObjectGuids.empty())
	{
		const std::vector<Transform2D>& transforms = toNew ? m_newTransforms : m_oldTransforms;
		for (std::size_t i = 0; i < m_convertedObjectGuids.size(); ++i)
		{
			if (CGameObject* object = canvas->FindByInstanceGuid(m_convertedObjectGuids[i]).TryGet())
			{
				object->GetTransform() = transforms[i];
			}
		}
	}
	return true;
}

bool EditorLayerActions::SetLayerProperty(
	CGameCanvas& canvas,
	CGameLayer& layer,
	CSetLayerPropertyCommand::EField field,
	const LayerPropertySnapshot& newProperties)
{
	auto command = MakeOwnerPtr<CSetLayerPropertyCommand>(
		canvas.SafeFromThis(), &layer, field, LayerPropertySnapshot::Capture(layer), newProperties);
	return Editor::CommandManager.ExecuteCommand(std::move(command));
}

// ── CCreateLayerCommand ──────────────────────────────────────────────────────

CCreateLayerCommand::CCreateLayerCommand(SafePtr<CGameCanvas> canvas, const char* name)
	: m_canvas(canvas)
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

CDeleteLayerCommand::CDeleteLayerCommand(SafePtr<CGameCanvas> canvas, CGameLayer* layer)
	: m_canvas(canvas)
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
	// 마지막 남은 레이어면 캔버스가 거부한다("레이어 0개" 불허).
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

CAddLayerFromAssetCommand::CAddLayerFromAssetCommand(SafePtr<CGameCanvas> canvas, const File::Guid& assetGuid)
	: m_canvas(canvas)
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

CMoveLayerCommand::CMoveLayerCommand(SafePtr<CGameCanvas> canvas, CGameLayer* layer, std::size_t newIndex)
	: m_canvas(canvas)
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
