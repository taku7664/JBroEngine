#pragma once

class CGameScene;

// Game systems are owned by CGameScene inside GameScript. Editor/live-compile code
// must not retain system pointers across DLL reloads.
class CGameSystem
{
public:
	virtual ~CGameSystem() = default;

public:
	void Initialize(CGameScene& scene);
	void Update(CGameScene& scene);
	void FixedUpdate(CGameScene& scene);
	void Finalize(CGameScene& scene);
	// 시뮬레이션 정지 시 호출 — 시스템을 종료(Finalize)하지 않고 재생 상태만 정리한다.
	// (예: CAudioSystem 이 재생 중인 사운드 인스턴스를 멈추고 해제.) m_isInitialized 는 유지.
	void SimulationStop(CGameScene& scene);
	bool IsInitialized() const;
	virtual bool ShouldUpdateInEditMode() const { return false; }

protected:
	virtual void OnInitialize(CGameScene& scene) {}
	virtual void OnUpdate(CGameScene& scene) {}
	virtual void OnFixedUpdate(CGameScene& scene) {}
	virtual void OnFinalize(CGameScene& scene) {}
	virtual void OnSimulationStop(CGameScene& scene) {}

private:
	bool m_isInitialized = false;
};
