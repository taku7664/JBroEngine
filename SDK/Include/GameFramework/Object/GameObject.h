#pragma once

#include "GameFramework/Component/Component.h"
#include "GameFramework/Component/Transform2D.h"
#include "GameFramework/Component/WorldTransform2D.h"
#include "Utillity/Base/BitFlag.h"
#include "Utillity/File/FilePath.h"
#include "Utillity/Pointer/SafePtr.h"

#include <cstdint>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <vector>

class CScene;

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
//  · 고정 크기 → CScene 의 TObjectPool<CGameObject> 에 거주(메모리 소유=풀).
//  · Transform(Local) / WorldTransform(World) / 계층(Parent·Children)을 멤버로 직접
//    보유한다 — 컴포넌트가 아니다.
//  · 컴포넌트는 타입별 풀에 살고, 여기서는 SafePtr 로 참조만 들고 lifetime 을
//    결정한다(Destroy 시 scene 을 통해 각 풀에서 해제).
//  · 외부 공유는 SafeFromThis()/SafePtr<CGameObject>.
//
//  템플릿 메서드 AddComponent/RemoveComponent 는 CScene 완전형이 필요하므로
//  본체는 Scene.h 하단에 정의한다(GetComponent 류는 scene 불필요 → 여기 인라인).
// ─────────────────────────────────────────────────────────────────────────────
class CGameObject final : public EnableSafeFromThis<CGameObject>
{
public:
	CGameObject() = default;
	CGameObject(CScene& scene, const char* name, const File::Guid& instanceGuid)
		: Name(name ? name : "GameObject")
		, InstanceGuid(instanceGuid)
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
	File::Guid    InstanceGuid;

	// 생성순서 키 — 하이라키 표시/저장 정렬용. 풀 슬롯 순회 순서는 생성순서와 무관하므로
	// (할당 역순·슬롯 재사용) 이 단조 증가 값으로 형제 그룹을 정렬한다. 직렬화하지 않는다
	// (로드 시 파일 순서대로 CreateGameObject 가 다시 부여 → 파일 순서 = 표시 순서).
	std::uint64_t CreationOrder = 0;

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

	Transform2D&            GetTransform()       { return Local; }
	const Transform2D&      GetTransform() const { return Local; }
	WorldTransform2D&       GetWorld()           { return World; }
	const WorldTransform2D& GetWorld()     const { return World; }

	CScene* GetScene() const { return m_scene; }

	// ── 컴포넌트 ──────────────────────────────────────────────────────────────
	// AddComponent/RemoveComponent: Scene.h 하단 정의.
	template<typename T, typename... Args> T* AddComponent(Args&&... args);
	template<typename T>                    void RemoveComponent();

	template<typename T>
	T* GetComponent()
	{
		for (const SafePtr<CComponent>& c : m_components)
		{
			if (T* typed = dynamic_cast<T*>(c.TryGet()))
			{
				return typed;
			}
		}
		return nullptr;
	}

	template<typename T>
	const T* GetComponent() const
	{
		for (const SafePtr<CComponent>& c : m_components)
		{
			if (const T* typed = dynamic_cast<const T*>(c.TryGet()))
			{
				return typed;
			}
		}
		return nullptr;
	}

	template<typename T>
	bool HasComponent() const { return nullptr != GetComponent<T>(); }

	// 같은 타입 컴포넌트 전부(멀티 컴포넌트). 첫 1개만 필요하면 GetComponent<T>.
	template<typename T>
	std::vector<T*> GetComponents()
	{
		std::vector<T*> result;
		for (const SafePtr<CComponent>& c : m_components)
		{
			if (T* typed = dynamic_cast<T*>(c.TryGet()))
			{
				result.push_back(typed);
			}
		}
		return result;
	}

	const std::vector<SafePtr<CComponent>>& GetComponents() const { return m_components; }

	// 타입소거 컴포넌트 조회 — Ref<T> 처럼 컴파일타임에 T 를 모르는 코드(또는 DLL 경계)에서
	// 동적 타입(type_index)으로 컴포넌트 주소를 얻는다. 단일 상속이라 반환 주소 == T*.
	void* FindComponentRaw(std::type_index type)
	{
		for (const SafePtr<CComponent>& c : m_components)
		{
			CComponent* comp = c.TryGet();
			if (comp && std::type_index(typeid(*comp)) == type)
			{
				return comp;
			}
		}
		return nullptr;
	}

	// 컴포넌트 InstanceGuid 로 특정 1개를 찾는다(멀티 컴포넌트 지목). 같은 오브젝트에 같은
	// 타입이 여럿이어도 guid 로 구분된다. componentGuid 가 비어 있으면 타입 첫 매치로 폴백
	// (컴포넌트 guid 가 없던 구 데이터/단일 인스턴스 호환). 반환 주소는 단일 상속이라 곧 T*.
	void* FindComponentRawByGuid(const File::Guid& componentGuid, std::type_index type)
	{
		if (componentGuid.IsNull())
		{
			return FindComponentRaw(type);
		}
		for (const SafePtr<CComponent>& c : m_components)
		{
			CComponent* comp = c.TryGet();
			if (comp && std::type_index(typeid(*comp)) == type && comp->InstanceGuid == componentGuid)
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
			if (comp && comp->InstanceGuid == componentGuid)
			{
				return comp;
			}
		}
		return nullptr;
	}

	// scene 의 AddComponent/RemoveComponent 가 호출하는 부착/탈착 훅.
	void AttachComponent(const SafePtr<CComponent>& component) { m_components.push_back(component); }
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

	// ── 계층 ──────────────────────────────────────────────────────────────────
	SafePtr<CGameObject> GetParent() const { return m_parent; }
	const std::vector<SafePtr<CGameObject>>& GetChildren() const { return m_children; }

	bool SetParent(CGameObject& parent);
	void ClearParent();
	bool IsDescendantOf(const CGameObject& possibleAncestor) const;

	// scene 이 사용하는 계층 링크 조작(외부 호출 금지).
	void __AddChild(const SafePtr<CGameObject>& child) { m_children.push_back(child); }
	void __RemoveChild(CGameObject* child)
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
	void __SetParentRef(const SafePtr<CGameObject>& parent) { m_parent = parent; }

	// ── 파괴 ──────────────────────────────────────────────────────────────────
	void Destroy();

private:
	CScene*                          m_scene = nullptr;
	SafePtr<CGameObject>             m_parent;
	std::vector<SafePtr<CGameObject>> m_children;
	std::vector<SafePtr<CComponent>>  m_components;
};
