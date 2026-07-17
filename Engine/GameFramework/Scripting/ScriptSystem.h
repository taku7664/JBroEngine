#pragma once

#include "GameFramework/System/GameSystem.h"

// 스크립트만 라이프사이클을 가진다. 실행 순서는 컴포넌트 타입 풀 순서가 아니라
// GameObject 가 보유한 단일 컴포넌트 리스트의 표시 순서다.
class CScriptSystem final : public CGameSystem
{
protected:
	void OnUpdate(CGameCanvas& scene) override;
	void OnFixedUpdate(CGameCanvas& scene) override;
};
