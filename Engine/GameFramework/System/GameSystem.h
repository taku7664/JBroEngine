#pragma once

class CGameCanvas;

// Game systems are owned by CGameCanvas inside GameScript. Editor/live-compile code
// must not retain system pointers across DLL reloads.
class CGameSystem
{
public:
	virtual ~CGameSystem() = default;

public:
	void Initialize(CGameCanvas& canvas);
	void Update(CGameCanvas& canvas);
	void FixedUpdate(CGameCanvas& canvas);
	void Finalize(CGameCanvas& canvas);
	// 시뮬레이션 정지 시 호출 — 시스템을 종료(Finalize)하지 않고 재생 상태만 정리한다.
	// (예: CAudioSystem 이 재생 중인 사운드 인스턴스를 멈추고 해제.) m_isInitialized 는 유지.
	void SimulationStop(CGameCanvas& canvas);
	bool IsInitialized() const;
	virtual bool ShouldUpdateInEditMode() const { return false; }

protected:
	virtual void OnInitialize(CGameCanvas& canvas) {}
	virtual void OnUpdate(CGameCanvas& canvas) {}
	virtual void OnFixedUpdate(CGameCanvas& canvas) {}
	virtual void OnFinalize(CGameCanvas& canvas) {}
	virtual void OnSimulationStop(CGameCanvas& canvas) {}

private:
	bool m_isInitialized = false;
};
