#include <JBro/Runtime/DebugLineBatch.h>

#include <JBro/Runtime/SystemContext.h>

namespace JBro::Internal
{
    namespace
    {
        std::uint8_t ToByte(float channel)
        {
            if (false == (channel > 0.0f))
            {
                return 0;
            }
            if (channel >= 1.0f)
            {
                return 255;
            }
            return static_cast<std::uint8_t>(channel * 255.0f + 0.5f);
        }
    }

    DebugLineBatch::DebugLineBatch(const Color& color, float duration, float thickness)
    {
        m_style.color[0] = ToByte(color.R);
        m_style.color[1] = ToByte(color.G);
        m_style.color[2] = ToByte(color.B);
        m_style.color[3] = ToByte(color.A);
        m_style.duration = duration;
        m_style.thickness = thickness;
    }

    DebugLineBatch::~DebugLineBatch()
    {
        Flush();
    }

    void DebugLineBatch::Add(float fromX, float fromY, float fromZ, float toX, float toY, float toZ)
    {
        if (m_count == Capacity)
        {
            Flush();
        }
        DebugLine& line = m_lines[m_count];
        line = m_style;
        line.from[0] = fromX;
        line.from[1] = fromY;
        line.from[2] = fromZ;
        line.to[0] = toX;
        line.to[1] = toY;
        line.to[2] = toZ;
        ++m_count;
    }

    void DebugLineBatch::Flush()
    {
        System::IDebugDrawSystem* system = GetSystemContext().DebugDraw;
        if (system != nullptr && m_count > 0)
        {
            system->AddLines(m_lines, m_count);
        }
        m_count = 0;
    }
}
