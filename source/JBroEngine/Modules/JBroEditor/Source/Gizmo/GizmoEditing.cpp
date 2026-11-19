#include "GizmoEditing.h"

#include <JBro/Canvas/Canvas.h>
#include <JBro/Editor/Command/CompoundCommand.h>
#include <JBro/Editor/EditorApplication.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Framework3D/Component/Transform3D.h>
#include <JBro/Reflection/PropertyInfo.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Runtime/GameObject.h>
#include <JBro/Types/NameTable.h>

#include <cmath>
#include <cstring>
#include <JBro/Types/Bool.h>
#include <JBro/Types/Float.h>

namespace JBro
{
    namespace
    {
        // **Transform2D 의 회전은 라디안이다**(D-247). 전에는 이 자리가 도를 받아 π/180 을
        // 곱했고, 그래서 기즈모로 90° 돌리면 `rotation` 에 90 이 적혀 렌더러가 90 라디안으로
        // 그렸다. 기즈모 안에서는 읽기와 쓰기가 둘 다 도라 앞뒤가 맞아 보였을 뿐이다.
        Quaternion FromAngleAboutZ(Radian angle)
        {
            return FromAxisAngle({0.0f, 0.0f, 1.0f}, angle);
        }

        const char* FieldNameFor(GizmoMode mode)
        {
            switch (mode)
            {
            case GizmoMode::Translate:
                return "position";
            case GizmoMode::Rotate:
                return "rotation";
            case GizmoMode::Scale:
                return "scale";
            }
            return "position";
        }

        Float SafeDivide(Float value, Float divisor)
        {
            return std::fabs(divisor) > 1.0e-6f ? value / divisor : value;
        }

        // 부모의 월드 회전·스케일. 부모가 없거나 트랜스폼이 없으면 항등이다.
        void ParentFrame(Canvas& canvas, Object::GameObject& object, Bool planar, Quaternion& rotation, Vector3& scale)
        {
            rotation = {};
            scale = {1.0f, 1.0f, 1.0f};
            Object::GameObject* parent = object.GetParent();
            if (parent == nullptr)
            {
                return;
            }
            if (planar)
            {
                if (auto* transform = canvas.FindComponentRaw<Component::Transform2D>(parent))
                {
                    rotation = FromAngleAboutZ(transform->worldRotation);
                    scale = {transform->worldScale.x, transform->worldScale.y, 1.0f};
                }
                return;
            }
            if (auto* transform = canvas.FindComponentRaw<Component::Transform3D>(parent))
            {
                rotation = Normalize(transform->worldRotation);
                scale = transform->worldScale;
            }
        }
    }

    Bool GizmoEditing::ReadSubject(EditorApplication& editor, Object::GameObject& object, GizmoSubject& subject)
    {
        Canvas* canvas = editor.GetCanvas();
        if (canvas == nullptr)
        {
            return false;
        }
        subject = {};
        if (auto* transform = canvas->FindComponentRaw<Component::Transform3D>(&object))
        {
            // 월드 캐시가 아직 없으면(첫 프레임) 로컬 값이 곧 월드다.
            subject.position = transform->worldValid ? transform->worldPosition : transform->position;
            subject.rotation = Normalize(transform->worldValid ? transform->worldRotation : transform->rotation);
            subject.scale = transform->scale;
            subject.planar = false;
            return true;
        }
        if (auto* transform = canvas->FindComponentRaw<Component::Transform2D>(&object))
        {
            const Vector2 position = transform->worldValid ? transform->worldPosition : transform->position;
            const Radian angle = transform->worldValid ? transform->worldRotation : transform->GetRotationRadian();
            subject.position = {position.x, position.y, 0.0f};
            subject.rotation = FromAngleAboutZ(angle);
            subject.scale = {transform->scale.x, transform->scale.y, 1.0f};
            subject.planar = true;
            return true;
        }
        return false;
    }

