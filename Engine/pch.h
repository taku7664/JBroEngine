#ifndef PCH_H
#define PCH_H

// Add headers to precompile here.
#include "Framework.h"

// Engine.lib 내부 cpp 가 Core 서비스/씬을 광범위하게 만지므로 umbrella 두 줄로 편의 제공.
// SDK 외부 빌드(GameScriptSample 등)는 별도 pch 라 영향 없음.
// 핵심 슬림화 효과는 Engine/Framework.h 에서 Core/GameFramework 직접 include 가 모두
// 빠졌다는 점에 있다 — umbrella 는 "묶음 한 줄" 이라 의존성 변경 시 영향 범위가 명확.
#include "Core/CoreAll.h"
#include "GameFramework/GameFrameworkAll.h"

#endif // PCH_H
