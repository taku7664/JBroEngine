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

// 레이어가 사는 공간. 카메라를 따르는가, 화면에 못박히는가.
//
// ParallaxFactor 로는 이걸 표현할 수 없다 — 그건 카메라 뷰의 **위치만** 배율하고
// 회전·줌은 카메라를 그대로 물려받기 때문에, factor 0 레이어도 카메라가 줌하면 커지고
// 회전하면 기운다. 투영을 카메라에서 뽑느냐 화면에서 뽑느냐는 보간할 수 없는 이산 선택이라
// float 이 아니라 enum 이다.
enum class ELayerSpace : std::uint8_t
{
	World,    // 기본. 카메라를 따른다(ParallaxFactor 가 적용되는 쪽).
	Screen,   // 카메라 무관 — 위치·회전·줌 전부 독립. UI/HUD 가 이것.
};

// Screen 레이어에서 "1 유닛이 몇 픽셀인가". 표면 종횡비가 기준과 다를 때의 정책이다.
//
// 종횡비 가변은 모바일 전용 문제가 아니다 — Windows 패키지 창이 리사이즈 가능하고
// (RenderSurfaceTypes.h 의 IsResizable 기본 true, 런타임이 덮지 않는다) 데스크톱 패키지엔
// 레터박스가 없다. 그래서 플랫폼 분기가 아니라 전 플랫폼 공용 저작 속성이다.
enum class EScreenScaleMode : std::uint8_t
{
	FixedHeight,     // 기본. 기준 높이 유지 → 가로가 종횡비 따라 늘고 준다.
	FixedWidth,      // 기준 폭 유지 → 세로가 늘고 준다. 세로 게임용.
	Contain,         // 기준 렉트 전체가 항상 보인다(둘 중 넉넉한 쪽). 여백 생김, 잘림 없음.
	ConstantPixel,   // 1 유닛 = 항상 PPU 픽셀. 픽셀아트/픽셀퍼펙트 UI 용.
};

// 직렬화/에디터 공용 — enum ↔ 문자열.
inline const char* ToString(ELayerSpace space)
{
	switch (space)
	{
	case ELayerSpace::Screen: return "Screen";
	case ELayerSpace::World:
	default:                  return "World";
	}
}

inline ELayerSpace LayerSpaceFromString(const char* text)
{
	if (nullptr != text && 0 == std::strcmp(text, "Screen"))
	{
		return ELayerSpace::Screen;
	}
	return ELayerSpace::World;
}

inline const char* ToString(EScreenScaleMode mode)
{
	switch (mode)
	{
	case EScreenScaleMode::FixedWidth:    return "FixedWidth";
	case EScreenScaleMode::Contain:       return "Contain";
	case EScreenScaleMode::ConstantPixel: return "ConstantPixel";
	case EScreenScaleMode::FixedHeight:
	default:                              return "FixedHeight";
	}
}

