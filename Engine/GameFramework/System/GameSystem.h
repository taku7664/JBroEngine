#pragma once

class CGameCanvas;

// Game systems are owned by CGameCanvas inside GameScript. Editor/live-compile code
// must not retain system pointers across DLL reloads.
class CGameSystem
{
public:
	virtual ~CGameSystem() = default;

public:
	void Initialize(CGameCanvas& scene);
	void Update(CGameCanvas& scene);
	void FixedUpdate(CGameCanvas& scene);
	void Finalize(CGameCanvas& scene);
	// 시뮬레이션 정지 시 호출 — 시스템을 종료(Finalize)하지 않고 재생 상태만 정리한다.
	// (예: CAudioSystem 이 재생 중인 사운드 인스턴스를 멈추고 해제.) m_isInitialized 는 유지.
	void SimulationStop(CGameCanvas& scene);
	bool IsInitialized() const;
	virtual bool ShouldUpdateInEditMode() const { return false; }

protected:
	virtual void OnInitialize(CGameCanvas& scene) {}
	virtual void OnUpdate(CGameCanvas& scene) {}
	virtual void OnFixedUpdate(CGameCanvas& scene) {}
	virtual void OnFinalize(CGameCanvas& scene) {}
	virtual void OnSimulationStop(CGameCanvas& scene) {}

private:
	bool m_isInitialized = false;
};
