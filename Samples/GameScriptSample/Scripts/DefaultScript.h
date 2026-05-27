#pragma once

#include "GameFramework/Scripting/ScriptAPI.h"

// ── CDefaultScript ────────────────────────────────────────────────────────────
// 새 스크립트를 만들 때 이 파일을 복사해서 시작하세요.
//
// SCRIPT_CLASS(T) — 클래스 최상단에 한 번 선언합니다.
// REFLECT_FIELD   — Inspector 에 노출하고 씬 저장/핫리로드에서 값이 유지됩니다.
// 일반 멤버 변수  — Inspector 미노출, 핫리로드 시 초기화됩니다.
class CDefaultScript final : public CGameScript
{
	SCRIPT_CLASS(CDefaultScript)

protected:
	void OnCreate()      override;
	void OnStart()       override;
	void OnUpdate()      override;
	void OnFixedUpdate() override;
	void OnDestroy()     override;
};