inline EScreenScaleMode ScreenScaleModeFromString(const char* text)
{
	if (nullptr == text)
	{
		return EScreenScaleMode::FixedHeight;
	}
	if (0 == std::strcmp(text, "FixedWidth"))
	{
		return EScreenScaleMode::FixedWidth;
	}
	if (0 == std::strcmp(text, "Contain"))
	{
		return EScreenScaleMode::Contain;
	}
	if (0 == std::strcmp(text, "ConstantPixel"))
	{
		return EScreenScaleMode::ConstantPixel;
	}
	return EScreenScaleMode::FixedHeight;
}

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
//  CGameLayer — 캔버스(캔버스)의 컴포짓 단위이자 오브젝트 그룹.
//
//  · 트랜스폼 없음 — 순수 컴포짓 속성(순서·블렌드·Opacity·가시성·Parallax)만.
//    순서는 CGameCanvas::m_layers 벡터 순서가 곧 컴포짓(아래→위) 순서다.
//  · 오브젝트 소속은 CGameObject::m_layer(SafePtr) 가 표현한다. 레이어는 소속 목록을
//    들지 않는다 — SetParent/파괴 지점마다 목록을 동기화하는 비용·불일치 버그를 피하고,
//    순회가 필요한 곳(직렬화·실행순서 재빌드·렌더 수집)은 캔버스 전체 1회 순회로 버킷팅한다.
//  · 메모리 소유 = CGameCanvas(OwnerPtr). 외부 공유는 SafePtr.
//  · Visible=false 는 렌더만 끈다(시뮬 계속).
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
	// 캔버스 전환 시 같은 `.jlayer` 에서 온 레이어가 이미 살아 있으면 그 인스턴스를 승계한다
	// (오브젝트·스크립트·Ref 가 무손상으로 넘어온다). false = 파괴 후 파일에서 새로 로드.
	//
	// **수신 캔버스의 참조별 설정**이다 — 같은 레이어라도 캔버스마다 다르게 저작한다(던전B 는
	// 플레이어를 승계, 보스캔버스는 리셋). 그래서 캔버스 파일의 레이어 노드에만 저장하고,
	// SourceAssetGuid 가 null 인 인라인 레이어에는 의미가 없다(승계 대상이 아니다).
	//
	// 기본 false = 승계는 명시 opt-in — 초대장 모델의 취지이자, 기본값이 상태 이월이면
	// 리셋을 원하는 캔버스마다 일일이 꺼야 한다.
	bool            KeepOnCanvasChange = false;
	ELayerBlendMode BlendMode = ELayerBlendMode::Normal;
	float           Opacity = 1.0f;          // 0~1. 컴포짓 시 알파 곱(페이드 연출용).
	bool            Visible = true;          // false = 렌더 제외(시뮬은 계속).
	bool            ForceOwnTexture = false; // true = 항상 RT 경유(차후 레이어 이펙트용).

	// 이 레이어가 사는 공간. Screen 이면 아래 ParallaxFactor 는 의미가 없다(카메라를 아예 안 쓴다).
	ELayerSpace      Space = ELayerSpace::World;
	// Screen 일 때만 읽는다. World 레이어에서는 무시.
	EScreenScaleMode ScaleMode = EScreenScaleMode::FixedHeight;
	// 앵커 기준 렉트를 화면 전체가 아니라 안전영역(노치·펀치홀·제스처바를 제외한 영역)으로.
	// 위젯 단위가 아니라 레이어 단위인 이유: 안전영역은 UI '면'의 성질이지 개별 위젯의 성질이
	// 아니다(배경은 컷아웃까지 덮고 HUD 만 들어와야 하면 레이어를 나눈다 — 순서 때문에 어차피 나눈다).
	// 현재 전 플랫폼 인셋이 0 이라 켜도 꺼도 결과가 같다.
	bool             AnchorToSafeArea = false;

	// World 스페이스 안에서의 카메라 추종 배율. 1=카메라와 동일, 0.5=절반 속도 원경,
	// 0=월드 원점 고정. **화면 고정(UI)은 이 값이 아니라 Space=Screen 이다** — factor 0 은
	// 위치만 멈출 뿐 카메라 줌·회전은 그대로 따라간다.
	float           ParallaxFactor = 1.0f;

	const char* GetName() const { return Name.c_str(); }
	void        SetName(const char* name) { Name = name ? name : ""; }

	// 캔버스 내 순서 = 컴포짓 순서 = 렌더 아이템 정렬 키. 렌더 수집이 오브젝트마다
	// 읽으므로(매 프레임) 캔버스 목록을 선형 탐색하지 않도록 여기 캐시한다. 갱신 주체는
	// CGameCanvas(레이어 생성/파괴/이동 시 ReindexLayers) — 그 외에는 쓰지 않는다.
	std::uint16_t GetIndex() const { return m_index; }

private:
	friend class CGameCanvas;

	std::uint16_t m_index = 0;
};

// 사용자 대면 별칭 — 스크립트/문서는 접두사 없는 Layer 로 쓴다.
using Layer = CGameLayer;
