#include "Physics2DGeometry.h"

#include <JBro/Canvas/Internal/CanvasAccess.h>
#include <JBro/Framework2D/Component/Transform2D.h>
#include <JBro/Runtime/GameObject.h>

#include <cmath>
#include <JBro/Types/Bool.h>

namespace JBro::Internal
{
    namespace
    {
        // 회전은 라디안으로 모은다 - 저장도 계산도 그 단위다(D-248).
        Bool CalculateWorldMatrix(Canvas& canvas, GameObject* object, Matrix3x2& matrix, Vector2& scale, Radian& rotation)
        {
            Component::Transform2D* local = canvas.FindComponentRaw<Component::Transform2D>(object);
            if (local == nullptr || false == local->IsActiveComponent())
            {
                return false;
            }

            const Matrix3x2 localMatrix = MakeTransformMatrix2D(local->position, local->GetRotationRadian(), local->scale);
            GameObject* parent = object->GetParent();
            Component::Transform2D* parentLocal = canvas.FindComponentRaw<Component::Transform2D>(parent);
            if (parentLocal == nullptr)
            {
                // 부모에 Transform 이 **아예 없으면** 물려받을 자리가 없으므로 자기 로컬이 곧 월드다.
                matrix = localMatrix;
                scale = local->scale;
                rotation = local->GetRotationRadian();
                return true;
            }

            Matrix3x2 parentMatrix;
            Vector2 parentScale;
            Radian parentRotation = 0.0f;
            if (false == CalculateWorldMatrix(canvas, parent, parentMatrix, parentScale, parentRotation))
            {
                return false;
            }
            matrix = MultiplyMatrix3x2(localMatrix, parentMatrix);
            scale = { local->scale.x * parentScale.x, local->scale.y * parentScale.y };
            rotation = local->GetRotationRadian() + parentRotation;
            return true;
        }
    }

    Bool CalculateObjectPose(Canvas& canvas, GameObject* object, ObjectPose& result)
    {
        if (object == nullptr
            || false == CalculateWorldMatrix(canvas, object, result.matrix, result.scale, result.angle))
        {
            return false;
        }
        result.position = { result.matrix.m31, result.matrix.m32 };
        // 각도는 회전의 합이다 - `Transform2D` 의 `worldRotation` 과 캔버스 뷰의 콜라이더 그림이 쓰는 규칙이고, 되쓰기(각도 - 부모 각도)가
        // 그 정확한 역이다. 행렬 첫 행의 각도는 회전 + 비균등 크기인 부모 아래에서 찌그러짐을 섞고, 뒤집힌 부모 아래에서 부호가 바뀐다
        // (physics-plan §4 의 4 (3)). 자리는 행렬로 잰다 - 그것은 찌그러짐이 있어도 맞다.
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
