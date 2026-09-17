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

namespace JBro
{
    namespace
    {
        constexpr float Pi = 3.14159265358979f;
        constexpr float DegreesToRadians = Pi / 180.0f;
        constexpr float RadiansToDegrees = 180.0f / Pi;

        Quaternion FromDegreesAboutZ(float degrees)
        {
            return FromAxisAngle({0.0f, 0.0f, 1.0f}, degrees * DegreesToRadians);
        }

        // 필드 이름으로 표의 자리를 찾는다. 커맨드의 길은 인덱스다.
        bool FieldPath(ComponentTypeId typeId, const char* name, SetPropertyCommand::Path& path)
        {
            const PropertyTable* table = PropertyRegistry::Lookup(typeId);
            if (table == nullptr)
            {
                return false;
            }
            for (std::uint32_t index = 0; index < table->count; ++index)
            {
                const char* found = NameTable::Get().Resolve(table->properties[index].name);
                if (found != nullptr && std::strcmp(found, name) == 0)
                {
                    path = {};
                    path.indices[0] = index;
                    path.depth = 1;
                    return true;
                }
            }
            return false;
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

        float SafeDivide(float value, float divisor)
        {
            return std::fabs(divisor) > 1.0e-6f ? value / divisor : value;
        }

        // 부모의 월드 회전·스케일. 부모가 없거나 트랜스폼이 없으면 항등이다.
        void ParentFrame(Canvas& canvas, GameObject& object, bool planar, Quaternion& rotation, Vec3& scale)
        {
            rotation = {};
            scale = {1.0f, 1.0f, 1.0f};
            GameObject* parent = object.GetParent();
            if (parent == nullptr)
            {
                return;
            }
            if (planar)
            {
                if (auto* transform = canvas.FindComponentRaw<Component::Transform2D>(parent))
                {
                    rotation = FromDegreesAboutZ(transform->worldRotation);
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

    bool GizmoEditing::ReadSubject(EditorApplication& editor, GameObject& object, GizmoSubject& subject)
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
            const Vec2 position = transform->worldValid ? transform->worldPosition : transform->position;
            const float degrees = transform->worldValid ? transform->worldRotation : transform->rotation;
            subject.position = {position.x, position.y, 0.0f};
            subject.rotation = FromDegreesAboutZ(degrees);
            subject.scale = {transform->scale.x, transform->scale.y, 1.0f};
            subject.planar = true;
            return true;
        }
        return false;
    }

    bool GizmoEditing::CollectTarget(EditorApplication& editor, GameObject& object, Target& target) const
    {
        Canvas* canvas = editor.GetCanvas();
        if (canvas == nullptr)
        {
            return false;
        }
        const char* field = FieldNameFor(m_mode);
        if (auto* transform = canvas->FindComponentRaw<Component::Transform3D>(&object))
        {
            constexpr ComponentTypeId typeId = MakeStableTypeId(Component::Transform3D::StaticTypeName());
            if (false == MakeComponentAddress(editor.GetObjectIds(), object, *transform, target.address)
                || false == FieldPath(typeId, field, target.path)
                || false == SetPropertyCommand::ReadValue(*transform, typeId, target.path, target.before))
            {
                return false;
            }
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
                || false == FieldPath(typeId, field, target.path)
                || false == SetPropertyCommand::ReadValue(*transform, typeId, target.path, target.before))
            {
                return false;
            }
            target.planar = true;
            target.localPosition = {transform->position.x, transform->position.y, 0.0f};
            target.localAngleDegrees = transform->rotation;
            target.localScale = {transform->scale.x, transform->scale.y, 1.0f};
            ParentFrame(*canvas, object, true, target.parentRotation, target.parentScale);
            return true;
        }
        return false;
    }

    bool GizmoEditing::Begin(EditorApplication& editor, GizmoMode mode, const GizmoSubject& primaryStart)
    {
        m_targets.Clear();
        m_mode = mode;
        m_primaryStart = primaryStart;
        const Array<GameObject*> roots = editor.GetTopLevelSelectedObjects();
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

    void GizmoEditing::Write(EditorApplication& editor, const Target& target, const GizmoSubject& primaryNow) const
    {
        ComponentBase* component = ResolveComponent(editor.GetObjectIds(), target.address);
        if (component == nullptr)
        {
            return;
        }
        const Quaternion parentInverse = Conjugate(Normalize(target.parentRotation));
        switch (m_mode)
        {
        case GizmoMode::Translate:
        {
            // 월드 델타를 부모의 축으로 돌려 로컬 델타로 만든다.
            const Vec3 worldDelta = Subtract(primaryNow.position, m_primaryStart.position);
            Vec3 localDelta = Rotate(parentInverse, worldDelta);
            localDelta = {SafeDivide(localDelta.x, target.parentScale.x), SafeDivide(localDelta.y, target.parentScale.y),
                SafeDivide(localDelta.z, target.parentScale.z)};
            const Vec3 position = Add(target.localPosition, localDelta);
            if (target.planar)
            {
                static_cast<Component::Transform2D*>(component)->position = {position.x, position.y};
            }
            else
            {
                static_cast<Component::Transform3D*>(component)->position = position;
            }
            break;
        }
        case GizmoMode::Rotate:
        {
            // 월드에서 Δq 만큼 돌았다. 로컬로는 부모 축에서 본 Δq 다: q_p^-1 Δq q_p q_local.
            const Quaternion delta = Normalize(Multiply(primaryNow.rotation, Conjugate(m_primaryStart.rotation)));
            if (target.planar)
            {
                // Z 축 회전의 각. 사원수 (0,0,sin(θ/2),cos(θ/2)).
                const float degrees = 2.0f * std::atan2(delta.z, delta.w) * RadiansToDegrees;
                static_cast<Component::Transform2D*>(component)->rotation = target.localAngleDegrees + degrees;
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
            const Vec3 factor = {SafeDivide(primaryNow.scale.x, m_primaryStart.scale.x),
                SafeDivide(primaryNow.scale.y, m_primaryStart.scale.y),
                SafeDivide(primaryNow.scale.z, m_primaryStart.scale.z)};
            const Vec3 scale = Multiply(target.localScale, factor);
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
            String after;
            const bool read = SetPropertyCommand::ReadValue(*component, target.address.typeId, target.path, after);
            // 위젯이 쓴 값을 도로 되돌려 놓는다. 쓰는 것은 커맨드의 몫이다 - 그래야 되돌리기가 무엇을
            // 되돌리는지 하나로 남는다.
            SetPropertyCommand::ApplyValue(*component, target.address.typeId, target.path, target.before);
            if (false == read || after == target.before)
            {
                continue;
            }
            compound->Add(MakeOwnerPtr<SetPropertyCommand>(editor.GetObjectIds(), target.address, target.path,
                target.before, after));
        }
        m_targets.Clear();
        if (compound->GetCount() != 0)
        {
            editor.GetCommands().Execute(std::move(compound));
        }
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
                SetPropertyCommand::ApplyValue(*component, target.address.typeId, target.path, target.before);
            }
        }
        m_targets.Clear();
    }
}
