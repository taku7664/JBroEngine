#include "pch.h"
#include "GameObject.h"

#include "GameFramework/Scene/Scene.h"

bool CGameObject::SetParent(CGameObject& parent)
{
	if (this == &parent || parent.IsDescendantOf(*this))
	{
		return false;
	}

	ClearParent();
	m_parent = parent.SafeFromThis();
	parent.__AddChild(SafeFromThis());
	return true;
}

void CGameObject::ClearParent()
{
	if (CGameObject* parent = m_parent.TryGet())
	{
		parent->__RemoveChild(this);
	}
	m_parent.Reset();
}

bool CGameObject::IsDescendantOf(const CGameObject& possibleAncestor) const
{
	const CGameObject* current = m_parent.TryGet();
	while (nullptr != current)
	{
		if (current == &possibleAncestor)
		{
			return true;
		}
		current = current->m_parent.TryGet();
	}
	return false;
}

void CGameObject::Destroy()
{
	if (m_scene)
	{
		m_scene->DestroyGameObject(this);
	}
}
