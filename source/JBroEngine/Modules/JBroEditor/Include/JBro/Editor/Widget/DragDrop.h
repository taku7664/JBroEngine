#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

namespace JBro::Widget
{
    // 에디터의 끌어 놓기는 모두 이 층을 거친다(D-255, §11.1).
    //
    // 기존 엔진 `EditorDragDrop` 은 꾸러미 이름을 한 헤더에 모았지만 받는 쪽마다 `ImGui::AcceptDragDropPayload` 를
    // 직접 불렀고, 꾸러미에 날 포인터(`CGameObject*`)를 실었다. 새 엔진은 그 전까지 이름 문자열을 파일마다 따로 적어,
    // 인스펙터가 계층의 이름을 한 번 더 적어 두고 있었다 - 한쪽 이름이 바뀌면 드롭이 조용히 끊긴다.
    //
    // **꾸러미 종류는 이 표 하나다.** 이름 문자열은 소스(`DragDrop.cpp`) 한 곳에서만 나오고 부르는 쪽은 종류로 말한다.
    enum class DragKind : std::uint8_t
    {
        // 에셋 브라우저의 줄. `AssetDrag.h` 가 머리와 경로 묶음을 싣는다.
        Asset,
        // 계층의 오브젝트 줄. 에디터 오브젝트 번호(`EditorObjectId`, 8 바이트)다 - 주소가 아니다(D-72).
        HierarchyObject,
        // 계층의 레이어 줄. `LayerId` 다.
        HierarchyLayer,
        // 목록 위젯의 행. `ListReorderPayload` 다.
        ListReorder,
        // 인스펙터의 컴포넌트 머리. 오브젝트의 에디터 번호와 슬롯 번호다 - 같은 오브젝트의 머리에만 놓인다.
        InspectorComponent,
        Count,
    };

    // **받는 자리는 외곽선을 긋지 않는다**(D-255). ImGui 의 기본 표시는 받는 자리에 테두리를 두르는데, 행이 빽빽한
    // 계층·목록에서는 그 테두리가 옆 줄까지 덮어 어디에 놓이는지 오히려 흐렸다. 기본은 옅게 칠하는 것이고,
    // 끼울 선처럼 제 표시를 그리는 자리는 `None` 을 준다.
    enum class DropFeedback : std::uint8_t
    {
        Fill,
        None,
    };

    // 받는 자리 위에 있는 꾸러미다. 이 종류의 꾸러미가 이 자리 위에 있으면 참이고,
    // 놓는 프레임에만 `delivered` 가 참이다 - 선을 그리는 것은 위에 있을 때, 옮기는 것은 놓였을 때다.
    // `data` 는 이 프레임 동안만 산다.
    struct DropPayload
    {
        const void* data = nullptr;
        std::size_t size = 0;
        Bool delivered = false;

        explicit operator bool() const
        {
            return data != nullptr;
        }
    };

    // 끌기. `BeginDragSource` 가 참일 때만 `SetDragPayload` 와 그림(끌고 다니는 이름표)을 그리고 `EndDragSource` 로 닫는다.
    // 직전 항목이 끌기의 출처가 된다.
    Bool BeginDragSource();
    void SetDragPayload(DragKind kind, const void* data, std::size_t size);
    void EndDragSource();

    // **꾸러미는 값으로 싣는다.** 복사해서 옮길 수 있는 것만 받는다 - 포인터를 실으면 놓을 때 이미 죽은 것을 가리킬 수 있다.
    template <typename T>
    void SetDragValue(DragKind kind, const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>, "a drag payload is copied bytes");
        SetDragPayload(kind, &value, sizeof(T));
    }

    // 지금 이 종류를 끌고 있는가. 받는 자리를 만들지 말지(보이지 않는 단추가 클릭을 가로채지 않게)를 여기서 정한다.
    Bool IsDragging(DragKind kind);
    // 무엇이든 끌고 있는가. 끌고 지나가는 손짓을 다른 손짓(사각 선택)으로 읽지 않게 한다.
    Bool IsDraggingAnything();
    // 지금 끌고 있는 이 종류의 꾸러미를 받는 자리 밖에서 본다. `delivered` 는 늘 거짓이다.
    DropPayload PeekDrag(DragKind kind);

    // 받기. 직전 항목이 받는 자리이고, `BeginDropTarget` 이 참일 때만 `AcceptDrop` 을 부르고 `EndDropTarget` 으로 닫는다.
    Bool BeginDropTarget();
    DropPayload AcceptDrop(DragKind kind, DropFeedback feedback = DropFeedback::Fill);
    void EndDropTarget();

    // 꾸러미를 값으로 읽는다. 크기가 다르면 읽지 않는다 - 믿고 읽으면 남의 기억을 값으로 읽는다.
    template <typename T>
    Bool ReadDropValue(const DropPayload& payload, T& out)
    {
        static_assert(std::is_trivially_copyable_v<T>, "a drag payload is copied bytes");
        if (payload.data == nullptr || payload.size != sizeof(T))
        {
            return false;
        }
        std::memcpy(&out, payload.data, sizeof(T));
        return true;
    }

    // 놓였을 때만 값을 준다. 위에 있을 때의 표시는 `feedback` 이 맡는다.
    template <typename T>
    Bool AcceptDropValue(DragKind kind, T& out, DropFeedback feedback = DropFeedback::Fill)
    {
        const DropPayload payload = AcceptDrop(kind, feedback);
        return payload.delivered && ReadDropValue(payload, out);
    }

    // 받는 자리의 표시. 색은 테마의 것이고 패널이 제 색을 만들지 않는다(§11.3.1).
    // 칠하기는 자식으로 들어가는 자리, 선은 사이에 끼우는 자리다.
    void DrawDropFill(Float minX, Float minY, Float maxX, Float maxY);
    void DrawDropLine(Float minX, Float maxX, Float y);
}
