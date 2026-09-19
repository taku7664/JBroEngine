#pragma once

#include <JBro/Reflection/PropertyInfo.h>

namespace JBro
{
    class Canvas;
    class ComponentBase;

    // 프로퍼티 표가 등록된 컴포넌트마다 부른다. 표가 없는 타입(등록하지 않은 스크립트 등)은 건너뛴다.
    using ReflectedComponentVisitor = void (*)(const PropertyTable& table, ComponentBase& component, void* user);

    // 캔버스의 모든 오브젝트의 모든 컴포넌트를 리플렉션 표와 함께 돈다. 캔버스 파일과 에셋 해석 패스(D-115)가 같은
    // 걸음을 쓴다 - 걷는 길이 둘이면 한쪽만 고쳐지는 날이 온다. 프레임 경로가 아니다.
    void ForEachReflectedComponent(Canvas& canvas, ReflectedComponentVisitor visitor, void* user);
}
