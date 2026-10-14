#include <JBro/Framework2D/Handle/SpriteRenderer2D.h>

#include <JBro/Framework2D/Component/SpriteRenderer2D.h>

namespace JBro::Handle
{
    Component::SpriteRenderer2D* SpriteRenderer2D::Resolve(const char* operation) const
    {
        return static_cast<Component::SpriteRenderer2D*>(ResolveComponent(StaticTypeName(), operation));
    }

    AssetId SpriteRenderer2D::GetSpriteId() const
    {
        const Component::SpriteRenderer2D* component = Resolve("GetSpriteId");
        if (component == nullptr)
        {
            return {};
        }
        return component->spriteId;
    }

    AssetHandle SpriteRenderer2D::GetSprite() const
    {
        const Component::SpriteRenderer2D* component = Resolve("GetSprite");
        if (component == nullptr)
        {
            return {};
        }
        return component->sprite;
    }

    void SpriteRenderer2D::SetSprite(AssetHandle value)
    {
        Component::SpriteRenderer2D* component = Resolve("SetSprite");
        if (component == nullptr)
        {
            return;
        }
        component->sprite = value;
    }

    AssetId SpriteRenderer2D::GetMaterialId() const
    {
        const Component::SpriteRenderer2D* component = Resolve("GetMaterialId");
        if (component == nullptr)
        {
            return {};
        }
        return component->materialId;
    }

    AssetHandle SpriteRenderer2D::GetMaterial() const
    {
        const Component::SpriteRenderer2D* component = Resolve("GetMaterial");
        if (component == nullptr)
        {
            return {};
        }
        return component->material;
    }

    void SpriteRenderer2D::SetMaterial(AssetHandle value)
    {
        Component::SpriteRenderer2D* component = Resolve("SetMaterial");
        if (component == nullptr)
        {
            return;
        }
        component->material = value;
    }

    Color SpriteRenderer2D::GetTint() const
    {
        const Component::SpriteRenderer2D* component = Resolve("GetTint");
        if (component == nullptr)
        {
            return {};
        }
        return component->tint;
    }

    void SpriteRenderer2D::SetTint(Color value)
    {
        Component::SpriteRenderer2D* component = Resolve("SetTint");
        if (component == nullptr)
        {
            return;
        }
        component->tint = value;
    }

    Component::SpriteSizeMode SpriteRenderer2D::GetSizeMode() const
    {
        const Component::SpriteRenderer2D* component = Resolve("GetSizeMode");
        if (component == nullptr)
        {
            return {};
        }
        return component->sizeMode;
    }

    void SpriteRenderer2D::SetSizeMode(Component::SpriteSizeMode value)
    {
        Component::SpriteRenderer2D* component = Resolve("SetSizeMode");
        if (component == nullptr)
        {
            return;
        }
        component->sizeMode = value;
    }

    Component::SpritePivotMode SpriteRenderer2D::GetPivotMode() const
    {
        const Component::SpriteRenderer2D* component = Resolve("GetPivotMode");
        if (component == nullptr)
        {
            return {};
        }
        return component->pivotMode;
    }

    void SpriteRenderer2D::SetPivotMode(Component::SpritePivotMode value)
    {
        Component::SpriteRenderer2D* component = Resolve("SetPivotMode");
        if (component == nullptr)
        {
            return;
        }
        component->pivotMode = value;
    }

    Vector2 SpriteRenderer2D::GetPivot() const
    {
        const Component::SpriteRenderer2D* component = Resolve("GetPivot");
        if (component == nullptr)
        {
            return {};
        }
        return component->pivot;
    }

    void SpriteRenderer2D::SetPivot(Vector2 value)
    {
        Component::SpriteRenderer2D* component = Resolve("SetPivot");
        if (component == nullptr)
        {
            return;
        }
        component->pivot = value;
    }

    Vector2 SpriteRenderer2D::GetSize() const
    {
        const Component::SpriteRenderer2D* component = Resolve("GetSize");
        if (component == nullptr)
        {
            return {};
        }
        return component->size;
    }

    void SpriteRenderer2D::SetSize(Vector2 value)
    {
        Component::SpriteRenderer2D* component = Resolve("SetSize");
        if (component == nullptr)
        {
            return;
        }
        component->size = value;
    }

    Component::SpriteFlip SpriteRenderer2D::GetFlip() const
    {
        const Component::SpriteRenderer2D* component = Resolve("GetFlip");
        if (component == nullptr)
        {
            return {};
        }
        return component->flip;
    }

    void SpriteRenderer2D::SetFlip(Component::SpriteFlip value)
    {
        Component::SpriteRenderer2D* component = Resolve("SetFlip");
        if (component == nullptr)
        {
            return;
        }
        component->flip = value;
    }

    UInt32 SpriteRenderer2D::GetFrameIndex() const
    {
        const Component::SpriteRenderer2D* component = Resolve("GetFrameIndex");
        if (component == nullptr)
        {
            return {};
        }
        return component->frameIndex;
    }

    void SpriteRenderer2D::SetFrameIndex(UInt32 value)
    {
        Component::SpriteRenderer2D* component = Resolve("SetFrameIndex");
        if (component == nullptr)
        {
            return;
        }
        component->frameIndex = value;
    }

    Int32 SpriteRenderer2D::GetRenderOrder() const
    {
        const Component::SpriteRenderer2D* component = Resolve("GetRenderOrder");
        if (component == nullptr)
        {
            return {};
        }
        return component->renderOrder;
    }

    void SpriteRenderer2D::SetRenderOrder(Int32 value)
    {
        Component::SpriteRenderer2D* component = Resolve("SetRenderOrder");
        if (component == nullptr)
        {
            return;
        }
        component->renderOrder = value;
    }

    Bool SpriteRenderer2D::GetVisible() const
    {
        const Component::SpriteRenderer2D* component = Resolve("GetVisible");
        if (component == nullptr)
        {
            return {};
        }
        return component->visible;
    }

    void SpriteRenderer2D::SetVisible(Bool value)
    {
        Component::SpriteRenderer2D* component = Resolve("SetVisible");
        if (component == nullptr)
        {
            return;
        }
        component->visible = value;
    }
}
