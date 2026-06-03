#pragma once

// ⚠ DEPRECATED — 옛 `struct GameObject`(Name/IsActive/Layer/InstanceGuid 보유 컴포넌트)는
// 다형성 전환으로 폐지되었다. 해당 데이터는 이제 실체 객체 `CGameObject`
// (GameFramework/Object/GameObject.h)의 멤버다.
//
// 이 헤더는 인클루드 경로 호환을 위해 한시적으로 남겨둔 스텁이다.
// `GameObject` 타입을 참조하던 코드는 `CGameObject` 사용으로 교체할 것.
// (전환 완료 후 이 파일과 모든 #include 를 제거한다.)

#include "GameFramework/Object/GameObject.h"
