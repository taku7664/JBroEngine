#pragma once

#include "GameFramework/Component/Component.h"
#include "GameFramework/Component/Transform2D.h"
#include "GameFramework/Component/WorldTransform2D.h"
#include "GameFramework/Object/GameInstance.h"
#include "GameFramework/Reflection/ReflectionTypes.h"
#include "GameFramework/Scene/GameLayer.h"
#include "Utillity/Base/BitFlag.h"
#include "Utillity/File/FilePath.h"
#include "Utillity/Pointer/SafePtr.h"

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

class CGameScene;

// 오브젝트 비트 플래그. 상시 멤버(호스트/DLL/게임 동일 레이아웃 → ABI 안전).
// 비트의 *의미*는 레이어별로 다를 수 있다(EditorHidden 은 에디터 씬뷰 전용).
enum EObjectFlags : unsigned int
{
	ObjectFlag_None         = 0u,
	ObjectFlag_EditorHidden = 1u << 0, // 에디터 씬뷰 렌더 제외. 런타임/게임은 무시.
};

// ─────────────────────────────────────────────────────────────────────────────
//  CGameObject — 씬의 실체 객체(다형성 컴포넌트 구조).
//
//  · 고정 크기 → CGameScene 의 TObjectPool<CGameObject> 에 거주(메모리 소유=풀).
//  · Transform(Local) / WorldTransform(World) / 계층(Parent·Children)을 멤버로 직접
//    보유한다 — 컴포넌트가 아니다.
//  · 컴포넌트는 타입별 풀에 살고, 여기서는 SafePtr 로 참조만 들고 lifetime 을
//    결정한다(Destroy 시 scene 을 통해 각 풀에서 해제).
//  · 외부 공유는 SafeFromThis()/SafePtr<CGameObject>.
//
//  템플릿 메서드 AddComponent/RemoveComponent 는 CGameScene 완전형이 필요하므로
//  본체는 Scene.h 하단에 정의한다(GetComponent 류는 scene 불필요 → 여기 인라인).
// ─────────────────────────────────────────────────────────────────────────────
class CGameObject final : public GameInstance, public EnableSafeFromThis<CGameObject>
{
public:
	CGameObject() = default;
	CGameObject(CGameScene& scene, const char* name, const File::Guid& instanceGuid)
		: GameInstance(instanceGuid)
		, Name(name ? name : "GameObject")
		, m_scene(&scene)
	{
	}

	CGameObject(const CGameObject&) = delete;
	CGameObject& operator=(const CGameObject&) = delete;

	// ── 기본 속성 ────────────────────────────────────────────────────────────
	std::string   Name;
	std::string   Tag;            // 자유 분류 태그(검색/그룹핑용). 비어 있을 수 있음.
	bool          IsActive = true;
	BitFlag       Flags;          // EObjectFlags. 직렬화됨. 확장용(예: EditorHidden).
	// InstanceGuid 는 GameInstance 베이스가 보유한다.

	// ── 플래그 편의 ──────────────────────────────────────────────────────────
	bool IsEditorHidden() const { return Flags.Has(ObjectFlag_EditorHidden); }
	void SetEditorHidden(bool hidden)
	{
		if (hidden) Flags.Add(ObjectFlag_EditorHidden);
		else        Flags.Remove(ObjectFlag_EditorHidden);
	}

	// ── Transform (컴포넌트 아님 — 멤버) ──────────────────────────────────────
	Transform2D      Local;
	WorldTransform2D World;

	const char* GetName() const { return Name.c_str(); }
	void        SetName(const char* name) { Name = name ? name : ""; }
	bool        IsActiveSelf() const { return IsActive; }
	void        SetActive(bool active) { IsActive = active; }

