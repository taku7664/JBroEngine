#include <JBro/Canvas/CanvasReflection.h>

#include <JBro/Canvas/Canvas.h>
#include <JBro/Reflection/PropertyRegistry.h>
#include <JBro/Runtime/GameObject.h>

namespace JBro
{
    void ForEachReflectedInstance(Canvas& canvas, ReflectedInstanceVisitor visitor, void* user)
    {
        if (visitor == nullptr)
        {
            return;
        }
        canvas.ForEachObject([visitor, user](Object::GameObject& object)
        {
            for (const ComponentSlot& slot : object.GetComponents())
            {
                ComponentBase* component = slot.reference.TryGet();
                const PropertyTable* table = PropertyRegistry::Lookup(slot.typeId);
                if (component == nullptr || table == nullptr)
                {
                    continue;
                }
                visitor(*table, component, user);
            }
            for (const ScriptSlot& slot : object.GetScripts())
            {
                GameScriptBase* script = slot.reference.TryGet();
                const PropertyTable* table = PropertyRegistry::Lookup(slot.typeId);
                if (script == nullptr || table == nullptr)
                {
                    continue;
                }
                visitor(*table, script, user);
            }
        });
    }
}
