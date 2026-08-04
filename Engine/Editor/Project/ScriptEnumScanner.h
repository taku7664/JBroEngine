#pragma once

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  ScriptEnumScanner ─ 게임 스크립트 폴더의 `enum class` 목록 스캐너
//
//  두 곳이 같은 목록을 봐야 한다:
//    · 코드 생성기(GameScriptProjectGenerator) — enum 이름표 테이블을 뱉는다.
//    · 저작 UI(ScriptSchema) — 프로퍼티 타입 2차 콤보를 채운다.
//  둘이 각자 스캔하면 규칙이 갈라져 "코드는 되는데 에디터엔 안 뜨는" 상태가 생기므로
//  스캔은 여기 하나만 둔다.
//
//  값은 담지 않는다. 생성 코드가 `static_cast<std::int64_t>(EFoo::Bar)` 로 적어
//  컴파일러에게 계산시키므로, `= 1 << 3` 같은 초기화식을 파서가 해석할 필요가 없다.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

#include <filesystem>
#include <string>
#include <vector>

struct ScriptEnumInfo
{
	std::string              ClassName;    // "EPlayerState"
	std::vector<std::string> Enumerators;  // { "Idle", "Run", ... }
};

// scriptRoot 아래의 .h / .hpp 를 훑어 전역 `enum class` 를 모은다.
// 네임스페이스 안의 enum 은 이름이 수식되지 않아 `Ns::X` 표기로는 해석되지 않는다.
// 이름이 겹치면 처음 것만 남긴다(같은 이름 두 개는 어차피 C++ 에서도 충돌한다).
std::vector<ScriptEnumInfo> ScanScriptEnums(const std::filesystem::path& scriptRoot);
