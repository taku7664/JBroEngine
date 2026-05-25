#pragma once

#include "GameFramework/System/GameSystem.h"

// CTransformSystem runs at the very start of each update tick (before physics
// and rendering) and propagates local Transform2D values down the hierarchy,
// writing the result into each entity's WorldTransform2D component.
//
// After this system runs, every call to GetWorldTransform() in the same frame
// simply reads the cached WorldTransform2D — no parent-chain traversal needed.
//
// ShouldUpdateInEditMode() returns true so the editor scene view also shows
// correct world-space positions while the simulation is paused.
class CTransformSystem final : public CGameSystem
{
public:
	bool ShouldUpdateInEditMode() const override { return true; }

private:
	void OnUpdate(CScene& scene) override;
};
