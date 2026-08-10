#pragma once

#include "GameFramework/Component/Renderer2DComponent.h"

// ─────────────────────────────────────────────────────────────────────────────
//  CRendererComponentAccess — 리플렉션이 컴포넌트 필드를 raw 로 덮어쓴 뒤
//  렌더러 경계 캐시를 무효화하는 통로. 호스트(에디터/직렬화)만 사용한다.
//  게임 스크립트 SDK 에는 스테이징하지 않는다(StageSDK.targets 의 Exclude 참조).
//
//  왜 friend 로 에디터 커맨드를 직접 지정하지 않는가:
//  CSetComponentPropertyCommand 는 Application/Editor 에 있고 Renderer2DComponent.h 는
//  Engine/GameFramework 에 있으면서 SDK 로 나간다. 엔진 헤더가 에디터 타입을 friend 로
//  적으면 층이 거꾸로 뒤집힌다. 접근자를 한 겹 두면 그 의존이 생기지 않는다.
// ─────────────────────────────────────────────────────────────────────────────
class CRendererComponentAccess final
{
public:
	// 리플렉션 프로퍼티를 raw 로 쓴 직후 호출한다(인스펙터 편집 · undo/redo · 역직렬화).
	// 렌더러가 아니면 아무 일도 하지 않으므로 타입 검사 없이 불러도 된다.
	// 여러 번 불러도 무해하다(다음 조회에서 한 번만 재계산).
	static void NotifyReflectedWrite(CComponent& component)
	{
		if (CRenderer2DComponent* renderer = component.AsRenderer2DComponent())
		{
			renderer->MarkBoundsDirty();
		}
	}
};
