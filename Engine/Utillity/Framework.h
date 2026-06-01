#pragma once
#define WIN32_LEAN_AND_MEAN             // Exclude rarely used Windows headers.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <utility>
#include <string>
#include <string_view>
#include <format>
#include <source_location>
#include <stdexcept>
#include <filesystem>
// 컨테이너 — SDK 사용자(게임 스크립트 DLL) 가 SDK 헤더만으로 빌드 가능하도록
// 표준 컨테이너 include 를 SDK 측 Framework.h 에서 미리 보장한다.
// (Engine 내부 PCH 인 Engine/Framework.h 와는 별도 — 그쪽은 엔진 빌드용.)
#include <vector>
#include <array>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <queue>
#include <deque>
#include <functional>
#include <type_traits>
#include <memory>

#include "Utillity/Base/Defines.h"
#include "Utillity/Types/Types.h"
#include "Utillity/Types/EngineTypes.h"
#include "Utillity/File/FileUtillities.h"
#include "Utillity/Pointer/SafePtr.h"
#include "Utillity/Base/Utillities.h"
