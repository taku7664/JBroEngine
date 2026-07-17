#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Editor/Command/EditorCommandManager.h"
#include "Engine/GameFramework/Component/Transform2D.h"
#include "Engine/GameFramework/Reflection/ReflectionTypes.h"
#include "Utillity/File/FilePath.h" // File::Guid (오브젝트 안정 식별자 = InstanceGuid)

#include <vector>

class CGameCanvas;
class CGameObject;
class CGameLayer;

// 명령은 오브젝트를 InstanceGuid 로 보관한다(포인터/정수 id 아님).
// 파괴→undo→redo 로 오브젝트가 재생성돼 주소가 바뀌어도 guid 로 안전하게 재해석된다.

class CAddComponentCommand final : public IEditorCommand
{
public:
	CAddComponentCommand(SafePtr<CGameCanvas> scene, CGameObject* object, TypeId componentTypeId);
	~CAddComponentCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;

private:
	SafePtr<CGameCanvas> m_canvas;
	File::Guid m_objectGuid;
	TypeId m_componentTypeId = INVALID_TYPE_ID;
	File::Guid m_componentGuid;
	bool m_added = false;
};

class CAddScriptCommand final : public IEditorCommand
{
public:
	CAddScriptCommand(SafePtr<CGameCanvas> scene, CGameObject* object, TypeId scriptTypeId);
	~CAddScriptCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;

private:
	SafePtr<CGameCanvas> m_canvas;
	File::Guid m_objectGuid;
	TypeId m_scriptTypeId = INVALID_TYPE_ID;
	File::Guid m_scriptComponentGuid;
	bool m_added = false;
};

class CRemoveScriptCommand final : public IEditorCommand
{
public:
	CRemoveScriptCommand(SafePtr<CGameCanvas> scene, CGameObject* object, const File::Guid& componentGuid);
	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;
private:
	SafePtr<CGameCanvas> m_canvas;
	File::Guid m_objectGuid;
	File::Guid m_componentGuid;
	std::string m_snapshot;
	std::size_t m_componentIndex = 0;
	bool m_removed = false;
};

class CReorderComponentCommand final : public IEditorCommand
{
public:
	CReorderComponentCommand(SafePtr<CGameCanvas> scene, CGameObject* object, const File::Guid& componentGuid, std::size_t oldIndex, std::size_t newIndex);
	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;
private:
	bool Move(std::size_t index);
	SafePtr<CGameCanvas> m_canvas;
	File::Guid m_objectGuid;
	File::Guid m_componentGuid;
	std::size_t m_oldIndex = 0;
	std::size_t m_newIndex = 0;
	bool m_executed = false;
};

class CCreateGameObjectCommand final : public IEditorCommand
{
public:
	// parent == nullptr 이면 루트에 생성, 그 외에는 parent 의 자식으로 생성.
	// spawnWorldPos != nullptr 이면 그 월드 좌표에 생성한다(캔버스뷰 우클릭 위치). parent 가
	// 있으면 부모 월드 역행렬로 로컬 좌표를 환산한다. null 이면 기본(원점) 생성.
	// layer == nullptr 이면 캔버스 기본 레이어. parent 가 있으면 레이어는 부모를 따르므로 무시된다.
	CCreateGameObjectCommand(SafePtr<CGameCanvas> scene, const char* name,
	                         CGameObject* parent = nullptr,
	                         const Vector2* spawnWorldPos = nullptr,
	                         CGameLayer* layer = nullptr);
	~CCreateGameObjectCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;
	// 생성된 오브젝트(없으면 nullptr). 호출자가 선택 처리에 사용.
	CGameObject* GetEntity() const;

private:
	SafePtr<CGameCanvas> m_canvas;
	std::string m_name;
	File::Guid m_parentGuid; // null = 루트
	File::Guid m_layerGuid;  // null = 기본 레이어
	File::Guid m_objectGuid; // 생성된 오브젝트의 안정 식별자(redo 시 동일 guid 강제)
	bool     m_created = false;
	bool     m_hasSpawnPos = false; // spawnWorldPos 지정 여부
	Vector2  m_spawnWorldPos = Vector2(0.0f, 0.0f); // 월드 생성 위치(m_hasSpawnPos 일 때만)
};

