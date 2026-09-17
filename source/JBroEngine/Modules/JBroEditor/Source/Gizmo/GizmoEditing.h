#pragma once

#include <JBro/Editor/Command/ComponentAddress.h>
#include <JBro/Editor/Command/SetPropertyCommand.h>
#include <JBro/Editor/Gizmo/GizmoModel.h>
#include <JBro/Types/Array.h>
#include <JBro/Types/String.h>

namespace JBro
{
    class EditorApplication;
    class GameObject;

    // 기즈모의 끌기를 트랜스폼 편집으로 옮긴다(D-109). 인스펙터의 드래그와 같은 길이다(§11.3):
    // 끄는 동안은 위젯처럼 값을 직접 쓰고, 놓으면 편집 전 값으로 되돌린 뒤 `SetPropertyCommand` 묶음
    // 하나를 실행한다 - 쓰는 길이 커맨드 하나로 남고 Ctrl+Z 한 번이 끌기 하나를 되돌린다.
    //
    // 대상은 고른 것 중 맨 위 것들이다(부모와 자식을 함께 골랐으면 부모만). 주된 것의 월드 변화를
    // 델타로 떠서 저마다의 로컬 값에 얹는다 - 셋을 골라 끌었을 때 셋이 한 자리로 모이면 옮긴 것이 아니다.
    class GizmoEditing
    {
    public:
        // 주된 선택의 월드 트랜스폼을 기즈모 대상으로 읽는다. 트랜스폼이 없으면 거짓이다.
        static bool ReadSubject(EditorApplication& editor, GameObject& object, GizmoSubject& subject);

        // 끌기 시작: 대상들의 편집 전 값을 뜬다. 뜬 것이 하나도 없으면 거짓이고 끌기는 시작하지 않는다.
        bool Begin(EditorApplication& editor, GizmoMode mode, const GizmoSubject& primaryStart);
        // 끄는 동안: 주된 것의 지금 값에서 델타를 내어 대상마다 쓴다.
        void Apply(EditorApplication& editor, const GizmoSubject& primaryNow);
        // 놓음: 편집 전 값으로 되돌리고 커맨드 하나로 확정한다. 바뀐 것이 없으면 커맨드도 없다.
        void Commit(EditorApplication& editor);
        void Cancel(EditorApplication& editor);

        bool IsActive() const
        {
            return m_active;
        }

    private:
        struct Target
        {
            ComponentAddress address;
            SetPropertyCommand::Path path;
            String before;
            // 로컬 시작값. 2D 는 z·w 를 쓰지 않는다.
            Vec3 localPosition;
            Quaternion localRotation;
            float localAngleDegrees = 0.0f;
            Vec3 localScale = {1.0f, 1.0f, 1.0f};
            // 부모의 월드 회전·스케일. 월드 델타를 로컬로 옮기는 데 쓴다.
            Quaternion parentRotation;
            Vec3 parentScale = {1.0f, 1.0f, 1.0f};
            bool planar = false;
        };

        bool CollectTarget(EditorApplication& editor, GameObject& object, Target& target) const;
        void Write(EditorApplication& editor, const Target& target, const GizmoSubject& primaryNow) const;

        Array<Target> m_targets;
        GizmoSubject m_primaryStart;
        GizmoMode m_mode = GizmoMode::Translate;
        bool m_active = false;
    };
}