    Bool GizmoEditing::CollectTarget(EditorApplication& editor, Object::GameObject& object, Target& target) const
    {
        Canvas* canvas = editor.GetCanvas();
        if (canvas == nullptr)
        {
            return false;
        }
        const char* field = FieldNameFor(m_mode);
        target.movesPosition = m_mode != GizmoMode::Translate;
        if (auto* transform = canvas->FindComponentRaw<Component::Transform3D>(&object))
        {
            constexpr ComponentTypeId typeId = MakeStableTypeId(Component::Transform3D::StaticTypeName());
            if (false == MakeComponentAddress(editor.GetObjectIds(), object, *transform, target.address)
                || false == SetPropertyCommand::MakeFieldPath(typeId, field, target.path)
                || false == SetPropertyCommand::ReadValue(transform, typeId, target.path, target.before))
            {
                return false;
            }
            // 되살릴 위치를 뜨지 못했으면 이 대상은 움직이지 않는다 - 쓰고 나서 되돌릴 수 없다.
            if (target.movesPosition
                && (false == SetPropertyCommand::MakeFieldPath(typeId, "position", target.positionPath)
                    || false == SetPropertyCommand::ReadValue(transform, typeId, target.positionPath, target.positionBefore)))
            {
                return false;
            }
            target.worldPosition = transform->worldValid ? transform->worldPosition : transform->position;
            target.planar = false;
            target.localPosition = transform->position;
            target.localRotation = Normalize(transform->rotation);
            target.localScale = transform->scale;
            ParentFrame(*canvas, object, false, target.parentRotation, target.parentScale);
            return true;
        }
        if (auto* transform = canvas->FindComponentRaw<Component::Transform2D>(&object))
        {
            constexpr ComponentTypeId typeId = MakeStableTypeId(Component::Transform2D::StaticTypeName());
            if (false == MakeComponentAddress(editor.GetObjectIds(), object, *transform, target.address)
                || false == SetPropertyCommand::MakeFieldPath(typeId, field, target.path)
                || false == SetPropertyCommand::ReadValue(transform, typeId, target.path, target.before))
            {
                return false;
            }
            if (target.movesPosition
                && (false == SetPropertyCommand::MakeFieldPath(typeId, "position", target.positionPath)
                    || false == SetPropertyCommand::ReadValue(transform, typeId, target.positionPath, target.positionBefore)))
            {
                return false;
            }
            const Vector2 world = transform->worldValid ? transform->worldPosition : transform->position;
            target.worldPosition = {world.x, world.y, 0.0f};
            target.planar = true;
            target.localPosition = {transform->position.x, transform->position.y, 0.0f};
            target.localAngle = transform->GetRotationRadian();
            target.localScale = {transform->scale.x, transform->scale.y, 1.0f};
            ParentFrame(*canvas, object, true, target.parentRotation, target.parentScale);
            return true;
        }
        return false;
    }

    Bool GizmoEditing::Begin(EditorApplication& editor, GizmoMode mode, const GizmoSubject& primaryStart)
    {
        m_targets.Clear();
        m_mode = mode;
        m_primaryStart = primaryStart;
        const Array<Object::GameObject*> roots = editor.GetTopLevelSelectedObjects();
        for (std::size_t index = 0; index < roots.Size(); ++index)
        {
            if (roots[index] == nullptr)
            {
                continue;
            }
            Target target;
            if (CollectTarget(editor, *roots[index], target))
            {
                m_targets.Add(target);
            }
        }
        m_active = m_targets.Size() != 0;
        return m_active;
    }

    void GizmoEditing::WritePosition(ComponentBase& component, const Target& target, const Vector3& worldDelta)
    {
        // 월드 델타를 부모의 축으로 돌려 로컬 델타로 만든다.
        const Quaternion parentInverse = Conjugate(Normalize(target.parentRotation));
        Vector3 localDelta = Rotate(parentInverse, worldDelta);
        localDelta = {SafeDivide(localDelta.x, target.parentScale.x), SafeDivide(localDelta.y, target.parentScale.y),
            SafeDivide(localDelta.z, target.parentScale.z)};
        const Vector3 position = Add(target.localPosition, localDelta);
        if (target.planar)
        {
            static_cast<Component::Transform2D&>(component).position = {position.x, position.y};
        }
        else
        {
            static_cast<Component::Transform3D&>(component).position = position;
        }
    }

