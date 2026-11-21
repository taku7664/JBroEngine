#pragma once

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Canvas/Layer.h>
#include <JBro/Core/Core.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Types/String.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/Uuid.h>

namespace JBro
{
    class Canvas;

    // **화면의 창 하나다**(D-438, viewport-plan §4.1, 기존 엔진 `CanvasViewport`). 캔버스가 목록으로 든다.
    //
    // 카메라는 "월드 속의 눈" 일 뿐이고, 그 눈의 그림을 **화면 어디에·어떤 레이어를·어떤 차례로** 그릴지는 이것이 정한다.
    // 그래서 2D 와 3D 가 같은 모양이고 카메라 컴포넌트에는 화면 배치의 칸이 없다. 목록 차례가 그리는 차례다.
    // 보통 게임은 새 캔버스의 뷰포트 하나(전체 화면, 카메라 자동)로 끝나 사용자는 볼 일이 없다.
    struct CanvasViewport
    {
        String name;
        // 끄면 그리지 않는다.
        Bool active = true;
        // 그리는 눈 - 카메라 오브젝트의 번호다. 비면 **자동**이고, 그 차원의 첫 켜진 카메라(캔버스 차례)다.
        // 그 오브젝트가 없거나 그 차원의 켜진 카메라가 아니면 이 뷰포트는 그리지 않는다(자동으로 떨어지지 않는다).
        InstanceId camera = InvalidInstanceId;
        // 대상 안의 사각형이다. 왼쪽 위 원점이고 **비율(0..1) x 대상 크기 + 픽셀**이다(D-439, 기존 엔진 `Layout2D`).
        // 오른쪽 위에서 20 픽셀 띄운 200 x 150 미니맵은 `origin (1, 0)`·`originPixels (-220, 20)`·`size (0, 0)`·`sizePixels (200, 150)` 이다.
        // 비율은 0..1 로 자르고, 사각형이 대상 밖으로 나간 만큼은 잘린다(`ResolveViewportPixels`).
        Vector2 origin{ 0.0f, 0.0f };
        Vector2 size{ 1.0f, 1.0f };
        Vector2 originPixels{ 0.0f, 0.0f };
        Vector2 sizePixels{ 0.0f, 0.0f };
        // 그릴 레이어. **월드 레이어와 화면 레이어를 따로 읽는다**(D-439, `BuildViewportLayerMasks`):
        //   월드 - 목록에 월드 레이어가 하나도 없으면 모두, 있으면 든 것만이다. 없는 번호(`InvalidLayerId`)는 "월드 레이어 없음" 의 표시다.
        //   화면 - 목록에 든 화면 레이어는 이 뷰포트의 사각형 안에 그린다(분할 화면의 플레이어별 HUD). 어느 뷰포트도 고르지 않은 화면 레이어는
        //          지금처럼 대상 전체에 한 번 그린다.
        Array<LayerId> layers;
        // 앞 뷰가 이미 그린 대상에서 제 사각형을 카메라의 지우기 색으로 먼저 칠한다. 대상의 첫 뷰는 언제나 대상 전체를 지운다.
        Bool clearViewport = true;
        // 있으면 화면 대신 이 렌더 텍스처(`.jrt`, 에셋 아이디)에 그린다.
        Uuid renderTextureId;
        // **풀린 렌더 텍스처다.** 파일에 적지 않는다. 프레임워크가 캔버스 에셋을 묶을 때(`BindCanvasAssets`) `renderTextureId` 로 채우고,
        // 스크립트가 바꿀 수 있다(`ViewportService::SetRenderTexture`) - 다시 묶으면 파일의 값으로 돌아간다.
        AssetHandle renderTexture;

        Bool HasCamera() const
        {
            return camera != InvalidInstanceId;
        }

        // 화면이 아니라 렌더 텍스처에 그리는 뷰포트인가. 아이디가 있는데 핸들이 풀리지 않았으면(에셋을 못 찾음) 그리지 않는다 - 화면에 잘못 그리지 않는다.
        Bool DrawsToTexture() const
        {
            return renderTexture.generation != 0 || false == renderTextureId.IsNull();
        }
    };

    // **뷰포트 사각형을 대상 픽셀로 푼다**(D-439). 비율은 0..1 로 자르고 픽셀을 더한 뒤 대상 안으로 자른다. 수가 아닌 값은 0 이다.
    // 남는 사각형이 비면 거짓이다. 2D·3D 의 그리기·포인터·화면 레이어의 기준이 모두 이것으로 잰다 - 따로 재면 그린 자리와 누르는 자리가 어긋난다.
    Bool ResolveViewportPixels(Vector2 origin, Vector2 size, Vector2 originPixels, Vector2 sizePixels, Float targetWidth, Float targetHeight,
        Float& x, Float& y, Float& width, Float& height);

    inline Bool ResolveViewportPixels(const CanvasViewport& viewport, Float targetWidth, Float targetHeight,
        Float& x, Float& y, Float& width, Float& height)
    {
        return ResolveViewportPixels(viewport.origin, viewport.size, viewport.originPixels, viewport.sizePixels, targetWidth, targetHeight,
            x, y, width, height);
    }

    // **레이어 차례의 비트 마스크다**(D-438). 렌더 쪽이 뷰마다 든다 - 아이템은 레이어 아이디가 아니라 차례(`LayerOrder`)를 들기 때문이다.
    // 차례 0~255 만 담는다(렌더러의 묶음 상한 `maxLayerGroups` 와 같다). 그 밖의 차례는 늘 그린다.
    struct ViewLayerMask
    {
        static constexpr UInt32 Words = 4;
        static constexpr UInt32 Capacity = Words * 64;

        Bool filtered = false;
        UInt64 bits[Words] = {};

        void Allow(LayerOrder order)
        {
            if (static_cast<UInt32>(order) < Capacity)
            {
                bits[order / 64] = bits[order / 64] | (UInt64(1) << (order % 64));
            }
        }

        Bool Draws(LayerOrder order) const
        {
            if (false == filtered || static_cast<UInt32>(order) >= Capacity)
            {
                return true;
            }
            return (bits[order / 64] & (UInt64(1) << (order % 64))) != 0;
        }

        // 담은 차례가 하나라도 있는가. 거르지 않는 마스크는 거짓이다.
        Bool Any() const
        {
            return filtered && (bits[0] | bits[1] | bits[2] | bits[3]) != 0;
        }
    };

    // **뷰포트의 레이어 목록을 월드와 화면 마스크로 나눈다**(D-439, `CanvasViewport::layers`). `world` 는 월드 레이어가 하나도 없으면
    // 거르지 않는다. `screen` 은 목록에 든 화면 레이어만 담는다(하나도 없으면 거르는 빈 마스크 - `Any` 가 거짓). 캔버스에 없는 번호는 월드 쪽이다.
    void BuildViewportLayerMasks(const Canvas& canvas, const CanvasViewport& viewport, ViewLayerMask& world, ViewLayerMask& screen);
    // 이 뷰포트가 그 레이어를 그리는가 - 위 규칙 그대로다. 화면 레이어는 이 뷰포트의 사각형에 그리는가다. 인스펙터의 칸이 쓴다.
    Bool ViewportDrawsLayer(const Canvas& canvas, const CanvasViewport& viewport, LayerId layer);
}
