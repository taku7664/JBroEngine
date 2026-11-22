#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Core/Core.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Color.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Uuid.h>

#include <cstdint>

namespace JBro
{
    // **환경광을 어디서 받나**(D-447, lighting3d-plan §6). 차례가 `.jcanvas` 의 값이다 - 바꾸면 파일을 옮긴다.
    enum class EnvironmentAmbientSource : std::uint8_t
    {
        // 한 색이 모든 방향에서 온다.
        Color,
        // 위(하늘)·옆(지평선)·아래(땅) 세 색을 법선의 높이로 섞는다(Unity 의 Gradient 와 같은 모양).
        Gradient,
        // 하늘 맵(`skyId`)을 방향마다 적분한 빛이다(SH 9 계수).
        Sky
    };
    inline constexpr std::uint8_t EnvironmentAmbientSourceCount = 3;

    // **캔버스 하나의 환경**(D-447, 사용자 결정 "캔버스 환경 설정", Unity 의 Lighting 창 Environment 와 같은 자리).
    // 3D 캔버스에서 표준 머티리얼이 받는 환경광·반사와 하늘 배경이다. 2D 는 보지 않는다.
    // 기본값은 이것이 생기기 전과 같은 그림이다 - 검은 `Color` 라 아무 빛도 더하지 않고, 하늘이 없어 배경도 지우기 색 그대로다.
    // `Light3D` 의 `Ambient` 는 그대로 있고 이 빛에 더해진다.
    struct CanvasEnvironment
    {
        EnvironmentAmbientSource ambientSource = EnvironmentAmbientSource::Color;
        // `Color` 의 색이다(선형).
        Color ambientColor{0.0f, 0.0f, 0.0f, 1.0f};
        // `Gradient` 의 세 색이다(선형, Unity 새 씬의 기본과 같은 값).
        Color skyColor{0.212f, 0.227f, 0.259f, 1.0f};
        Color equatorColor{0.114f, 0.125f, 0.133f, 1.0f};
        Color groundColor{0.047f, 0.043f, 0.035f, 1.0f};
        // 환경광(확산)에 곱한다. 0..8 이다.
        Float ambientIntensity = 1.0f;
        // **하늘 맵**(`.hdr` 등장방형 그림, `AssetType::EnvironmentMap`). 있으면 `Sky` 환경광·반사의 출처이고, 카메라의 배경이 `Sky` 면 그 뒤에 그린다.
        Uuid skyId;
        // 하늘 맵의 밝기다(배경·환경광·반사 모두). 0..8 이다.
        Float skyIntensity = 1.0f;
        // 하늘을 세로축(+Y)으로 돌린다(도).
        Float skyRotation = 0.0f;
        // 반사(거울 쪽 빛)에 곱한다. 0..1 이다. `Color`·`Gradient` 는 그 색을, `Sky` 는 하늘 맵을 비춘다.
        Float reflectionIntensity = 1.0f;
        // **풀린 하늘 맵이다.** 파일에 적지 않는다. 프레임워크가 캔버스 에셋을 묶을 때(`BindCanvasAssets`) `skyId` 로 채운다.
        AssetHandle sky;

        // 기본값과 같은가 - `.jcanvas` 는 기본이 아닐 때만 `Environment` 를 적는다(옛 파일과 글자까지 같다).
        Bool IsDefault() const;
        // 풀린 핸들을 빼고 같은가.
        Bool SameSettings(const CanvasEnvironment& other) const;
    };
}
