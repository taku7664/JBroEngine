#pragma once
#include <JBro/Editor/Command/ComponentAddress.h>
#include <JBro/Editor/Widget/DragDrop.h>

namespace JBro::Widget
{
    // 인스펙터 재정렬과 AI 첨부가 같은 꾸러미를 읽는다. 슬롯은 순서, instance는 끌기 시작한 대상이다.
    struct ComponentDragPayload
    {
        EditorObjectId object = InvalidEditorObjectId;
        UInt64 slot = 0;
        AttachedKind kind = AttachedKind::Component;
        InstanceId instance = 0;
    };
    void SetComponentDragPayload(const ComponentDragPayload& value);
}
