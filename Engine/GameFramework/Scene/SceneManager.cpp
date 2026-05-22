#include "pch.h"
#include "SceneManager.h"

#include "GameFramework/Scene/Scene.h"

CSceneManager::~CSceneManager()
{
	Clear();
}

CScene* CSceneManager::CreateScene(const char* name)
{
	if (nullptr == name || '\0' == name[0])
	{
		return nullptr;
	}

	auto it = m_scenes.find(name);
	if (it != m_scenes.end())
	{
		return it->second.Get();
	}

	OwnerPtr<CScene> scene = MakeOwnerPtr<CScene>();
	CScene* rawScene = scene.Get();
	m_scenes.emplace(name, std::move(scene));
	if (false == m_activeScene.IsValid())
	{
		m_activeScene = rawScene->SafeFromThis();
	}
	return rawScene;
}

bool CSceneManager::DestroyScene(const char* name)
{
	if (nullptr == name)
	{
		return false;
	}

	auto it = m_scenes.find(name);
	if (it == m_scenes.end())
	{
		return false;
	}

	if (m_activeScene.TryGet() == it->second.Get())
	{
		m_activeScene = nullptr;
	}

	m_scenes.erase(it);
	return true;
}

bool CSceneManager::SetActiveScene(const char* name)
{
	SafePtr<CScene> scene = FindScene(name);
	if (false == scene.IsValid())
	{
		return false;
	}

	m_activeScene = scene;
	return true;
}

SafePtr<CScene> CSceneManager::GetActiveScene() const
{
	return m_activeScene;
}

SafePtr<CScene> CSceneManager::FindScene(const char* name) const
{
	if (nullptr == name)
	{
		return nullptr;
	}

	auto it = m_scenes.find(name);
	return it != m_scenes.end() ? it->second.GetSafePtr() : nullptr;
}

std::size_t CSceneManager::GetLoadedSceneCount() const
{
	return m_scenes.size();
}

bool CSceneManager::GetLoadedSceneNames(std::vector<std::string>& outNames) const
{
	outNames.clear();
	outNames.reserve(m_scenes.size());
	for (const auto& pair : m_scenes)
	{
		outNames.push_back(pair.first);
	}
	return true;
}

void CSceneManager::Update()
{
	if (m_activeScene)
	{
		m_activeScene->Update();
	}
}

void CSceneManager::Clear()
{
	m_activeScene = nullptr;
	m_scenes.clear();
}
