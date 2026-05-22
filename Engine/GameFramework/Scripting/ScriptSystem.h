#pragma once

#include "GameFramework/System/GameSystem.h"

class CScriptSystem final : public CGameSystem
{
protected:
	void OnUpdate(CScene& scene) override;
};
