#pragma once

// 2D 게임 스크립트가 직접 include 하는 단일 프렐류드다(D-18).
// 경로는 차원과 무관하게 <JBro/ScriptAPI.h> 하나이며, 프로젝트가 고른 차원의 Framework 모듈이
// 그 경로를 제공한다. 시스템·호스트·Canvas 는 의도적으로 이 include 트리에 넣지 않는다.

// 이 아래의 Tier S 헤더들이 "프렐류드를 거쳤다"를 알아보는 표식이다.
#define JBRO_SCRIPT_PRELUDE 1

#include <JBro/Types/Types.h>

#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Runtime/Ref.h>
#include <JBro/Runtime/ServiceContext.h>
#include <JBro/Script/Macros.h>
// 네트워크 값 서비스(D-122). 차원과 무관하므로 두 프렐류드가 같은 줄을 공유한다.
#include <JBro/Network/ServiceContext.h>
// 오디오 값 서비스와 소스(D-197). 차원과 무관하므로 두 프렐류드가 같은 줄을 공유한다.
#include <JBro/AudioTypes/Component/AudioSource.h>
#include <JBro/AudioTypes/ServiceContext.h>

#include <JBro/Framework2D/Component/AudioListener2D.h>
#include <JBro/Framework2D/Component/Camera2D.h>
#include <JBro/Framework2D/Component/Physics2D.h>
#include <JBro/Framework2D/Component/SpriteRenderer2D.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework2D/Layer2D.h>
#include <JBro/Framework2D/Math2D.h>
#include <JBro/Framework2D/Prefab/Prefab.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
#include <JBro/Framework2D/ServiceContext.h>

using namespace JBro;
