#pragma once

#include <JBro/Editor/EditorNotifications.h>
#include <JBro/Editor/Widget/Common.h>
#include <JBro/Types/Float.h>

namespace JBro::Widget
{
    // 알림 상자의 크기와 간격이다. 테스트가 같은 값으로 자리를 잰다.
    struct NotificationStackStyle
    {
        Float width = 340.0f;
        // 창 가장자리에서 더미까지.
        Float margin = 16.0f;
        // 바닥에서 먼저 비워 둘 높이다. 에디터는 상태 표시줄 높이를 준다 - 더미가 그 줄을 덮지 않게.
        Float bottomInset = 0.0f;
        // 상자와 상자 사이.
        Float spacing = 8.0f;
        Float padding = 10.0f;
        // 왼쪽 무게 색 띠의 폭.
        Float accentWidth = 4.0f;
    };

    // 우측 하단의 알림 더미를 그린다(todo "에디터 공용 기반" 1 번). 모든 창 위에 선다.
    //
    // 알림 하나가 상자 하나다. 새 것이 바닥에 들어오고 앞의 것들은 위로 밀려 올라간다. 올려 두면
    // 시간이 멈추고 닫기 표시가 보인다. 누른 채 옆으로 끌면 따라오고, 멀리 끌어 놓으면 그쪽으로 밀려
    // 사라진다. 끌지 않고 누르면 **그 알림의 핸들을 돌려준다** - 할 일(`NotificationAction`)을
    // 부르는 것은 에디터를 쥔 쪽이 `EditorNotifications::Activate` 로 한다. 누른 것이 없으면
    // `InvalidNotificationHandle` 이다.
    //
    // 시간과 애니메이션은 옮기지 않는다 - `EditorNotifications::Update` 가 한다.
    NotificationHandle NotificationStack(
        EditorNotifications& notifications, const NotificationStackStyle& style = {});

    // 상자 하나의 높이. 제목 한 줄과 상자 폭에서 줄을 바꾼 설명이다.
    Float NotificationHeight(const NotificationView& view, const NotificationStackStyle& style);
}
