#pragma once

#include "GameFramework/Object/GameInstance.h"
#include "Utillity/File/FilePath.h"
#include "Utillity/Pointer/SafePtr.h"

#include <cstdint>
#include <cstring>
#include <string>

// 레이어 컴포짓 블렌드 — 포토샵식 기본셋. 렌더러가 RHI 블렌드로 매핑한다
// (GameFramework 는 RHI 타입을 직접 알지 않는다).
enum class ELayerBlendMode : std::uint8_t
{
	Normal,
	Additive,
	Multiply,
	Screen,
};

// 직렬화/에디터 공용 — enum ↔ 문자열.
inline const char* ToString(ELayerBlendMode mode)
{
	switch (mode)
	{
	case ELayerBlendMode::Additive: return "Additive";
	case ELayerBlendMode::Multiply: return "Multiply";
	case ELayerBlendMode::Screen:   return "Screen";
	case ELayerBlendMode::Normal:
	default:                        return "Normal";
	}
}

inline ELayerBlendMode LayerBlendModeFromString(const char* text)
{
	if (nullptr == text)
	{
		return ELayerBlendMode::Normal;
	}
	if (0 == std::strcmp(text, "Additive"))
	{
		return ELayerBlendMode::Additive;
	}
	if (0 == std::strcmp(text, "Multiply"))
	{
		return ELayerBlendMode::Multiply;
	}
	if (0 == std::strcmp(text, "Screen"))
	{
		return ELayerBlendMode::Screen;
	}
	return ELayerBlendMode::Normal;
}

// ─────────────────────────────────────────────────────────────────────────────
//  CGameLayer — 캔버스(씬)의 컴포짓 단위이자 오브젝트 그룹.
//
//  · 트랜스폼 없음 — 순수 컴포짓 속성(순서·블렌드·Opacity·가시성·Static·Parallax)만.
//    순서는 CGameScene::m_layers 벡터 순서가 곧 컴포짓(아래→위) 순서다.
//  · 오브젝트 소속은 CGameObject::m_layer(SafePtr) 가 표현한다. 레이어는 소속 목록을
//    들지 않는다 — SetParent/파괴 지점마다 목록을 동기화하는 비용·불일치 버그를 피하고,
//    순회가 필요한 곳(직렬화·실행순서 재빌드·렌더 수집)은 씬 전체 1회 순회로 버킷팅한다.
//  · 메모리 소유 = CGameScene(OwnerPtr). 외부 공유는 SafePtr.
//  · Visible=false 는 렌더만 끈다(시뮬 계속). Static 은 렌더 동결만(시뮬은 유저 책임).
// ─────────────────────────────────────────────────────────────────────────────
class CGameLayer final : public GameInstance, public EnableSafeFromThis<CGameLayer>
{
public:
	CGameLayer() = default;
	CGameLayer(const char* name, const File::Guid& instanceGuid)
		: GameInstance(instanceGuid)
		, Name(name ? name : "Layer")
	{
	}

	CGameLayer(const CGameLayer&) = delete;
	CGameLayer& operator=(const CGameLayer&) = delete;

	std::string     Name;
	// 이 레이어가 나온 `.jlayer` 에셋. null = 캔버스에 인라인으로만 존재하는 레이어.
	// 캔버스 파일의 레이어 노드에만 저장한다 — 레이어 파일은 자기 자신을 참조하지 않는다.
	File::Guid      SourceAssetGuid;
	ELayerBlendMode BlendMode = ELayerBlendMode::Normal;
	float           Opacity = 1.0f;          // 0~1. 컴포짓 시 알파 곱(페이드 연출용).
	bool            Visible = true;          // false = 렌더 제외(시뮬은 계속).
	bool            Static = false;          // true = RT 1회 그린 뒤 재그리기 스킵(렌더 동결).
	bool            ForceOwnTexture = false; // true = 항상 RT 경유(차후 레이어 이펙트용).
	float           ParallaxFactor = 1.0f;   // 1=월드, 0=뷰포트 고정(UI), 0.5=원경 패럴랙스.

	const char* GetName() const { return Name.c_str(); }
	void        SetName(const char* name) { Name = name ? name : ""; }

	// 캔버스 내 순서 = 컴포짓 순서 = 렌더 아이템 정렬 키. 렌더 수집이 오브젝트마다
	// 읽으므로(매 프레임) 씬 목록을 선형 탐색하지 않도록 여기 캐시한다. 갱신 주체는
	// CGameScene(레이어 생성/파괴/이동 시 ReindexLayers) — 그 외에는 쓰지 않는다.
	std::uint16_t GetIndex() const { return m_index; }

private:
	friend class CGameScene;

	std::uint16_t m_index = 0;
};

// 사용자 대면 별칭 — 스크립트/문서는 접두사 없는 Layer 로 쓴다.
using Layer = CGameLayer;
