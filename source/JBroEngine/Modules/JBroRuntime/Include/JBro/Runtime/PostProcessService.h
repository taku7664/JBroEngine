#pragma once

#include <JBro/Types/Bool.h>
#include <JBro/Types/Color.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Types/UInt.h>

namespace JBro::Service
{
    // 덮어쓸 후처리 필드 하나다(D-346). `PostProcessService::FindField` 가 준다. 찾지 못했으면 무효다.
    struct PostProcessField
    {
        UInt32 value = 0xFFFFFFFFu;

        Bool IsValid() const
        {
            return value != 0xFFFFFFFFu;
        }
    };

    // 스크립트가 카메라의 후처리 효과 필드를 덮어쓰는 길이다(D-346). `GetServiceContext().PostProcess` 로 부른다.
    //
    //   PostProcessField saturation = GetServiceContext().PostProcess.FindField("ColorAdjustments", "saturation");
    //   GetServiceContext().PostProcess.SetFloat(saturation, -100.0f);   // 맞으면 화면이 회색으로
    //   GetServiceContext().PostProcess.Clear(saturation);               // 되돌린다
    //
    // 덮어쓴 값은 카메라의 프로필과 볼륨을 섞은 **맨 끝**에 얹힌다 - 프로필에 그 효과가 없으면 그 종류의 기본값에서 시작하는 항목이 생긴다.
    // 지울 때까지 남는다. 볼륨을 켜고 끄는 것은 볼륨 핸들(`PostProcessVolume2D::SetWeight`·`SetPostProcessProfile`)이다.
    // 찾기는 이름을 견주므로 `OnStart` 에서 찾아 필드에 든다. 캔버스가 묶이지 않은 자리에서는 아무것도 하지 않는다.
    class PostProcessService
    {
    public:
        PostProcessField FindField(const char* effect, const char* field) const;
        Bool SetFloat(PostProcessField field, Float value) const;
        Bool SetInt(PostProcessField field, Int32 value) const;
        Bool SetBool(PostProcessField field, Bool value) const;
        Bool SetColor(PostProcessField field, Color value) const;
        Bool SetVector2(PostProcessField field, Vector2 value) const;
        void Clear(PostProcessField field) const;
        void ClearAll() const;
    };
}
