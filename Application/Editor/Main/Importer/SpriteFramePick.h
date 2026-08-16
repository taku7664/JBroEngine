#pragma once

#include "Engine/Core/Asset/AssetTypes.h"   // AssetGuid
#include "Utillity/File/FilePath.h"         // File::Guid

#include <cstdint>

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  스프라이트 프레임 고르기 ─ 인스펙터의 FrameIndex 를 숫자로 치는 대신
//  시트 뷰어에서 칸을 눌러 고른다.
//
//    인스펙터 ──Begin()──▶ 뷰어(고르기 모드) ──SetResult()──▶ 인스펙터 TryTakeResult()
//
//  **뷰어가 값을 직접 쓰지 않는다.** 쓰기는 undo 커맨드와 경계 캐시 무효화를 타야 하고,
//  그러려면 캔버스·오브젝트·프로퍼티가 그 순간 전부 살아 있어야 한다. 그 맥락을 프레임 너머로
//  들고 다니면(포인터든 콜백이든) 그 사이에 오브젝트가 지워질 수 있다.
//  그래서 뷰어는 **고른 숫자만** 남기고, 그 컴포넌트를 그리고 있는 인스펙터가 가져가 쓴다.
//  인스펙터가 그리고 있다는 것 자체가 대상이 살아 있다는 증거다.
//
//  요청은 한 번에 하나다 — 고르는 동안 다른 컴포넌트로 옮겨 가는 흐름은 없다.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
namespace SpriteFramePick
{
	// 요청 시작. 해당 시트의 뷰어 탭을 열고 앞으로 가져온다.
	void Begin(const AssetGuid& sheetGuid, const File::Guid& requesterComponentGuid);

	// 이 시트가 지금 고르기 대기 중인가 — 뷰어가 안내와 클릭 처리를 켜는 조건.
	bool IsWaitingFor(const AssetGuid& sheetGuid);

	// 이 컴포넌트가 낸 요청이 대기 중인가 — 버튼을 "취소"로 바꾸는 조건.
	bool IsPendingFor(const File::Guid& requesterComponentGuid);

	// 뷰어가 칸을 클릭했을 때.
	void SetResult(std::uint32_t frameIndex);

	// 인스펙터가 매 프레임 확인. 자기 결과가 있으면 꺼내 가고 요청을 지운다.
	bool TryTakeResult(const File::Guid& requesterComponentGuid, std::uint32_t& outFrameIndex);

	void Cancel();
}
