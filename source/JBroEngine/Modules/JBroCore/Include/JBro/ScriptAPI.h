#pragma once

// 게임 스크립트가 직접 include 하는 단일 프렐류드다.
// 시스템과 호스트 컨텍스트는 의도적으로 이 include 트리에 넣지 않는다.

#include <JBro/Types/Types.h>
#include <JBro/Runtime/Ref.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Runtime/GameObjectHandle.h>
#include <JBro/Runtime/ServiceContext.h>
#include <JBro/Script/Macros.h>

using namespace JBro;
