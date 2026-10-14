#pragma once
#include <JBro/InputTypes/InputRebinding.h>
namespace JBro::ScriptApiCatalog2D
{
    // The status is retained separately from the output binding; Cancelled is not a miss.
    struct InputCapture
    {
        InputCaptureResult result = InputCaptureResult::None;
        InputBinding binding;
    };
    static_assert(std::is_trivially_copyable_v<InputCapture>);
}
