#include <JBro/Canvas/CanvasReflection.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Runtime/GameObject.h>

namespace JBro
{
    void ForEachReflectedComponent(Canvas& canvas, ReflectedComponentVisitor visitor, void* user)
    {
        if (visitor == nullptr)
        {
            return;
        }
        canvas.ForEachObject([visitor, user](GameObject& object)
        {
            for (const ComponentSlot& slot : object.GetComponents())
            {
                ComponentBase* component = slot.reference.TryGet();
                const PropertyTable* table = PropertyRegistry::Lookup(slot.typeId);
                if (component == nullptr || table == nullptr)
                {
                    continue;
                }
                visitor(*table, *component, user);
            }
        });
    }
}
