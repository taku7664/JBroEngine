#include "Physics2DGeometry.h"

#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Runtime/GameObject.h>

#include <cmath>

namespace JBro::Internal
{
    namespace
    {
        bool CalculateWorldMatrix(Canvas& canvas, GameObject* object, Matrix3x2& matrix, Vec2& scale)
        {
            Component::Transform2D* local = canvas.FindComponentRaw<Component::Transform2D>(object);
            if (local == nullptr || false == local->IsActiveComponent())
            {
                return false;
            }

            const Matrix3x2 localMatrix = MakeTransformMatrix2D(local->position, local->rotation, local->scale);
            GameObject* parent = object->GetParent();
            Component::Transform2D* parentLocal = canvas.FindComponentRaw<Component::Transform2D>(parent);
            if (parentLocal == nullptr)
            {
                // 부모에 Transform 이 **아예 없으면** 물려받을 자리가 없으므로 자기 로컬이 곧 월드다.
                matrix = localMatrix;
                scale = local->scale;
                return true;
            }

            Matrix3x2 parentMatrix;
            Vec2 parentScale;
            if (false == CalculateWorldMatrix(canvas, parent, parentMatrix, parentScale))
            {
                return false;
            }
            matrix = MultiplyMatrix3x2(localMatrix, parentMatrix);
            scale = { local->scale.x * parentScale.x, local->scale.y * parentScale.y };
            return true;
        }
    }

    bool CalculateObjectPose(Canvas& canvas, GameObject* object, ObjectPose& result)
    {
        if (object == nullptr || false == CalculateWorldMatrix(canvas, object, result.matrix, result.scale))
        {
            return false;
        }
        result.position = { result.matrix.m31, result.matrix.m32 };
        // 첫 행은 (cos·sx, sin·sx) 다. sx 의 부호로 나누어야 뒤집힌 물체의 각도가 π 만큼 튀지 않는다.
        const float sign = result.scale.x < 0.0f ? -1.0f : 1.0f;
        result.angle = std::atan2(result.matrix.m12 * sign, result.matrix.m11 * sign);
        return true;
    }

    Component::BodyType2D GetBodyType(Canvas& canvas, GameObject* object)
    {
        Component::Rigidbody2D* body = canvas.FindComponentRaw<Component::Rigidbody2D>(object);
        if (body == nullptr || false == body->IsActiveComponent())
        {
            return Component::BodyType2D::Static;
        }
        return body->bodyType;
    }
}
