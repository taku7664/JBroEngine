# RHI / Renderer Parallel Development

## Purpose

RHI and renderer work must not advance only one backend while leaving the others implicit.

Current primary backends:

- D3D11
- WebGPU
- Vulkan

Mobile work is expected to use Vulkan as the shared native direction, but that does not make Vulkan-only changes acceptable by default. Any renderer feature must be reviewed against all active RHI backends.

## Core Rule

When changing RHI, renderer, shader, render target, texture, or build-time graphics asset behavior:

1. Identify the affected renderer feature.
2. Check D3D11, WebGPU, and Vulkan behavior.
3. Implement equivalent support where the backend is active.
4. If equivalent support is intentionally deferred, document the gap and the reason.
5. Add or update verification so the gap cannot be hidden by one backend passing.

Do not treat "compiled on one RHI" as renderer completion.

## Backend Parity Checklist

For every renderer/RHI feature, check this matrix.

| Area | D3D11 | WebGPU | Vulkan |
| --- | --- | --- | --- |
| Device initialization | Windows native path | Emscripten/WebGPU path | Windows/mobile native Vulkan path |
| Swapchain/surface | HWND swapchain | canvas/surface path | HWND/native surface path |
| Render target | backbuffer + offscreen target | canvas + offscreen target | swapchain + offscreen target |
| Texture upload | initial data upload | initial data upload | staging upload + layout transition |
| Sampler state | filter/address mapping | filter/address mapping | filter/address mapping |
| Buffer update | constant/vertex/index update | uniform/storage update | staging/direct update policy |
| Shader program | HLSL/DX bytecode path | WGSL path | SPIR-V path |
| Pipeline state | vertex layout/blend/topology | vertex layout/blend/topology | vertex layout/blend/topology |
| Draw path | same renderer commands | same renderer commands | same renderer commands |
| Runtime smoke | native game launch | browser launch | native Vulkan launch |

If a row changes for one backend, verify whether the same row must change for the other two.

## Shader Cook Contract

Current state:

- D3D11 compiles HLSL at runtime.
- WebGPU consumes WGSL source at runtime.
- Vulkan consumes SPIR-V binary.
- Shader assets are recognized as asset files, but there is no common build-time shader cook pipeline yet.

Required direction:

- Build/cook must produce backend-specific shader payloads from one shader asset contract.
- Runtime must load cooked shader payloads, not editor source files.
- The renderer must request shader variants by logical shader ID and backend, not by hardcoded source path.
- Built-in shaders and user shader assets should eventually use the same cook contract.

Proposed payload split:

| Backend | Cooked payload |
| --- | --- |
| D3D11 | DXBC/DXIL or validated HLSL fallback |
| WebGPU | validated WGSL payload |
| Vulkan | SPIR-V binary |

Open decision:

- Whether source authoring uses separate per-backend source files first, or a common source language with translation. Do not guess this inside implementation; decide explicitly before building the general shader cooker.

## Runtime Smoke Contract

Runtime smoke should prove that each backend can boot, create GPU resources, execute the common renderer path, and present or produce a visible frame.

Minimum smoke target:

1. Initialize platform surface.
2. Initialize selected RHI.
3. Create renderer.
4. Create texture, sampler, vertex/index buffer, shader program, and pipeline.
5. Clear target with a non-black color.
6. Draw one sprite/quad.
7. Present one or more frames.
8. Report validation/device errors as failure.

Backend-specific smoke entries:

- D3D11: Windows native executable smoke.
- WebGPU: Emscripten/browser smoke.
- Vulkan: Windows native Vulkan smoke first, Android/iOS later through mobile package smoke.

## Documentation Requirement

Every RHI/renderer feature PR or local task should state:

- affected renderer feature
- touched backends
- backend gaps, if any
- verification run per backend
- whether shader cook/runtime smoke needs follow-up

If only one backend was changed, the final report must explicitly say why the other backends did not need changes.

## Current Known Gaps

- No shared shader cook automation exists yet.
- No automated runtime smoke matrix exists yet.
- Vulkan built-in sprite shader uses embedded SPIR-V, while D3D11/WebGPU still use runtime source paths.
- Mobile Vulkan surface/build exists only as foundation; Android/iOS package/runtime smoke is not complete.

These gaps should be reduced before adding higher-level renderer features.
