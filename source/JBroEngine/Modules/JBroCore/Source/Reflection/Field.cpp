#include <JBro/Reflection/Field.h>

namespace JBro::Detail
{
    void ApplyFieldEntry(const FieldEntry& entry, PropertyInfo& info, PropertyEditInfo& edit)
    {
        info.name         = NameTable::Get().Intern(entry.name);
        info.type         = &entry.GetType();
        info.Address      = entry.Address;
        info.ConstAddress = entry.ConstAddress;
        info.serialize    = entry.attributes.serialize;

        // 어트리뷰트가 하나도 없으면 편집 메타데이터를 붙이지 않는다.
        // 게임 빌드는 그 필드의 표시 이름·툴팁 문자열을 통째로 안 들고 간다.
        if (false == entry.attributes.HasEditInfo())
        {
            info.edit = nullptr;
            return;
        }

        edit.displayName = entry.attributes.displayName;
        edit.tooltip     = entry.attributes.tooltip;
        edit.category    = entry.attributes.category;
        edit.hasRange    = entry.attributes.hasRange;
        edit.rangeMin    = entry.attributes.rangeMin;
        edit.rangeMax    = entry.attributes.rangeMax;
        edit.editable    = entry.attributes.editable;
        info.edit        = &edit;
    }
}
