#include <JBro/Host/DebugDrawSystem.h>

#include <JBro/Host/TimeSystem.h>

#include <cmath>

namespace JBro::System
{
    namespace
    {
        bool IsFinite(const DebugLine& line)
        {
            for (int axis = 0; axis < 3; ++axis)
            {
                if (false == std::isfinite(line.from[axis]) || false == std::isfinite(line.to[axis]))
                {
                    return false;
                }
            }
            return std::isfinite(line.thickness) && std::isfinite(line.duration);
        }
    }

    bool DebugDrawSystem::Initialize(std::uint32_t capacity, const TimeSystem* time)
    {
        Shutdown();
        m_entries.Reserve(capacity);
        m_capacity = capacity;
        m_time = time;
        return true;
    }

    void DebugDrawSystem::Shutdown()
    {
        m_entries = {};
        m_capacity = 0;
        m_time = nullptr;
        m_dropped = 0;
        m_rejected = 0;
    }

    void DebugDrawSystem::BeginFrame()
    {
        m_dropped = 0;
        m_rejected = 0;
        const FrameTime* time = m_time != nullptr ? &m_time->GetFrameTime() : nullptr;
        // 게임이 멈춘 프레임(한 프레임 진행이 아닌)은 아무것도 거두지 않는다. 스크립트가 다시 그리지 않으므로 거두면 멈춘 화면이 빈다.
        const bool simulating = m_time == nullptr || m_time->IsSimulating();
        const float delta = time != nullptr ? time->deltaTime : 0.0f;
        const bool fixedStepsRun = time == nullptr || time->fixedStepCount > 0;
        std::uint32_t kept = 0;
        for (std::uint32_t index = 0; index < m_entries.Size(); ++index)
        {
            Entry& entry = m_entries[index];
            bool keep = false;
            if (entry.line.duration > 0.0f)
            {
                entry.line.duration -= delta;
                keep = entry.line.duration > 0.0f;
            }
            else if (entry.fromFixedStep)
            {
                // 고정 스텝에서 그린 것은 이번 프레임에 고정 스텝이 다시 돌 때 비운다 - 스텝이 없는 프레임에도 남아 깜빡이지 않는다.
                keep = false == fixedStepsRun;
            }
            else
            {
                keep = false == simulating;
            }
            if (keep)
            {
                if (kept != index)
                {
                    m_entries[kept] = entry;
                }
                ++kept;
            }
        }
        m_entries.Resize(kept);
    }

    void DebugDrawSystem::Clear()
    {
        m_entries.Clear();
        m_dropped = 0;
        m_rejected = 0;
    }

    std::uint32_t DebugDrawSystem::AddLines(const DebugLine* lines, std::uint32_t count)
    {
        if (lines == nullptr)
        {
            return 0;
        }
        const bool inFixedStep = m_time != nullptr && m_time->GetFrameTime().inFixedStep;
        std::uint32_t accepted = 0;
        for (std::uint32_t index = 0; index < count; ++index)
        {
            const DebugLine& line = lines[index];
            if (false == IsFinite(line))
            {
                ++m_rejected;
                continue;
            }
            // 용량을 넘기면 키우지 않고 버린다. `Reserve` 로 잡은 자리 안에서만 더하므로 매 프레임 할당이 없다.
            if (m_entries.Size() >= m_capacity)
            {
                m_dropped += count - index;
                break;
            }
            Entry entry;
            entry.line = line;
            entry.line.thickness = line.thickness < 0.25f ? 0.25f : (line.thickness > 64.0f ? 64.0f : line.thickness);
            entry.line.duration = line.duration > 0.0f ? line.duration : 0.0f;
            entry.fromFixedStep = inFixedStep;
            m_entries.Add(entry);
            ++accepted;
        }
        return accepted;
    }

    std::uint32_t DebugDrawSystem::GetLineCount() const
    {
        return static_cast<std::uint32_t>(m_entries.Size());
    }

    const DebugLine& DebugDrawSystem::GetLine(std::uint32_t index) const
    {
        return m_entries[index].line;
    }

    std::uint32_t DebugDrawSystem::GetCapacity() const
    {
        return m_capacity;
    }

    std::uint32_t DebugDrawSystem::GetDroppedCount() const
    {
        return m_dropped;
    }

    std::uint32_t DebugDrawSystem::GetRejectedCount() const
    {
        return m_rejected;
    }

    void DebugDrawSystem::SetGameViewVisible(bool visible)
    {
        m_gameViewVisible = visible;
    }

    bool DebugDrawSystem::IsGameViewVisible() const
    {
        return m_gameViewVisible;
    }
}
