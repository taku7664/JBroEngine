#pragma once

// 2D 게임 스크립트가 직접 include 하는 단일 프렐류드다(D-18).
// 경로는 차원과 무관하게 <JBro/ScriptAPI.h> 하나이며, 프로젝트가 고른 차원의 Framework 모듈이
// 그 경로를 제공한다. 시스템·호스트·Canvas 는 의도적으로 이 include 트리에 넣지 않는다.

// 이 아래의 Tier S 헤더들이 "프렐류드를 거쳤다"를 알아보는 표식이다.
#define JBRO_SCRIPT_PRELUDE 1

#include <JBro/Types/Types.h>

#include <JBro/Runtime/GameObjectHandle.h>
// `JBRO_FIELD(GameObject, target)` 가 프렐류드만으로 닿아야 한다(D-233). 컴포넌트 핸들은 핸들 헤더가 리플렉션을 함께 연다(D-302).
#include <JBro/Runtime/GameObjectHandleReflection.h>
#include <JBro/Runtime/Ref.h>
#include <JBro/Runtime/ServiceContext.h>
#include <JBro/Script/Macros.h>
// 네트워크 값 서비스(D-122). 차원과 무관하므로 두 프렐류드가 같은 줄을 공유한다.
#include <JBro/Network/ServiceContext.h>
// 오디오 값 서비스와 소스(D-197). 차원과 무관하므로 두 프렐류드가 같은 줄을 공유한다.
#include <JBro/AudioTypes/Handle/AudioSource.h>
#include <JBro/AudioTypes/ServiceContext.h>
// 게임 입력(D-214). 키 이름·장치 상태와 `GetInputServices().Input` 이다. 이것도 두 프렐류드가 같은 줄을 공유한다.
#include <JBro/InputTypes/InputBuffer.h>
#include <JBro/InputTypes/InputHandler.h>
#include <JBro/InputTypes/ServiceContext.h>
#include <JBro/LocalizationTypes/ServiceContext.h>
#include <JBro/SaveTypes/ServiceContext.h>

// 빌트인 컴포넌트는 핸들로만 보인다(D-302). 데이터 헤더(`Component/*.h`)는 스크립트 대상 빌드에서 `#error` 다.
#include <JBro/Framework2D/ComponentTypes2D.h>
#include <JBro/Framework2D/Handle/AudioListener2D.h>
#include <JBro/Framework2D/Handle/Button2D.h>
#include <JBro/Framework2D/Handle/Camera2D.h>
#include <JBro/Framework2D/Handle/Collider2D.h>
#include <JBro/Framework2D/Handle/DistanceJoint2D.h>
#include <JBro/Framework2D/Handle/HingeJoint2D.h>
#include <JBro/Framework2D/Handle/Light2D.h>
#include <JBro/Framework2D/Handle/Rigidbody2D.h>
#include <JBro/Framework2D/Handle/ShadowCaster2D.h>
#include <JBro/Framework2D/Handle/SpriteRenderer2D.h>
#include <JBro/Framework2D/Handle/Text2D.h>
#include <JBro/Framework2D/Handle/Transform2D.h>
#include <JBro/Framework2D/Layer2D.h>
#include <JBro/Framework2D/Prefab/Prefab.h>
#include <JBro/Framework2D/Scripting/GameScript.h>
#include <JBro/Framework2D/ServiceContext.h>
#include <JBro/Types/Math2D.h>

using namespace JBro;
// 스크립트가 보는 참조는 엔진 타입과 이름이 같은 핸들이다(D-302). `GameObject` 는 `JBro::Handle::GameObject`, `Transform2D` 는 `JBro::Handle::Transform2D` 로 풀린다.
using namespace JBro::Handle;
