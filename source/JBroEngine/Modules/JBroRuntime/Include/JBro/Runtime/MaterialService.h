#pragma once

#include <JBro/Runtime/ComponentHandle.h>
#include <JBro/Types/Uuid.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Color.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/Int.h>
#include <JBro/Types/Math2D.h>
#include <JBro/Types/UInt.h>

namespace JBro::Service
{
    // 덮어쓸 머티리얼 파라미터 하나다(D-348). `MaterialService::FindParameter` 가 준다. 찾지 못했으면 무효다.
    struct MaterialParameter
    {
        UInt32 value = 0xFFFFFFFFu;

        Bool IsValid() const
        {
            return value != 0xFFFFFFFFu;
        }
    };

    // 스크립트가 머티리얼 에셋의 파라미터를 덮어쓰는 길이다(D-348). `GetServiceContext().Material` 로 부른다.
    //
    //   MaterialParameter glow = GetServiceContext().Material.FindParameter(GetSpriteRenderer2D().GetMaterialId(), "glowColor");
    //   GetServiceContext().Material.SetColor(glow, Color{1.0f, 0.2f, 0.2f, 1.0f});   // 그 머티리얼을 쓰는 모든 것이 바뀐다
    //   GetServiceContext().Material.Clear(glow);                                       // 머티리얼 파일의 값으로 되돌린다
    //
    // 덮어쓴 값은 **머티리얼 에셋** 단위다 - 같은 머티리얼을 쓰는 스프라이트·메시·후처리 효과가 모두 함께 바뀐다. 파일은 바뀌지 않고, 지울 때까지
    // 남는다. 셰이더의 형과 맞아야 얹힌다: `SetFloat` 는 `float`, `SetInt` 는 `int`·`uint`, `SetBool` 은 `bool`, `SetVector2` 는 `float2`,
    // `SetColor`·`SetVector4` 는 `float3`·`float4`(`float3` 이면 넷째 칸은 버린다). 찾기는 이름을 견주므로 `OnStart` 에서 찾아 필드에 든다.
    // 캔버스가 묶이지 않은 자리에서는 아무것도 하지 않는다.
    //
    // **오브젝트 하나만 바꾸려면**(D-368) 렌더러 핸들을 함께 넘긴다 - 같은 머티리얼의 다른 오브젝트는 그대로이고, 그 오브젝트는 따로 그려진다(배치가 끊긴다).
    //
    //   MaterialParameter glow = GetServiceContext().Material.FindParameter(GetComponent<SpriteRenderer2D>(), material, "glowColor");
    class MaterialService
    {
    public:
        MaterialParameter FindParameter(Uuid material, const char* parameter) const;
        MaterialParameter FindParameter(const Internal::ComponentHandle& renderer, Uuid material, const char* parameter) const;
        Bool SetFloat(MaterialParameter parameter, Float value) const;
        Bool SetInt(MaterialParameter parameter, Int32 value) const;
        Bool SetBool(MaterialParameter parameter, Bool value) const;
        Bool SetVector2(MaterialParameter parameter, Vector2 value) const;
        Bool SetColor(MaterialParameter parameter, Color value) const;
        Bool SetVector4(MaterialParameter parameter, Float x, Float y, Float z, Float w) const;
        void Clear(MaterialParameter parameter) const;
        void ClearAll() const;
    };
}
