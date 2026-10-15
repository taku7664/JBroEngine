#include "DocumentAssistance.h"
#include <JBro/Editor/Widget/CodeEditor.h>

namespace JBro::Code
{
    namespace
    {
        Bool Same(Position left, Position right)
        {
            return left.line == right.line && left.character == right.character;
        }
        Bool Before(Position left, Position right)
        {
            return left.line < right.line || (left.line == right.line && left.character < right.character);
        }
        Bool Contains(const Range& range, Position point)
        {
            return !Before(point, range.start) && Before(point, range.end);
        }
    }
    DocumentAssistance::DocumentAssistance(Widget::CodeEditor& buffer) : m_buffer(buffer) {}
    void DocumentAssistance::Connect(SafePtr<LanguageClient> language, const String& path)
    {
        Invalidate();
        m_language = language;
        m_path = path;
    }
    void DocumentAssistance::Invalidate()
    {
        if (m_language != nullptr)
        {
            m_language->CancelHover(m_path);
            m_language->CancelReferences(m_path);
        }
        m_hoverRequest = 0;
        m_referencesRequest = 0;
        m_hoverAge = 0.0f;
        m_hoverActive = false;
        m_hoverResolved = false;
        m_buffer.SetHoverTooltip({}, {});
        m_buffer.SetReferenceHighlights({});
    }
    void DocumentAssistance::HighlightReferences(Position definition)
    {
        m_buffer.SetReferenceHighlights({});
        m_referencesRequest = m_language != nullptr && m_language->GetState() == ClientState::Ready
            ? m_language->RequestReferences(m_path, m_language->GetVersion(m_path), definition) : UInt64(0);
    }
    void DocumentAssistance::Update(Float elapsed)
    {
        if (m_language == nullptr)
        {
            return;
        }
        if (m_language->GetState() != ClientState::Ready)
        {
            Invalidate();
            return;
        }
        if (m_hoverActive && m_hoverRequest == 0 && !m_hoverResolved)
        {
            m_hoverAge += elapsed;
            if (m_hoverAge >= 0.25f)
            {
                m_hoverRequest = m_language->RequestHover(m_path, m_language->GetVersion(m_path), m_hoverPosition);
            }
        }
        if (m_hoverRequest != 0)
        {
            const auto* result = m_language->GetHover(m_path);
            if (result != nullptr && result->requestId == m_hoverRequest && result->ready)
            {
                m_hoverRequest = 0;
                m_hoverResolved = true;
                if (result->success && result->version == m_language->GetVersion(m_path)
                    && Contains(result->range, m_hoverPosition))
                {
                    m_hoverRange = result->range;
                    m_buffer.SetHoverTooltip(result->range, result->text);
                }
            }
        }
        if (m_referencesRequest != 0)
        {
            const auto* result = m_language->GetReferences(m_path);
            if (result != nullptr && result->requestId == m_referencesRequest && result->ready)
            {
                m_referencesRequest = 0;
                if (result->success && result->version == m_language->GetVersion(m_path))
                {
                    m_buffer.SetReferenceHighlights(result->ranges);
                }
            }
        }
    }
    void DocumentAssistance::AfterDraw()
    {
        Position point;
        const Bool active = m_language != nullptr && m_language->GetState() == ClientState::Ready
            && m_buffer.GetHoveredPosition(point);
        if (!active)
        {
            if (m_hoverActive)
            {
                m_language->CancelHover(m_path);
                m_hoverActive = false;
                m_hoverResolved = false;
                m_hoverRequest = 0;
                m_buffer.SetHoverTooltip({}, {});
            }
            return;
        }
        if (!m_hoverActive || (!Same(point, m_hoverPosition) && !Contains(m_hoverRange, point)))
        {
            m_language->CancelHover(m_path);
            m_hoverAge = 0.0f;
            m_hoverRequest = 0;
            m_hoverResolved = false;
            m_hoverRange = {};
            m_buffer.SetHoverTooltip({}, {});
        }
        m_hoverActive = true;
        m_hoverPosition = point;
    }
}