class CSetComponentPropertyCommand final : public IEditorCommand
{
public:
	CSetComponentPropertyCommand(
		SafePtr<CGameCanvas> scene,
		CGameObject* object,
		TypeId componentTypeId,
		std::size_t propertyOffset,
		std::vector<std::uint8_t> oldValue,
		std::vector<std::uint8_t> newValue,
		const File::Guid& componentGuid);
	~CSetComponentPropertyCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;
	// 같은 (오브젝트·컴포넌트·프로퍼티·인스턴스) 편집이면 newer 의 새 값만 흡수(드래그 묶기).
	bool TryMerge(const IEditorCommand& newer) override;

private:
	bool WriteValue(const std::vector<std::uint8_t>& value);

private:
	SafePtr<CGameCanvas> m_canvas;
	File::Guid m_objectGuid;
	File::Guid m_componentGuid;
	TypeId m_componentTypeId = INVALID_TYPE_ID;
	std::size_t m_propertyOffset = 0;
	std::vector<std::uint8_t> m_oldValue;
	std::vector<std::uint8_t> m_newValue;
};

class CSetComponentEnabledCommand final : public IEditorCommand
{
public:
	CSetComponentEnabledCommand(
		SafePtr<CGameCanvas> scene,
		CGameObject* object,
		const File::Guid& componentGuid,
		bool oldValue,
		bool newValue);

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;

private:
	bool Apply(bool value);

	SafePtr<CGameCanvas> m_canvas;
	File::Guid m_objectGuid;
	File::Guid m_componentGuid;
	bool m_oldValue = true;
	bool m_newValue = true;
};

class CSetComponentStringPropertyCommand final : public IEditorCommand
{
public:
	CSetComponentStringPropertyCommand(SafePtr<CGameCanvas> scene, CGameObject* object, TypeId componentTypeId,
		std::size_t propertyOffset, std::string oldValue, std::string newValue, const File::Guid& componentGuid);
	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;
	bool TryMerge(const IEditorCommand& newer) override;
private:
	bool WriteValue(const std::string& value);
	SafePtr<CGameCanvas> m_canvas;
	File::Guid m_objectGuid;
	File::Guid m_componentGuid;
	TypeId m_componentTypeId = INVALID_TYPE_ID;
	std::size_t m_propertyOffset = 0;
	std::string m_oldValue;
	std::string m_newValue;
};

// 오브젝트 Transform(Local) 편집. Transform 은 컴포넌트가 아니라 CGameObject 멤버라
// 컴포넌트 프로퍼티 커맨드 경로를 못 탄다 → 전용 커맨드. 다중 선택 지원: 한 번의 편집을
// 델타로 보고 선택된 모든 오브젝트에 같은 델타를 적용한다(undo 1개). 드래그는 TryMerge 로 묶인다.
//   delta: Position/Scale 은 가산(+), RotationRadians 도 가산. 각 오브젝트 = old[i] + delta.
class CSetObjectTransformCommand final : public IEditorCommand
{
public:
	CSetObjectTransformCommand(SafePtr<CGameCanvas> scene,
	                           const std::vector<CGameObject*>& objects,
	                           const Transform2D& delta);
	~CSetObjectTransformCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;
	bool TryMerge(const IEditorCommand& newer) override;

private:
	void Apply(bool withDelta);

	SafePtr<CGameCanvas>          m_canvas;
	std::vector<File::Guid>  m_objectGuids; // 대상 오브젝트들
	std::vector<Transform2D> m_oldTransforms; // 드래그 시작 시점 각 오브젝트 Transform(병렬)
	Transform2D              m_delta;        // 누적 델타(Position/Rotation/Scale 가산)
};

// 오브젝트 Transform(Local) 절대값 편집. Guizmo 처럼 대상별 local delta 가 달라질 수 있는
// 편집은 공통 delta 커맨드로 표현하면 부모 transform 반례에서 깨지므로 old/new 스냅샷을 저장한다.
class CSetObjectTransformsCommand final : public IEditorCommand
{
public:
	CSetObjectTransformsCommand(SafePtr<CGameCanvas> scene,
	                            const std::vector<CGameObject*>& objects,
	                            const std::vector<Transform2D>& oldTransforms,
	                            const std::vector<Transform2D>& newTransforms);
	~CSetObjectTransformsCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;

private:
	void Apply(const std::vector<Transform2D>& transforms);

	SafePtr<CGameCanvas>          m_canvas;
	std::vector<File::Guid>  m_objectGuids;
	std::vector<Transform2D> m_oldTransforms;
	std::vector<Transform2D> m_newTransforms;
};

