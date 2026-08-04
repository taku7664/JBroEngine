#pragma once

#include "Utillity/File/FilePath.h"

#include <string>

// ─────────────────────────────────────────────────────────────────────────────
//  UserData — 게임이 **쓸 수 있는** 유일한 뿌리.
//
//  설치 경로가 아니라 사용자 프로필 아래다. Program Files 밑은 쓰기 권한이 없을 수 있고,
//  에셋 루트는 패키지 빌드에서 팩 파일이며 웹에서는 새로고침하면 사라지는 MEMFS 이미지다.
//
//  세이브와 로그가 같은 결정을 두 번 내리지 않도록 여기 한 벌만 둔다
//  (뿌리 아래 `Saves` / `Logs` 로 갈린다).
// ─────────────────────────────────────────────────────────────────────────────
namespace UserData
{
	// 제품명을 파일 이름으로 쓸 수 있게 다듬는다. 사람이 프로젝트 설정에 적는 값이라
	// 공백·기호가 섞일 수 있는데, 그대로 경로에 붙이면 플랫폼마다 다르게 깨진다.
	// 빈 이름이면 기본 이름을 쓴다(실제 게임 세이브와 섞이지 않도록 구분된 이름).
	std::string SanitizeProductName(const char* productName);

	// 플랫폼별 쓰기 가능 뿌리. 실패하면 빈 경로.
	//   · Windows — `%LOCALAPPDATA%\<제품명>`
	//   · Web     — `/UserData` (IDBFS 마운트. 오리진 단위로 갈리므로 제품명을 붙이지 않는다)
	//   · 그 외    — `$HOME/.local/share/<제품명>`
	File::Path ResolveRoot(const char* productName);
}