	// 계층 활성 — 자신 + 모든 조상이 활성일 때만 true (Unity activeInHierarchy 시맨틱).
	// 부모가 비활성이면 자식도 비활성으로 간주된다. O(계층 깊이).
	bool IsActiveInHierarchy() const
	{
		if (false == IsActive)
		{
			return false;
		}
		for (const CGameObject* ancestor = m_parent.TryGet();
		     nullptr != ancestor;
		     ancestor = ancestor->m_parent.TryGet())
		{
			if (false == ancestor->IsActive)
			{
				return false;
			}
		}
		return true;
	}

	Transform2D&            GetTransform()       { return Local; }
	const Transform2D&      GetTransform() const { return Local; }
	WorldTransform2D&       GetWorld()           { return World; }
	const WorldTransform2D& GetWorld()     const { return World; }

	CGameScene* GetScene() const { return m_scene; }
	std::uint64_t GetCreationOrder() const { return m_creationOrder; }

	// ── 레이어 ────────────────────────────────────────────────────────────────
	// 소속 레이어. 불변식: 자식은 항상 부모와 같은 레이어(SetParent 가 서브트리 전파).
	// 재배정은 CGameScene::MoveObjectToLayer(루트 서브트리 단위)로만 한다.
	SafePtr<CGameLayer> GetLayer() const { return m_layer; }
	// 소속 레이어의 컴포짓 순서 — 렌더 수집이 아이템마다 읽는다(매 프레임). 레이어가 인덱스를
	// 캐시하므로 O(1). 미배정(로드 과도기 등)은 0 = 맨 아래 레이어로 간주.
	std::uint16_t GetLayerIndex() const
	{
		const CGameLayer* layer = m_layer.TryGet();
		return layer ? layer->GetIndex() : 0;
	}

	// ── 컴포넌트 ──────────────────────────────────────────────────────────────
	// AddComponent/RemoveComponent: Scene.h 하단 정의.
	template<typename T, typename... Args> T* AddComponent(Args&&... args);
	template<typename T>                    void RemoveComponent();

	template<typename T>
	T* GetComponent()
	{
		static_assert(std::is_base_of_v<CComponent, T>, "T must derive from CComponent.");
		if constexpr (std::is_same_v<CComponent, T>)
		{
			return static_cast<T*>(FindComponentRaw(INVALID_TYPE_ID));
		}
		else if constexpr (requires { T::StaticTypeName(); })
		{
			constexpr TypeId typeId = MakeStableTypeId(T::StaticTypeName());
			return static_cast<T*>(FindComponentRaw(typeId));
		}
		else
		{
			// 자동 생성 스크립트는 아직 StaticTypeName을 갖지 않는다. 이 호환 경로는
			// 일반 컴포넌트 조회에는 사용되지 않으며, 스크립트 타입 특성 생성 후 제거한다.
			for (const SafePtr<CComponent>& componentRef : m_components)
			{
				if (T* typed = dynamic_cast<T*>(componentRef.TryGet()))
				{
					return typed;
				}
			}
			return nullptr;
		}
	}

	template<typename T>
	const T* GetComponent() const
	{
		static_assert(std::is_base_of_v<CComponent, T>, "T must derive from CComponent.");
		if constexpr (std::is_same_v<CComponent, T>)
		{
			return static_cast<const T*>(FindComponentRaw(INVALID_TYPE_ID));
		}
		else if constexpr (requires { T::StaticTypeName(); })
		{
			constexpr TypeId typeId = MakeStableTypeId(T::StaticTypeName());
			return static_cast<const T*>(FindComponentRaw(typeId));
		}
		else
		{
			for (const SafePtr<CComponent>& componentRef : m_components)
			{
				if (const T* typed = dynamic_cast<const T*>(componentRef.TryGet()))
				{
					return typed;
				}
			}
			return nullptr;
		}
	}

	template<typename T>
	bool HasComponent() const { return nullptr != GetComponent<T>(); }