// 오브젝트(+서브트리) 삭제. Undo 는 직렬화 스냅샷(프리팹 텍스트)으로 복원.
class CDeleteGameObjectCommand final : public IEditorCommand
{
public:
	CDeleteGameObjectCommand(SafePtr<CGameCanvas> scene, CGameObject* object);
	~CDeleteGameObjectCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;

private:
	SafePtr<CGameCanvas> m_canvas;
	File::Guid  m_objectGuid;  // 삭제 대상(redo 재삭제·undo 후 재해석 키)
	File::Guid  m_parentGuid;  // 복원 시 재부모(null = 루트)
	// 복원 시 소속 레이어. 역직렬화는 기본 레이어에 만들므로 루트는 여기서 되돌려야 한다
	// (자식은 SetParent 가 부모 레이어를 서브트리에 전파해 저절로 맞는다).
	File::Guid  m_layerGuid;
	std::string m_snapshot;    // 서브트리 직렬화(undo 복원용)
	bool        m_deleted = false;
};

// 여러 오브젝트(+각 서브트리)를 한 번의 undo 단위로 삭제한다.
// 호출자는 부모/자식 중복 삭제를 피하기 위해 최상위 선택만 넘기는 것을 전제로 한다.
class CDeleteGameObjectsCommand final : public IEditorCommand
{
public:
	CDeleteGameObjectsCommand(SafePtr<CGameCanvas> scene, const std::vector<CGameObject*>& objects);
	~CDeleteGameObjectsCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;

private:
	struct Entry
	{
		File::Guid ObjectGuid;
		File::Guid ParentGuid;
		File::Guid LayerGuid;   // 복원 시 소속 레이어(단일 삭제의 m_layerGuid 와 같은 이유)
		std::string Snapshot;
		bool Deleted = false;
	};

	SafePtr<CGameCanvas> m_canvas;
	std::vector<Entry> m_entries;
};

// 클립보드 오브젝트(들)을 붙여넣는다. 다중 + 위치 지정 + undo(붙여넣은 루트 일괄 파괴) 지원.
class CPasteObjectsCommand final : public IEditorCommand
{
public:
	// clipboardText = Serialization::SerializeObjects 결과(레거시 단일 포맷도 허용).
	// spawnWorldPos != nullptr 이면 붙여넣은 그룹 중심을 그 월드 좌표로 이동(상대 배치 보존).
	// parent != nullptr 이면 붙여넣은 루트를 해당 오브젝트의 자식으로 둔다.
	// layer != nullptr 이면 붙여넣은 루트를 그 레이어에 둔다(parent 가 있으면 부모 레이어가 이긴다).
	CPasteObjectsCommand(SafePtr<CGameCanvas> scene, std::string clipboardText,
	                     const Vector2* spawnWorldPos = nullptr,
	                     CGameObject* parent = nullptr,
	                     CGameLayer* layer = nullptr);
	~CPasteObjectsCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;

	// 붙여넣은 루트 오브젝트들(현재 캔버스 기준 재해석). 호출자가 선택 처리에 사용.
	std::vector<CGameObject*> GetPastedRoots() const;

private:
	SafePtr<CGameCanvas> m_canvas;
	std::string m_clipboard;        // 최초 실행 후엔 정규화 스냅샷(guid/위치 고정 → redo 재현)
	File::Guid  m_parentGuid;       // null = 루트
	// null = 기본 레이어. 오브젝트 직렬화는 계층·레이어를 담지 않으므로(둘 다 캔버스 레벨 관심사)
	// 역직렬화 직후 루트는 항상 캔버스 루트 + 기본 레이어다 → 매 Execute 마다 다시 배정해야 한다.
	File::Guid  m_layerGuid;
	bool        m_hasSpawnPos = false;
	Vector2     m_spawnWorldPos = Vector2(0.0f, 0.0f);
	std::vector<File::Guid> m_pastedGuids; // 붙여넣은 루트(undo 파괴/선택)
	bool        m_firstDone = false;       // 최초 Execute 완료(이후 reissue/offset 생략)
};

// 컴포넌트 1개 제거. Undo 는 직렬화 스냅샷으로 재부착+값 복원.
class CRemoveComponentCommand final : public IEditorCommand
{
public:
	CRemoveComponentCommand(SafePtr<CGameCanvas> scene, CGameObject* object, TypeId componentTypeId, const File::Guid& componentGuid);
	~CRemoveComponentCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;

private:
	bool RemoveNow();

