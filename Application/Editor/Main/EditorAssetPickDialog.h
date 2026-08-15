#pragma once

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR

#include "Engine/Core/Asset/AssetTypes.h"     // AssetMetaData, EAssetType
#include "Utillity/File/FileUtillities.h"     // File::FileDialogFilter

#include <vector>

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  전용 에디터 창(스프라이트 뷰어 · 사운드 효과 에디터)의 "파일 열기" 공용 경로.
//
//  이 창들은 전부 **guid** 로 돈다. 그러니 파일을 고르는 것만으로는 부족하고,
//  고른 파일이 프로젝트 자산으로 **등록돼 있어야** 한다. 세 가지를 한 번에 처리한다.
//    1. 자산 폴더에서 시작하는 파일 다이얼로그
//    2. 자산 폴더 안인지 + 레지스트리에 있는지 + 기대한 타입인지 검사
//    3. 걸리면 이유를 담은 안내 팝업 (호출부마다 팝업을 다시 짜지 않게)
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
namespace EditorAssetPick
{
	// 성공하면 outMetaData 를 채우고 true.
	// 사용자가 취소하면 조용히 false. 그 밖의 실패는 안내 팝업을 띄우고 false.
	bool PickRegisteredAsset(
		const char* titleLocKey,
		std::vector<File::FileDialogFilter> filters,
		EAssetType expectedType,
		AssetMetaData& outMetaData);
}

#endif
