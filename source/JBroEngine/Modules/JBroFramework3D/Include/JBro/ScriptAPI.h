#pragma once

// 3D 게임 스크립트가 직접 include 하는 단일 프렐류드다(D-18).
// 경로는 차원과 무관하게 <JBro/ScriptAPI.h> 하나이며, 프로젝트가 고른 차원의 Framework 모듈이
// 그 경로를 제공한다. 시스템·호스트·Canvas 는 의도적으로 이 include 트리에 넣지 않는다.
//
// 3D 의 차원별 서비스는 `GetFramework3DServices()` 다(D-224). 2D 와 같은 자리다.

// 이 아래의 Tier S 헤더들이 "프렐류드를 거쳤다"를 알아보는 표식이다.
#define JBRO_SCRIPT_PRELUDE 1

#include <JBro/Types/Types.h>

#include <JBro/Runtime/GameObjectHandle.h>
// `JBRO_FIELD(GameObject, target)` 가 프렐류드만으로 닿아야 한다(D-233). 컴포넌트 핸들은 핸들 헤더가 리플렉션을 함께 연다(D-302).
#include <JBro/Runtime/GameObjectHandleReflection.h>
#include <JBro/Runtime/GameScriptBase.h>
#include <JBro/Runtime/Ref.h>
#include <JBro/Runtime/ScriptRefReflection.h>
#include <JBro/Runtime/NullableReflection.h>
#include <JBro/Reflection/ContainerTypeDescriptors.h>
// 에디터 `새 스크립트` 가 내놓는 값 필드(`Vector2`·`Vector3`·`Color`·에셋)의 설명서다. 값 타입과 따로 두는 헤더라(D-241)
// 프렐류드가 들이지 않으면 `JBRO_FIELD(Vector3, ...)` 가 설명서 없음으로 멈춘다. 두 프렐류드가 같은 줄을 공유한다.
#include <JBro/Reflection/CoreTypeDescriptors.h>
#include <JBro/Reflection/Math2DReflection.h>
#include <JBro/Reflection/Math3DReflection.h>
#include <JBro/AssetTypes/AssetTypesReflection.h>
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
#include <JBro/Framework3D/ComponentTypes3D.h>
#include <JBro/Framework3D/Handle/Animator3D.h>
#include <JBro/Framework3D/Handle/AudioListener3D.h>
#include <JBro/Framework3D/Handle/BoneAttachment3D.h>
#include <JBro/Framework3D/Handle/Camera3D.h>
#include <JBro/Framework3D/Handle/CharacterController3D.h>
#include <JBro/Framework3D/Handle/Collider3D.h>
#include <JBro/Framework3D/Handle/DistanceJoint3D.h>
#include <JBro/Framework3D/Handle/HingeJoint3D.h>
#include <JBro/Framework3D/Handle/Light3D.h>
#include <JBro/Framework3D/Handle/MeshRenderer3D.h>
#include <JBro/Framework3D/Handle/PostProcessVolume3D.h>
#include <JBro/Framework3D/Handle/Rigidbody3D.h>
#include <JBro/Framework3D/Handle/Text3D.h>
#include <JBro/Framework3D/Handle/Transform3D.h>
#include <JBro/Framework3D/Scripting/GameScript.h>
#include <JBro/Framework3D/ServiceContext.h>
// **화면 레이어의 UI 핸들**(D-363). 3D 캔버스의 화면 레이어는 2D 의 화면 UI 를 쓰므로 그 컴포넌트(트랜스폼·스프라이트·텍스트·위젯)만 핸들로 연다.
// 2D 의 월드 컴포넌트(카메라·라이트·물리·오디오 리스너)와 2D 서비스는 열지 않는다 - 3D 스크립트 SDK 에는 이 줄에서 닿는 2D 헤더만 들어간다.
#include <JBro/Framework2D/ComponentTypes2D.h>
#include <JBro/Framework2D/Handle/AspectRatioFitter2D.h>
#include <JBro/Framework2D/Handle/Button2D.h>
#include <JBro/Framework2D/Handle/CanvasGroup2D.h>
#include <JBro/Framework2D/Handle/ContentSizeFitter2D.h>
#include <JBro/Framework2D/Handle/Draggable2D.h>
#include <JBro/Framework2D/Handle/DropTarget2D.h>
#include <JBro/Framework2D/Handle/Dropdown2D.h>
#include <JBro/Framework2D/Handle/InputField2D.h>
#include <JBro/Framework2D/Handle/Layout2D.h>
#include <JBro/Framework2D/Handle/ListView2D.h>
#include <JBro/Framework2D/Handle/Mask2D.h>
#include <JBro/Framework2D/Handle/Modal2D.h>
#include <JBro/Framework2D/Handle/Navigation2D.h>
#include <JBro/Framework2D/Handle/SafeArea2D.h>
#include <JBro/Framework2D/Handle/ScrollView2D.h>
#include <JBro/Framework2D/Handle/Slider2D.h>
#include <JBro/Framework2D/Handle/SpriteRenderer2D.h>
#include <JBro/Framework2D/Handle/TabGroup2D.h>
#include <JBro/Framework2D/Handle/Text2D.h>
#include <JBro/Framework2D/Handle/Toggle2D.h>
#include <JBro/Framework2D/Handle/ToggleGroup2D.h>
#include <JBro/Framework2D/Handle/Tooltip2D.h>
#include <JBro/Framework2D/Handle/Transform2D.h>
#include <JBro/Framework2D/Handle/Tween2D.h>
#include <JBro/Framework2D/Handle/VirtualKeyboard2D.h>
#include <JBro/Types/Math3D.h>

using namespace JBro;
// 스크립트가 보는 참조는 엔진 타입과 이름이 같은 핸들이다(D-302). `GameObject` 는 `JBro::Handle::GameObject` 로 풀린다.
using namespace JBro::Handle;
