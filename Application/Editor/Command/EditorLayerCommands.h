#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/Command/EditorCommandManager.h"
#include "Engine/GameFramework/Scene/GameLayer.h"
#include "Utillity/File/FilePath.h" // File::Guid (레이어 안정 식별자 = InstanceGuid)

#include <string>
#include <vector>

class CGameScene;

// 오브젝트 커맨드(EditorSceneCommands)와 같은 규칙: 레이어를 InstanceGuid 로 보관하고
// 조작 시점에 활성 씬에서 다시 해석한다. 파괴→undo→redo 로 주소가 바뀌어도 안전하다.

// 레이어 편집 대상 속성 묶음. 필드가 7개뿐이고 서로 독립이라 필드별 커맨드를 만들지 않고
// 한 벌을 통째로 캡처/복원한다(undo 는 값 비교가 아니라 스냅샷 교체).
struct LayerPropertySnapshot
{
	std::string     Name;
	ELayerBlendMode BlendMode = ELayerBlendMode::Normal;
	float           Opacity = 1.0f;
	bool            Visible = true;
	bool            Static = false;
	bool            ForceOwnTexture = false;
	float           ParallaxFactor = 1.0f;
	bool            KeepOnCanvasChange = false;

	static LayerPropertySnapshot Capture(const CGameLayer& layer);
	void                         ApplyTo(CGameLayer& layer) const;
};

// 레이어 속성 편집(이름/블렌드/Opacity/가시성/Static/ForceOwnTexture/Parallax).
// fieldId 는 병합 판정에만 쓴다 — 같은 레이어의 같은 필드를 연속 편집(슬라이더 드래그)하면
// 한 undo 단위로 묶고, 다른 필드로 넘어가면 새 커맨드가 된다.
class CSetLayerPropertyCommand final : public IEditorCommand
{
public:
	enum class EField : std::uint8_t
	{
		Name,
		BlendMode,
		Opacity,
		Visible,
		Static,
		ForceOwnTexture,
		ParallaxFactor,
		KeepOnCanvasChange,
	};

	CSetLayerPropertyCommand(
		SafePtr<CGameScene> scene,
		CGameLayer* layer,
		EField field,
		LayerPropertySnapshot oldProperties,
		LayerPropertySnapshot newProperties);
	~CSetLayerPropertyCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;
	bool TryMerge(const IEditorCommand& newer) override;

private:
	bool Apply(const LayerPropertySnapshot& properties);

private:
	SafePtr<CGameScene>   m_scene;
	File::Guid            m_layerGuid;
	EField                m_field = EField::Name;
	LayerPropertySnapshot m_oldProperties;
	LayerPropertySnapshot m_newProperties;
};

// 레이어 생성(맨 위 = 목록 끝). Undo 는 빈 레이어 파괴.
class CCreateLayerCommand final : public IEditorCommand
{
public:
	CCreateLayerCommand(SafePtr<CGameScene> scene, const char* name);
	~CCreateLayerCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;
	// 생성된 레이어(없으면 nullptr). 호출자가 선택 처리에 사용.
	CGameLayer* GetLayer() const;

private:
	SafePtr<CGameScene> m_scene;
	std::string m_name;
	File::Guid  m_layerGuid; // 생성된 레이어의 안정 식별자(redo 시 동일 guid 강제)
	bool        m_created = false;
};

// 레이어 삭제 = 소속 오브젝트 서브트리 전부 삭제. Undo 는 레이어 재생성(guid·순서·속성 복원)
// 후 각 루트를 직렬화 스냅샷으로 되살려 그 레이어에 재배치.
class CDeleteLayerCommand final : public IEditorCommand
{
public:
	CDeleteLayerCommand(SafePtr<CGameScene> scene, CGameLayer* layer);
	~CDeleteLayerCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;

private:
	struct RootEntry
	{
		File::Guid  ObjectGuid;
		std::string Snapshot;
	};

	SafePtr<CGameScene>   m_scene;
	File::Guid            m_layerGuid;
	LayerPropertySnapshot m_properties;
	std::size_t           m_index = 0;   // 삭제 전 컴포짓 순서(undo 복원 위치)
	std::vector<RootEntry> m_roots;      // 소속 루트 서브트리(생성 순서대로)
	bool                  m_deleted = false;
};

// `.jlayer` 에셋을 캔버스에 레이어로 추가한다(레이어 뷰에 에셋 드롭).
// 오브젝트/레이어 guid 는 파일 값 그대로 복원되므로, 같은 에셋을 이미 로드한 캔버스에는
// 들어가지 않는다(LayerSerializer 가 DuplicateInstance 로 거부 → 커맨드도 실패).
class CAddLayerFromAssetCommand final : public IEditorCommand
{
public:
	CAddLayerFromAssetCommand(SafePtr<CGameScene> scene, const File::Guid& assetGuid);
	~CAddLayerFromAssetCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;
	// 추가된 레이어(없으면 nullptr). 호출자가 선택 처리에 사용.
	CGameLayer* GetLayer() const;

private:
	SafePtr<CGameScene> m_scene;
	File::Guid  m_assetGuid;
	File::Guid  m_layerGuid;  // 파일에서 온 레이어 guid(undo 후 redo 가 같은 레이어를 지목)
	bool        m_created = false;
};

namespace EditorLayerActions
{
	// 레이어 속성 편집을 undo 스택에 올리는 공용 진입점(하이어라키 눈 아이콘 / 인스펙터 패널).
	// 호출자는 Capture 로 뜬 스냅샷의 필드 하나만 바꿔서 넘긴다.
	bool SetLayerProperty(CGameScene& scene, CGameLayer& layer,
	                      CSetLayerPropertyCommand::EField field,
	                      const LayerPropertySnapshot& newProperties);
}

// 레이어 컴포짓 순서 변경(드래그 재배치).
class CMoveLayerCommand final : public IEditorCommand
{
public:
	CMoveLayerCommand(SafePtr<CGameScene> scene, CGameLayer* layer, std::size_t newIndex);
	~CMoveLayerCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;

private:
	SafePtr<CGameScene> m_scene;
	File::Guid  m_layerGuid;
	std::size_t m_oldIndex = 0;
	std::size_t m_newIndex = 0;
	bool        m_executed = false;
};

#endif
