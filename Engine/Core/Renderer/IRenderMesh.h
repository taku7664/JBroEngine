#pragma once

#include "Core/Renderer/IRenderResource.h"
#include <cstdint>

class IRHIBuffer;

class IRenderMesh : public IRenderResource
{
public:
	virtual SafePtr<IRHIBuffer> GetVertexBuffer() const = 0;
	virtual SafePtr<IRHIBuffer> GetIndexBuffer() const = 0;
	virtual std::uint32_t GetVertexCount() const = 0;
	virtual std::uint32_t GetIndexCount() const = 0;
};
