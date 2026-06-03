#pragma once

#include "Engine/Core/Asset/AssetTypes.h"     // AssetGuid
#include "Engine/Core/Asset/AudioEffectAsset.h" // AudioEffectData

#include <string>
#include <vector>

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  CEffectEditorWidget ─ 사운드 효과(.jfx) 에디터 위젯
//
//  Kind 콤보 + Kind별 파라미터 슬라이더를 그리고, 변경 시 .jfx 파일에 저장한다.
//  상태(현재 데이터/dirty)를 인스턴스 멤버로 보유하므로 여러 에디터 창이 동시에
//  떠 있어도 충돌하지 않는다 (static 캐시가 아님).
//
//  에셋 인스펙터가 아니라 전용 독윈도우(CReverbEditorWindow)에서 사용된다.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
class CEffectEditorWidget
{
public:
	// 편집 대상 효과 에셋 GUID. 디스크 .jfx 를 읽어 현재 데이터를 로드한다.
	void SetTargetGuid(const AssetGuid& guid);
	const AssetGuid& GetTargetGuid() const { return m_guid; }

	// 매 프레임 UI 그리기 (ImGui 컨텍스트 안에서 호출).
	void Draw();

private:
	void LoadFromDisk();
	bool SaveToDisk();
	void DrawPreview();   // 테스트 사운드 드롭 + 효과 적용 재생.

	AssetGuid       m_guid = INVALID_ASSET_GUID;
	AudioEffectData m_data;
	bool            m_loaded = false;
	bool            m_dirty  = false;

	AssetGuid       m_testSoundGuid = INVALID_ASSET_GUID;   // 미리듣기 테스트 사운드
};
