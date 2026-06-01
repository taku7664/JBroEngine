#pragma once

#include "Utillity/Math/Matrix3x2.h"

// Cached world-space transform for an entity.
// Populated every frame by CTransformSystem before physics and rendering.
// Read via GetWorldTransform() in SceneTransformUtils.h.
struct WorldTransform2D
{
	Matrix3x2 Matrix = Matrix3x2::Identity();
};
