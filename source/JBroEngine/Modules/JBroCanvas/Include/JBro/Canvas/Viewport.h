#pragma once

#include <JBro/Canvas/Layer.h>
#include <JBro/Core/Core.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Types/String.h>
#include <JBro/Types/UInt.h>
#include <JBro/Types/Uuid.h>

namespace JBro
{
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
        // 대상 안의 사각형이다. 왼쪽 위 원점, 0..1 비율.
        Vector2 origin{ 0.0f, 0.0f };
        Vector2 size{ 1.0f, 1.0f };
        // 그릴 월드 레이어. 비면 모든 레이어다. 화면 레이어는 이것과 무관하게 대상 전체에 그린다.
        Array<LayerId> layers;
        // 앞 뷰가 이미 그린 대상에서 제 사각형을 카메라의 지우기 색으로 먼저 칠한다. 대상의 첫 뷰는 언제나 대상 전체를 지운다.
        Bool clearViewport = true;
        // 있으면 화면 대신 이 렌더 텍스처(`.jrt`, 에셋 아이디)에 그린다. 풀린 핸들은 프레임워크가 든다(`BindCanvasAssets`).
        Uuid renderTextureId;

        Bool HasCamera() const
        {
            return camera != InvalidInstanceId;
        }

        Bool DrawsLayer(LayerId layer) const
        {
            if (layers.IsEmpty())
            {
                return true;
            }
            for (std::size_t at = 0; at < layers.Size(); ++at)
            {
                if (layers[at] == layer)
                {
                    return true;
                }
            }
            return false;
        }
    };

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
    };
}
