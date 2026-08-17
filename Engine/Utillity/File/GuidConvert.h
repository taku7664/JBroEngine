#pragma once

#include "Utillity/File/FilePath.h"
#include "Utillity/File/Guid128.h"

// ─────────────────────────────────────────────────────────────────────────────
//  File::Guid(fs::path) ↔ Guid128 변환.
//
//  Guid128.h 는 <filesystem> 을 모르는 POD 전용 헤더로 남겨 둔다(호스트↔게임 DLL 경계에
//  그대로 실리는 타입이라 의존을 늘리지 않는다). 두 타입을 모두 아는 코드가 쓰는 다리를
//  여기 한 곳에 둔다 — 예전에는 Canvas.cpp 가 같은 함수를 익명 네임스페이스에 들고 있었다.
//
//  ⚠ 변환은 cold path 전용이다. guid 문자열은 ASCII hex 라 fs::path::string() 왕복이
//    안전하지만, 그 호출이 곧 UTF16→UTF8 트랜스코딩 + 힙 할당이다. 조회마다 부르지 말고
//    **값이 정해질 때 한 번** 접어서 Guid128 로 들고 다닐 것.
// ─────────────────────────────────────────────────────────────────────────────
inline Guid128 ToGuid128(const File::Guid& guid)
{
	return Guid128::FromText(guid.string().c_str());
}
