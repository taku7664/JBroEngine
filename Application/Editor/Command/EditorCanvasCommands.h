#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/Command/EditorCommandManager.h"
#include "Engine/GameFramework/Canvas/CanvasViewport.h"
#include "Utillity/File/FilePath.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class CGameCanvas;

// 캔버스 속성(배경색)과 뷰포트 목록 편집 커맨드.
//
// 레이어 커맨드가 대상을 InstanceGuid 로 들고 있는 것과 달리 뷰포트는 인덱스로 지목한다.
// 뷰포트에는 guid 가 없지만, undo 스택이 LIFO 라서 어떤 커맨드를 undo 하는 시점의 목록은
// 그 커맨드가 Execute 를 마친 직후와 같다(뒤에 쌓인 삭제/삽입은 이미 되돌려진 상태다).
// 즉 저장한 인덱스는 그 시점에 항상 유효하다.

// 뷰포트 한 벌 통째 캡처. 레이어와 같은 이유로 필드별 커맨드를 만들지 않는다
// (undo = 값 비교가 아니라 스냅샷 교체).
struct ViewportSnapshot
{
	std::string             Name;
	File::Guid              CameraObjectGuid;
	Layout2D                Position = { Vector2(0.0f, 0.0f), Vector2(0.0f, 0.0f) };
	Layout2D                Size     = { Vector2(1.0f, 1.0f), Vector2(0.0f, 0.0f) };
	std::vector<File::Guid> LayerFilter;
	bool                    Active = true;

	static ViewportSnapshot Capture(const CanvasViewport& viewport);
	// ResolvedCamera 캐시는 복원하지 않고 비운다 — 카메라 guid 가 바뀌었는데 캐시가 살아
	// 있으면 렌더가 캐시를 그대로 써서 옛 카메라로 계속 그린다(캐시는 죽었을 때만 재해석).
	void                    ApplyTo(CanvasViewport& viewport) const;
};

// 캔버스 배경색. 컬러 피커 드래그는 병합해 undo 1개로 유지한다.
class CSetCanvasBackgroundColorCommand final : public IEditorCommand
{
public:
	CSetCanvasBackgroundColorCommand(SafePtr<CGameCanvas> scene, const float (&oldColor)[4], const float (&newColor)[4]);
	~CSetCanvasBackgroundColorCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;
	bool TryMerge(const IEditorCommand& newer) override;

private:
	bool Apply(const float (&color)[4]);

private:
	SafePtr<CGameCanvas> m_canvas;
	float               m_oldColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	float               m_newColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
};

// 뷰포트 속성 편집. field 는 병합 판정에만 쓴다(같은 뷰포트의 같은 필드 연속 편집 = undo 1개).
class CSetViewportPropertyCommand final : public IEditorCommand
{
public:
	enum class EField : std::uint8_t
	{
		Name,
		Camera,
		Rect,
		LayerFilter,
		Active,
	};

	CSetViewportPropertyCommand(
		SafePtr<CGameCanvas> scene,
		std::size_t index,
		EField field,
		ViewportSnapshot oldProperties,
		ViewportSnapshot newProperties);
	~CSetViewportPropertyCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;
	bool TryMerge(const IEditorCommand& newer) override;

private:
	bool Apply(const ViewportSnapshot& properties);

private:
	SafePtr<CGameCanvas> m_canvas;
	std::size_t         m_index = 0;
	EField              m_field = EField::Name;
	ViewportSnapshot    m_oldProperties;
	ViewportSnapshot    m_newProperties;
};

// 뷰포트 추가(목록 끝 = 화면 최상단). Undo 는 그 항목 제거.
class CCreateViewportCommand final : public IEditorCommand
{
public:
	explicit CCreateViewportCommand(SafePtr<CGameCanvas> scene);
	~CCreateViewportCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;
	// 생성된 뷰포트의 인덱스(미실행이면 npos).
	std::size_t GetIndex() const { return m_index; }

private:
	SafePtr<CGameCanvas> m_canvas;
	std::string         m_name;   // 최초 실행 시 씬이 붙인 자동 이름 — redo 가 같은 이름을 재현
	std::size_t         m_index = 0;
	bool                m_created = false;
};

// 뷰포트 삭제. Undo 는 스냅샷을 원래 자리에 되살린다.
class CDeleteViewportCommand final : public IEditorCommand
{
public:
	CDeleteViewportCommand(SafePtr<CGameCanvas> scene, std::size_t index);
	~CDeleteViewportCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;

private:
	SafePtr<CGameCanvas> m_canvas;
	std::size_t         m_index = 0;
	ViewportSnapshot    m_properties;
	bool                m_deleted = false;
};

namespace EditorCanvasActions
{
	// 캔버스 편집을 undo 스택에 올리는 공용 진입점. 호출자는 Capture 로 뜬 스냅샷의 필드
	// 하나만 바꿔서 넘긴다(레이어의 EditorLayerActions 와 같은 규칙).
	bool SetViewportProperty(CGameCanvas& scene, std::size_t index,
	                         CSetViewportPropertyCommand::EField field,
	                         const ViewportSnapshot& newProperties);
	bool SetBackgroundColor(CGameCanvas& scene, const float (&newColor)[4]);
}

#endif
