#pragma once

#include <JBro/Editor/Widget/Common.h>

#include <JBro/AssetTypes/AssetTypes.h>
#include <JBro/Types/ArrayView.h>

namespace JBro::Widget
{
    // `AssetId` 하나를 고르는 칸이다. `FilterCombo` 위에 얹혔다(D-116).
    //
    // **위젯은 레지스트리를 모른다.** 부르는 쪽이 같은 타입의 에셋을 이름(상대경로)과
    // 아이디 두 뷰로 넘긴다 - 위젯 계층이 에셋 모듈의 표를 들여다보지 않아야 에셋 브라우저·
    // 인스펙터·앞으로의 다른 자리가 같은 칸을 쓴다. 두 뷰는 길이가 같아야 한다.
    //
    // 트리거에는 현재 아이디의 이름이 보인다. 아이디가 비었으면 `NoneText`, 목록에 없으면
    // (지워진 파일, 다른 타입) `MissingText` 다. `AllowClear` 면 목록 맨 위에 비우기 항목이
    // 선다.
    //
    // 돌려주는 값: 참이면 아이디가 바뀌었다.
    class AssetField
    {
    public:
        AssetField(const char* id, ArrayView<const char* const> names,
            ArrayView<const AssetId> ids, AssetId& value);

        AssetField& NoneText(const char* text);
        AssetField& MissingText(const char* text);
        AssetField& AllowClear(bool allow = true);
        AssetField& Width(float width);

        bool Draw() const;
        bool operator()() const;

    private:
        const char* m_id = nullptr;
        ArrayView<const char* const> m_names;
        ArrayView<const AssetId> m_ids;
        AssetId& m_value;
        const char* m_noneText = nullptr;
        const char* m_missingText = nullptr;
        float m_width = 0.0f;
        bool m_allowClear = true;
    };
}
