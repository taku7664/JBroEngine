#pragma once

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  GameFrameworkAll.h ─ 엔진/에디터 내부에서 쓰는 GameFramework 묶음 umbrella
//
//  엔진/에디터 cpp 가 "씬 + 컴포넌트 + 시스템" 을 동시에 만지는 경우가 많아
//  이 헤더 하나로 자주 쓰이는 헤더들을 한꺼번에 가져온다.
//
//  사용 예 (Editor / Engine 내부):
//    #include "GameFramework/GameFrameworkAll.h"
//
//  스크립트 사용자는 이 헤더가 아니라 ScriptAPI.h 를 쓰면 된다.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

// ── ECS / Scene ─────────────────────────────────────────────────────────────
#include "GameFramework/ECS/EntityTypes.h"
#include "GameFramework/ECS/EntityManager.h"
#include "GameFramework/ECS/ComponentPool.h"
#include "GameFramework/System/GameSystem.h"
#include "GameFramework/Scene/SceneTypes.h"
#include "GameFramework/Scene/SceneSerializer.h"
#include "GameFramework/Scene/SceneManager.h"
#include "GameFramework/Scene/Scene.h"

// ── 자주 쓰이는 컴포넌트들 ────────────────────────────────────────────────
#include "GameFramework/Component/GameObject.h"
#include "GameFramework/Component/Transform2D.h"
#include "GameFramework/Component/TransformHierarchy2D.h"
#include "GameFramework/Component/WorldTransform2D.h"
#include "GameFramework/Component/SpriteRenderer2D.h"
#include "GameFramework/Component/Camera2D.h"
#include "GameFramework/Component/Light2D.h"
#include "GameFramework/Component/Physics2DComponents.h"
#include "GameFramework/Component/PrefabInstance.h"
#include "GameFramework/Component/ScriptComponent.h"

// ── 렌더 / 변환 시스템 ──────────────────────────────────────────────────
#include "GameFramework/Rendering/SpriteRenderSystem.h"
#include "GameFramework/Transform/TransformSystem.h"

// ── 프리팹 / 리플렉션 / 스크립팅 ────────────────────────────────────────
#include "GameFramework/Prefab/PrefabTypes.h"
#include "GameFramework/Prefab/PrefabSerializer.h"
#include "GameFramework/Reflection/ReflectionRegistry.h"
#include "GameFramework/Scripting/GameScript.h"
#include "GameFramework/Scripting/ScriptSystem.h"

// ── 디버그 / 물리 ───────────────────────────────────────────────────────
#include "GameFramework/Debug/SceneDebugDrawSystem.h"
#include "GameFramework/Physics2D/Physics2DTypes.h"
#include "GameFramework/Physics2D/Physics2DGeometry.h"
#include "GameFramework/Physics2D/Physics2DSystem.h"
