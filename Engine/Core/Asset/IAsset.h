#pragma once

#include "Core/Asset/AssetTypes.h"
#include "Utillity/Pointer/SafePtr.h"

#include <string>

class IAsset : public EnableSafeFromThis<IAsset>
{
public:
	virtual ~IAsset() = default;

public:
	virtual AssetGuid GetGuid() const = 0;
	virtual EAssetType GetAssetType() const = 0;
	virtual EAssetLoadState GetLoadState() const = 0;
	virtual const AssetMetaData& GetMetaData() const = 0;

	// 임포트 옵션 YAML 텍스트를 받아 자산 내부 상태를 in-place 갱신한다.
	// 자산 객체는 destroy 되지 않으므로 외부 SafePtr 가 살아남는다 — 씬/인스펙터 미리듣기 등
	// 자산을 사용 중인 곳이 깨지지 않는다.
	// 자산 타입은 자기 ImportOptions 파싱 + 내부 상태 갱신 + (필요 시) GPU/디코드 자원 in-place 교체를 책임진다.
	// 기본 구현은 no-op — 옵션 개념이 없는 자산은 override 불필요.
	virtual void ApplyImportOptions(const std::string& importOptionsYaml) { (void)importOptionsYaml; }
};

