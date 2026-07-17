#pragma once

#include <string>

// ─────────────────────────────────────────────────────────────────────────────
//  저장된 ImGui ini(창 배치) 안의 **창 안정 ID** 를 옮기는 마이그레이션.
//
//  창 ID 는 ImGui 도킹의 키다. 코드에서 창 이름을 바꾸면 저장된 배치가 그 창을 찾지 못해
//  도킹이 통째로 풀린다 — ini 는 `[Window][<id>]` / `Selected=<hash>` 로 옛 이름을 들고 있고,
//  ImGui 는 그게 어느 창이었는지 알 방법이 없다.
//
//  `.jproject` 의 다른 키(StartupScene → StartupCanvas 등)에 legacy 리더를 달아 준 것과 같은
//  이유로 여기도 옮겨 준다. 사용자가 짜 둔 창 배치는 사용자의 자산이고, 우리 쪽 리네임 때문에
//  날아갈 이유가 없다.
// ─────────────────────────────────────────────────────────────────────────────
namespace EditorImGuiIni
{
	// ini 텍스트의 `[Window][oldId]` 를 `[Window][newId]` 로 바꾸고, 도킹 노드가 창을 지목하는
	// `Selected=0x...` 해시도 새 ID 의 해시로 갱신한다.
	// 이미 새 ID 로 저장돼 있으면 아무것도 하지 않는다(멱등).
	std::string MigrateWindowId(const std::string& iniText, const char* oldId, const char* newId);

	// 이 엔진이 지금까지 옮긴 창 ID 를 전부 적용한다. 로드 직후 1회 호출.
	std::string MigrateKnownWindowIds(const std::string& iniText);
}
