#pragma once

class CScene;

// Game systems are owned by CScene inside GameCode. Editor/live-compile code
// must not retain system pointers across DLL reloads.
class CGameSystem
{
public:
	virtual ~CGameSystem() = default;

public:
	void Initialize(CScene& scene);
	void Update(CScene& scene);
	void FixedUpdate(CScene& scene);
	void Finalize(CScene& scene);
	bool IsInitialized() const;
	virtual bool ShouldUpdateInEditMode() const { return false; }

protected:
	virtual void OnInitialize(CScene& scene) {}
	virtual void OnUpdate(CScene& scene) {}
	virtual void OnFixedUpdate(CScene& scene) {}
	virtual void OnFinalize(CScene& scene) {}

private:
	bool m_isInitialized = false;
};
