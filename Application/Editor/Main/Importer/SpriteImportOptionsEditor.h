#pragma once

#include "Engine/Core/Asset/AssetTypes.h"
#include "Engine/Core/Asset/SpriteAsset.h"

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  스프라이트 임포트 옵션 — 편집 중 상태 저장소 + 편집 UI
//
//  인스펙터와 스프라이트 뷰어가 **같은 값을 본다.** 각자 복사본을 들면 한쪽에서 고친 게
//  다른 쪽에서 조용히 사라진다 — 창이 둘일 뿐 값은 하나여야 한다.
//
//  예전에는 인스펙터가 static 슬롯 **하나**로 들고 있었다(선택이 바뀔 때만 재로드).
//  뷰어는 파일당 창이라 여러 개가 동시에 열리므로 슬롯 하나로는 못 받는다 —
//  다른 자산 창을 여는 순간 서로의 편집값을 덮어쓴다. 그래서 guid 별로 나눈다.
//
//  디스크(.jmeta)에는 Apply 를 눌러야 쓴다. 편집 중 값은 저장 전까지 여기에만 있다.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
namespace SpriteImportOptionsEditor
{
	// 편집 중인 값. 처음 보는 guid 면 .jmeta 의 yaml 에서 읽어 온다.
	// 격자를 그리는 쪽(뷰어)이 이걸 읽는다 — 자산이 들고 있는 프레임은 마지막으로 Apply 된
	// 결과라, 슬라이더를 움직이는 동안에는 화면과 어긋난다.
	const SpriteImportOptions& Get(const AssetMetaData& metaData);

	bool IsDirty(const AssetGuid& guid);

	// 슬라이스 모드 / 그리드 / 피벗 / PPU 편집 UI. 값이 바뀌면 저장소를 갱신하고 dirty 를 세운다.
	// 섹션 헤더는 그리지 않는다 — 부르는 쪽이 자기 레이아웃에 맞게 붙인다.
	void DrawEditor(const AssetMetaData& metaData);

	// 슬라이스 결과 요약(프레임 수 / 첫 프레임 크기). 텍스처 크기를 알아야 하므로 분리했다.
	void DrawSliceSummary(const AssetMetaData& metaData);

	// .jmeta 에 저장. dirty 가 아니면 비활성 상태로 그린다.
	void DrawApplyButton(const AssetMetaData& metaData);
}
