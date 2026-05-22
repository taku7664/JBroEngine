#pragma once

#include "GameFramework/ECS/EntityTypes.h"

class CScene;

class CGameScript
{
public:
	virtual ~CGameScript() = default;

public:
	void Bind(CScene& scene, EntityId entity);
	CScene* GetScene() const;
	EntityId GetEntity() const;

	void Create();
	void Start();
	void Update();
	void Destroy();
	bool IsStarted() const;

protected:
	virtual void OnCreate() {}
	virtual void OnStart() {}
	virtual void OnUpdate() {}
	virtual void OnDestroy() {}

private:
	CScene* m_scene = nullptr;
	EntityId m_entity = INVALID_ENTITY_ID;
	bool m_isCreated = false;
	bool m_isStarted = false;
};
