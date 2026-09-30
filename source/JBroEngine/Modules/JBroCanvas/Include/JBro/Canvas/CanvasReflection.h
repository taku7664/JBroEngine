#pragma once

#include <JBro/Reflection/PropertyInfo.h>

namespace JBro
{
    class Canvas;

    // 프로퍼티 표가 등록된 컴포넌트와 스크립트마다 부른다. `instance` 는 그 타입의 객체다. 표가 없는 타입은 건너뛴다.
    using ReflectedInstanceVisitor = void (*)(const PropertyTable& table, void* instance, void* user);

    // 캔버스의 모든 오브젝트의 모든 컴포넌트와 스크립트를 리플렉션 표와 함께 돈다(D-271 - 스크립트도 에셋 필드를 가진다).
    // 에셋 해석 패스(D-115)가 쓴다 - 걷는 길이 둘이면 한쪽만 고쳐지는 날이 온다. 프레임 경로가 아니다.
    void ForEachReflectedInstance(Canvas& canvas, ReflectedInstanceVisitor visitor, void* user);
}
