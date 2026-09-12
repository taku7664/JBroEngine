#pragma once

// 3D 게임 스크립트가 직접 include 하는 단일 프렐류드다(D-18).
// 경로는 차원과 무관하게 <JBro/ScriptAPI.h> 하나이며, 프로젝트가 고른 차원의 Framework 모듈이
// 그 경로를 제공한다. 시스템·호스트·Canvas 는 의도적으로 이 include 트리에 넣지 않는다.
//
// 3D 는 아직 시스템과 서비스가 없어 차원별 Context 를 노출하지 않는다.
// 생기면 2D 와 같은 자리에서 ServiceContext 를 추가한다.

#include <JBro/Types/Types.h>

#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Runtime/GameScriptBase.h>
#include <JBro/Runtime/Ref.h>
#include <JBro/Runtime/ServiceContext.h>
#include <JBro/Script/Macros.h>

#include <JBro/Framework3D/Component/Camera3D.h>
#include <JBro/Framework3D/Component/MeshRenderer3D.h>
#include <JBro/Framework3D/Component/Physics3D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Framework3D/Math3D.h>

using namespace JBro;