	// 같은 타입 컴포넌트 전부(멀티 컴포넌트). 첫 1개만 필요하면 GetComponent<T>.
	template<typename T>
	std::vector<T*> GetComponents()
	{
		static_assert(std::is_base_of_v<CComponent, T>, "T must derive from CComponent.");
		std::vector<T*> result;
		if constexpr (std::is_same_v<CComponent, T>)
		{
			result.reserve(m_components.size());
			for (const SafePtr<CComponent>& componentRef : m_components)
			{
				if (CComponent* component = componentRef.TryGet())
				{
					result.push_back(component);
				}
			}
		}
		else if constexpr (requires { T::StaticTypeName(); })
		{
			constexpr TypeId typeId = MakeStableTypeId(T::StaticTypeName());
			for (const SafePtr<CComponent>& componentRef : m_components)
			{
				CComponent* component = componentRef.TryGet();
				if (component && component->GetTypeId() == typeId)
				{
					result.push_back(static_cast<T*>(component));
				}
			}
		}
		else
		{
			for (const SafePtr<CComponent>& componentRef : m_components)
			{
				if (T* typed = dynamic_cast<T*>(componentRef.TryGet()))
				{
					result.push_back(typed);
				}
			}
		}
		return result;
	}

	const std::vector<SafePtr<CComponent>>& GetComponents() const { return m_components; }

private:
	bool MoveComponent(const File::Guid& instanceGuid, std::size_t newIndex);

	// 타입소거 컴포넌트 조회 — Ref<T>처럼 컴파일타임에 T를 모르는 코드(또는 DLL 경계)에서
	// 생성 시 고정된 TypeId로 컴포넌트 주소를 얻는다. 단일 상속이라 반환 주소 == T*.
	void* FindComponentRaw(TypeId typeId)
	{
		for (const SafePtr<CComponent>& c : m_components)
		{
			CComponent* comp = c.TryGet();
			if (comp && (INVALID_TYPE_ID == typeId || comp->GetTypeId() == typeId))
			{
				return comp;
			}
		}
		return nullptr;
	}

	const void* FindComponentRaw(TypeId typeId) const
	{
		for (const SafePtr<CComponent>& componentRef : m_components)
		{
			const CComponent* component = componentRef.TryGet();
			if (component && (INVALID_TYPE_ID == typeId || component->GetTypeId() == typeId))
			{
				return component;
			}
		}
		return nullptr;
	}

	// 컴포넌트 InstanceGuid 로 특정 1개를 찾는다(멀티 컴포넌트 지목). 같은 오브젝트에 같은
	// 타입이 여럿이어도 guid 로 구분된다. componentGuid 가 비어 있으면 타입 첫 매치로 폴백
	// (컴포넌트 guid 가 없던 구 데이터/단일 인스턴스 호환). 반환 주소는 단일 상속이라 곧 T*.
	void* FindComponentRawByGuid(const File::Guid& componentGuid, TypeId typeId)
	{
		if (componentGuid.IsNull())
		{
			return FindComponentRaw(typeId);
		}
		for (const SafePtr<CComponent>& c : m_components)
		{
			CComponent* comp = c.TryGet();
			if (comp && (INVALID_TYPE_ID == typeId || comp->GetTypeId() == typeId) && comp->GetInstanceGuid() == componentGuid)
			{
				return comp;
			}
		}
		return nullptr;
	}

	const void* FindComponentRawByGuid(const File::Guid& componentGuid, TypeId typeId) const
	{
		if (componentGuid.IsNull())
		{
			for (const SafePtr<CComponent>& c : m_components)
			{
				const CComponent* comp = c.TryGet();
				if (comp && (INVALID_TYPE_ID == typeId || comp->GetTypeId() == typeId))
				{
					return comp;
				}
			}
			return nullptr;
		}
		for (const SafePtr<CComponent>& c : m_components)
		{
			const CComponent* comp = c.TryGet();
			if (comp && (INVALID_TYPE_ID == typeId || comp->GetTypeId() == typeId) && comp->GetInstanceGuid() == componentGuid)
			{
				return comp;
			}
		}
		return nullptr;
	}

