#pragma once

#include <JBro/Graphics/PostProcess.h>
#include <JBro/Graphics/PostProcessLibrary.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>
#include <JBro/Types/UInt.h>

#include <cstddef>

namespace JBro
{
    // **뷰의 프로필을 섞는 자리**(D-346, postprocess-plan §2.5). 카메라의 프로필이 기준이고, 볼륨의 프로필을 무게대로 우선순위 차례로 섞고,
    // 맨 끝에 스크립트가 덮어쓴 필드를 얹는다. 결과는 렌더러에 그대로 넘기는 `PostProcessStack` 이다. 2D·3D 프레임워크가 하나씩 든다.
    //
    // **섞기는 필드의 리플렉션 종류로 정한다.** `Float` 는 보간하고, `Float` 만 든 구조(`Vector2`·`Vector3`·`Color`)는 필드마다 보간하고,
    // `Int32` 는 보간해 반올림한다. 그 밖(`Bool`·열거·에셋)은 무게 절반에서 바꾼다. 볼륨은 덮어쓴 필드만 섞는다 - 적지 않은 필드는 기준 그대로다.
    // 저장하지 않는 필드(LUT 의 렌더러 텍스처 같은 풀린 값)는 그 항목의 에셋 아이디가 바뀔 때 함께 바뀐다.
    // 기준에 없는 종류를 볼륨이 덮어쓰면 그 종류의 기본값에서 시작하는 항목을 끝에 더한다(무게 0 이면 기본값 그대로다).
    //
    // **프레임 경로에 할당이 없다.** 자리는 `Reserve` 로 잡고, 넘치면 버리고 센다. 메인 스레드 전용이다.
    class PostProcessMixer final
    {
    public:
        // 스크립트가 덮어쓸 필드 하나를 가리키는 번호다. 위 16 비트가 효과 종류, 아래 16 비트가 파라미터 표의 필드 차례다.
        static constexpr UInt32 InvalidField = 0xFFFFFFFFu;
        static constexpr UInt32 MaxScriptOverrides = 32;
        static constexpr UInt32 MaxOverrideBytes = 16;

        void Reserve(UInt32 entries, UInt32 blockBytes);

        // 기준을 놓는다. 앞 프레임의 결과는 버린다. 꺼진 항목은 옮기지 않는다.
        void Begin(const PostProcessStack& base);
        // 볼륨 하나의 프로필을 `weight`(0..1) 로 섞는다. 0 이하면 아무것도 하지 않는다.
        void Blend(const PostProcessProfileView& profile, Float weight);
        // 스크립트가 덮어쓴 필드를 얹는다(`SetOverride`). 섞기의 맨 끝에 한 번 부른다.
        void ApplyOverrides();
        PostProcessStack GetResult() const;
        // 자리가 모자라 버린 항목 수다. `Begin` 이 비운다.
        UInt32 GetDroppedCount() const noexcept
        {
            return m_dropped;
        }

        // ── 스크립트의 덮어쓰기(D-346) ──
        // 효과 이름과 필드 이름으로 번호를 찾는다. 없거나 저장하지 않는 필드(숨긴 값)면 `InvalidField` 다.
        static UInt32 FindField(const char* effect, const char* field);
        // 그 필드의 값으로 덮어쓴다. 필드의 타입이 `type` 이어야 하고 크기가 `MaxOverrideBytes` 안이어야 한다. 같은 필드는 갈아 끼운다.
        // 자리(32)가 차거나 타입이 다르면 거짓이다.
        Bool SetOverride(UInt32 field, const TypeDescriptor& type, const void* value);
        void ClearOverride(UInt32 field);
        void ClearOverrides();
        UInt32 GetOverrideCount() const noexcept
        {
            return m_overrideCount;
        }

    private:
        struct ScriptOverride
        {
            UInt32 field = InvalidField;
            alignas(16) std::byte value[MaxOverrideBytes] = {};
        };

        // 그 종류의 항목을 찾는다. 없고 `create` 면 기본값으로 끝에 더한다. 자리가 없으면 nullptr 이다.
        std::byte* FindBlock(UInt32 type, Bool create);

        Array<PostProcessEntry> m_entries;
        Array<std::byte> m_blocks;
        UInt32 m_used = 0;
        UInt32 m_dropped = 0;
        ScriptOverride m_overrides[MaxScriptOverrides];
        UInt32 m_overrideCount = 0;
    };
}