	SafePtr<CGameCanvas> m_canvas;
	File::Guid  m_objectGuid;
	File::Guid  m_componentGuid;
	TypeId      m_componentTypeId = INVALID_TYPE_ID;
	std::string m_snapshot;     // 제거 전 컴포넌트 직렬화(undo 복원용)
	bool        m_removed = false;
};

// SetParent 와 WorldStay(로컬 Transform 자동 보정)를 함께 처리하는 커맨드.
// Undo: 이전 부모 관계 + 이전 로컬 Transform 복원.
// Redo: 새 부모 관계 + 계산된 새 로컬 Transform 재적용.
class CSetParentCommand final : public IEditorCommand
{
public:
	// newParent = nullptr 이면 부모 해제(루트로 이동).
	CSetParentCommand(SafePtr<CGameCanvas> scene, CGameObject* child, CGameObject* newParent);
	~CSetParentCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;

private:
	SafePtr<CGameCanvas> m_canvas;
	File::Guid      m_childGuid;
	File::Guid      m_oldParentGuid; // null = 루트
	File::Guid      m_newParentGuid; // null = 루트
	Transform2D     m_oldLocalTransform;
	Transform2D     m_newLocalTransform;
	bool            m_executed       = false;
};

// Hierarchy 드래그 드롭: 부모 변경(WorldStay)·소속 레이어·표시 순서(CreationOrder)를
// 한 undo 단위로 처리. 셋은 드롭 한 번에 함께 바뀌므로 커맨드를 나누면 undo 가 쪼개진다.
class CMoveGameObjectInHierarchyCommand final : public IEditorCommand
{
public:
	// newParent = nullptr 이면 루트로 이동.
	// insertNear = nullptr 이면 해당 parent 그룹의 맨 아래로 이동.
	// insertAfter = false/true 는 insertNear 앞/뒤 삽입.
	// newLayer = nullptr 이면 레이어 유지. 자식으로 이동하는 경우 레이어는 부모를 따라가므로
	// (자식=부모 레이어 불변식) 루트로 이동할 때만 의미가 있다.
	CMoveGameObjectInHierarchyCommand(
		SafePtr<CGameCanvas> scene,
		CGameObject* object,
		CGameObject* newParent,
		CGameObject* insertNear = nullptr,
		bool insertAfter = true,
		CGameLayer* newLayer = nullptr);
	~CMoveGameObjectInHierarchyCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo() override;
	void Redo() override;

private:
	struct OrderSnapshot
	{
		File::Guid ObjectGuid;
		std::uint64_t CreationOrder = 0;
	};

	bool Apply(const File::Guid& parentGuid, const File::Guid& layerGuid,
	           const Transform2D* localTransform, bool computeWorldStay);
	void CaptureOrders(std::vector<OrderSnapshot>& out) const;
	void RestoreOrders(const std::vector<OrderSnapshot>& orders);
	void RebuildOrder(CGameObject& object);

private:
	SafePtr<CGameCanvas> m_canvas;
	File::Guid m_objectGuid;
	File::Guid m_oldParentGuid;
	File::Guid m_newParentGuid;
	File::Guid m_insertNearGuid;
	File::Guid m_oldLayerGuid; // 이동 전 소속 레이어(undo 복원)
	File::Guid m_newLayerGuid; // null = 레이어 유지
	bool m_insertAfter = true;
	Transform2D m_oldLocalTransform;
	Transform2D m_newLocalTransform;
	std::vector<OrderSnapshot> m_oldOrders;
	std::vector<OrderSnapshot> m_newOrders;
	bool m_executed = false;
};

// PolygonCollider2D 의 버텍스 목록을 통째로 교체하는 커맨드.
// Execute / Redo: newPoints 를 LocalPoints 에 적용 + dirty 캐시 마킹.
// Undo         : 이전 LocalPoints 복원 + dirty 캐시 마킹.
class CModifyPolygonVerticesCommand final : public IEditorCommand
{
public:
	CModifyPolygonVerticesCommand(
		SafePtr<CGameCanvas>                   scene,
		CGameObject*                      object,
		std::vector<Vector2>       newPoints);
	~CModifyPolygonVerticesCommand() override = default;

	const char* GetName() const override;
	bool Execute() override;
	void Undo()    override;
	void Redo()    override;

private:
	bool Apply(const std::vector<Vector2>& points);

private:
	SafePtr<CGameCanvas>               m_canvas;
	File::Guid                    m_objectGuid;
	std::vector<Vector2>   m_oldPoints;
	std::vector<Vector2>   m_newPoints;
	bool                          m_executed = false;
};

#endif