	// 컴포넌트 guid 로 특정 컴포넌트 자체를 찾는다(타입 무관). 스크립트 Ref 해석 등에 사용.
	CComponent* FindComponentByGuid(const File::Guid& componentGuid)
	{
		if (componentGuid.IsNull())
		{
			return nullptr;
		}
		for (const SafePtr<CComponent>& c : m_components)
		{
			CComponent* comp = c.TryGet();
			if (comp && comp->GetInstanceGuid() == componentGuid)
			{
				return comp;
			}
		}
		return nullptr;
	}
	const CComponent* FindComponentByGuid(const File::Guid& componentGuid) const
	{
		if (componentGuid.IsNull()) return nullptr;
		for (const SafePtr<CComponent>& c : m_components)
		{
			const CComponent* comp = c.TryGet();
			if (comp && comp->GetInstanceGuid() == componentGuid) return comp;
		}
		return nullptr;
	}

	// scene 의 AddComponent/RemoveComponent 가 호출하는 부착/탈착 훅.
	void AttachComponent(const SafePtr<CComponent>& component)
	{
		m_components.push_back(component);
	}
	void DetachComponent(CComponent* component)
	{
		for (std::size_t i = 0; i < m_components.size(); ++i)
		{
			if (m_components[i].TryGet() == component)
			{
				m_components.erase(m_components.begin() + i);
				return;
			}
		}
	}

public:
	// ── 계층 ──────────────────────────────────────────────────────────────────
	SafePtr<CGameObject> GetParent() const { return m_parent; }
	const std::vector<SafePtr<CGameObject>>& GetChildren() const { return m_children; }

	bool SetParent(CGameObject& parent);
	void ClearParent();
	bool IsDescendantOf(const CGameObject& possibleAncestor) const;

	// ── 파괴 ──────────────────────────────────────────────────────────────────
	void Destroy();

private:
	friend class CGameScene;
	friend class CSceneRuntimeAccess;

	// 계층 링크 조작 — SetParent/ClearParent 내부 전용(예약 식별자 `__` 제거).
	void AddChildInternal(const SafePtr<CGameObject>& child) { m_children.push_back(child); }
	void RemoveChildInternal(CGameObject* child)
	{
		for (std::size_t i = 0; i < m_children.size(); ++i)
		{
			if (m_children[i].TryGet() == child)
			{
				m_children.erase(m_children.begin() + i);
				return;
			}
		}
	}

	// 소속 레이어를 서브트리 전체에 적용한다 — SetParent(부모 레이어 상속)와
	// CGameScene::MoveObjectToLayer 전용.
	void SetLayerRecursive(const SafePtr<CGameLayer>& layer);

	CGameScene*                       m_scene = nullptr;
	// 하이라키 표시/저장용 내부 정렬 키. 변경은 CSceneRuntimeAccess만 수행한다.
	std::uint64_t                     m_creationOrder = 0;
	SafePtr<CGameLayer>               m_layer;
	SafePtr<CGameObject>              m_parent;
	std::vector<SafePtr<CGameObject>> m_children;
	std::vector<SafePtr<CComponent>>  m_components;
};

// 사용자 대면 별칭 — 스크립트/문서는 접두사 없는 GameObject 로 쓴다.
using GameObject = CGameObject;

// ── 단일 활성 게이트 ─────────────────────────────────────────────────────────
// 컴포넌트가 이번 프레임 로직/렌더 대상인지: 오너 계층 활성 + 컴포넌트 Enabled.
// 모든 시스템(Render/Audio/Camera/Script/Physics)은 반드시 이 게이트를 쓴다 —
// 시스템별로 owner->IsActive 를 제각각 판단하던 불일치를 없애기 위함.
inline bool IsActiveComponent(const CComponent& component)
{
	const CGameObject* owner = component.GetOwner().TryGet();
	return nullptr != owner && owner->IsActiveInHierarchy() && component.IsEnabled();
}