    void GizmoEditing::Write(EditorApplication& editor, const Target& target, const GizmoSubject& primaryNow) const
    {
        ComponentBase* component = ResolveComponent(editor.GetObjectIds(), target.address);
        if (component == nullptr)
        {
            return;
        }
        const Quaternion parentInverse = Conjugate(Normalize(target.parentRotation));
        // 피벗(주된 것의 시작 위치)에서 이 대상까지. 주된 것은 0 이라 제 자리에 남는다.
        const Vector3 offset = Subtract(target.worldPosition, m_primaryStart.position);
        switch (m_mode)
        {
        case GizmoMode::Translate:
        {
            WritePosition(*component, target, Subtract(primaryNow.position, m_primaryStart.position));
            break;
        }
        case GizmoMode::Rotate:
        {
            // 월드에서 Δq 만큼 돌았다. 로컬로는 부모 축에서 본 Δq 다: q_p^-1 Δq q_p q_local.
            const Quaternion delta = Normalize(Multiply(primaryNow.rotation, Conjugate(m_primaryStart.rotation)));
            // 피벗 둘레로 같은 만큼 돈 자리로 옮긴다.
            WritePosition(*component, target, Subtract(Rotate(delta, offset), offset));
            if (target.planar)
            {
                // Z 축 회전의 각. 사원수 (0,0,sin(θ/2),cos(θ/2)).
                const Radian turned = 2.0f * std::atan2(delta.z, delta.w);
                static_cast<Component::Transform2D*>(component)->SetRotationRadian(target.localAngle + turned);
            }
            else
            {
                const Quaternion local = Multiply(Multiply(Multiply(parentInverse, delta), Normalize(target.parentRotation)),
                    target.localRotation);
                static_cast<Component::Transform3D*>(component)->rotation = Normalize(local);
            }
            break;
        }
        case GizmoMode::Scale:
        {
            const Vector3 factor = {SafeDivide(primaryNow.scale.x, m_primaryStart.scale.x),
                SafeDivide(primaryNow.scale.y, m_primaryStart.scale.y),
                SafeDivide(primaryNow.scale.z, m_primaryStart.scale.z)};
            const Vector3 scale = Multiply(target.localScale, factor);
            // 피벗에서의 거리도 주된 것의 축을 따라 같은 배로 늘린다 - 크기 손잡이는 늘 주된 것의 로컬 축이다.
            const Quaternion axes = Normalize(m_primaryStart.rotation);
            const Vector3 alongAxes = Multiply(Rotate(Conjugate(axes), offset), factor);
            WritePosition(*component, target, Subtract(Rotate(axes, alongAxes), offset));
            if (target.planar)
            {
                static_cast<Component::Transform2D*>(component)->scale = {scale.x, scale.y};
            }
            else
            {
                static_cast<Component::Transform3D*>(component)->scale = scale;
            }
            break;
        }
        }
    }

    void GizmoEditing::Apply(EditorApplication& editor, const GizmoSubject& primaryNow)
    {
        if (false == m_active)
        {
            return;
        }
        for (std::size_t index = 0; index < m_targets.Size(); ++index)
        {
            Write(editor, m_targets[index], primaryNow);
        }
    }

    void GizmoEditing::Restore(EditorApplication& editor, ComponentBase& component, const Target& target,
        const SetPropertyCommand::Path& path, const String& before, CompoundCommand* compound)
    {
        String after;
        const Bool read = SetPropertyCommand::ReadValue(&component, target.address.typeId, path, after);
        // 위젯이 쓴 값을 도로 되돌려 놓는다. 쓰는 것은 커맨드의 몫이다 - 그래야 되돌리기가 무엇을
        // 되돌리는지 하나로 남는다.
        SetPropertyCommand::ApplyValue(&component, target.address.typeId, path, before);
        if (compound == nullptr || false == read || after == before)
        {
            return;
        }
        compound->Add(MakeOwnerPtr<SetPropertyCommand>(editor.GetObjectIds(), target.address, path, before, after));
    }

    void GizmoEditing::Commit(EditorApplication& editor)
    {
        if (false == m_active)
        {
            return;
        }
        m_active = false;
        OwnerPtr<CompoundCommand> compound = MakeOwnerPtr<CompoundCommand>("Gizmo");
        for (std::size_t index = 0; index < m_targets.Size(); ++index)
        {
            const Target& target = m_targets[index];
            ComponentBase* component = ResolveComponent(editor.GetObjectIds(), target.address);
            if (component == nullptr)
            {
                continue;
            }
            Restore(editor, *component, target, target.path, target.before, compound.Get());
            if (target.movesPosition)
            {
                Restore(editor, *component, target, target.positionPath, target.positionBefore, compound.Get());
            }
        }
        m_targets.Clear();
        // 빈 묶음은 `CompoundCommand::Execute` 가 거절해 더미에 오르지 않는다 - 움직이지 않은 클릭은 되돌리기가 아니다.
        editor.GetCommands().Execute(std::move(compound));
    }

    void GizmoEditing::Cancel(EditorApplication& editor)
    {
        if (false == m_active)
        {
            return;
        }
        m_active = false;
        for (std::size_t index = 0; index < m_targets.Size(); ++index)
        {
            const Target& target = m_targets[index];
            if (ComponentBase* component = ResolveComponent(editor.GetObjectIds(), target.address))
            {
                Restore(editor, *component, target, target.path, target.before, nullptr);
                if (target.movesPosition)
                {
                    Restore(editor, *component, target, target.positionPath, target.positionBefore, nullptr);
                }
            }
        }
        m_targets.Clear();
    }
}
