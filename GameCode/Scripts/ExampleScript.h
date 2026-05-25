#pragma once

#include "GameFramework/Scripting/GameScript.h"

// ── ExampleScript ──────────────────────────────────────────────────────────────
// 예제 스크립트입니다. CGameScript를 상속받아 원하는 로직을 구현하세요.
//
// OnCreate()  — 씬에 처음 추가될 때 (한 번)
// OnStart()   — 시뮬레이션 시작 시 (한 번)
// OnUpdate()  — 매 프레임 (시뮬레이션 중)
// OnDestroy() — 씬에서 제거될 때 (한 번)
class ExampleScript : public CGameScript
{
protected:
    void OnCreate()  override;
    void OnStart()   override;
    void OnUpdate()  override;
    void OnDestroy() override;
};
