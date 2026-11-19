#include <JBro/Framework2D/Component/InputField2D.h>

namespace JBro::Component
{
    void InputField2D::OnDetached()
    {
        // 컴포넌트는 호스트의 캔버스만 만들고 떼므로 여기의 저장소는 호스트의 것이다(`Text2D` 와 같다).
        TextStore::Get().Destroy(text);
        text = {};
        TextStore::Get().Destroy(undoText);
        undoText = {};
        ComponentBase::OnDetached();
    }
}
