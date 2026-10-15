#include <JBro/Editor/Code/Breakpoints.h>
#include <JBro/Editor/Code/LanguageClient.h>
#include <algorithm>
#include <JBro/Types/ValueMath.h>

namespace JBro::Code
{
    namespace
    {
        UInt64 Fingerprint(const String& text)
        {
            UInt64 result = 14695981039346656037ull;
            for (const unsigned char value : text)
            {
                result = (result ^ value) * 1099511628211ull;
            }
            return result;
        }
    }
    Breakpoints::Document* Breakpoints::Find(const String& path)
    {
        const String normalized = LanguageClient::NormalizePath(path);
        for (auto& document : m_documents)
        {
            if (document.path == normalized)
            {
                return &document;
            }
        }
        return nullptr;
    }
    Array<Breakpoints::Anchor> Breakpoints::Move(const String& before, const String& after, const Array<Anchor>& anchors)
    {
        if (anchors.IsEmpty())
        {
            return {};
        }
        if (after.empty())
        {
            return {};
        }
        const auto lines = [](const String& text) {
            Array<std::string_view> result;
            std::size_t start = 0;
            for (std::size_t at = 0; at < text.size(); ++at)
            {
                if (text[at] == '\n')
                {
                    result.Add(text.View().substr(start, at + 1 - start));
                    start = at + 1;
                }
            }
            result.Add(text.View().substr(start));
            return result;
        };
        const auto oldLines = lines(before), newLines = lines(after);
        std::size_t prefix = 0, suffix = 0;
        while (prefix < oldLines.Size() && prefix < newLines.Size() && oldLines[prefix] == newLines[prefix])
        {
            ++prefix;
        }
        while (suffix < oldLines.Size() - prefix && suffix < newLines.Size() - prefix
            && oldLines[oldLines.Size() - 1 - suffix] == newLines[newLines.Size() - 1 - suffix])
        {
            ++suffix;
        }
        Array<UInt32> mapping;
        mapping.Resize(oldLines.Size());
        for (std::size_t at = 0; at < prefix; ++at)
        {
            mapping[at] = static_cast<UInt32>(at + 1);
        }
        for (std::size_t at = 0; at < suffix; ++at)
        {
            mapping[oldLines.Size() - 1 - at] = static_cast<UInt32>(newLines.Size() - at);
        }
        const std::size_t oldCount = oldLines.Size() - prefix - suffix, newCount = newLines.Size() - prefix - suffix;
        // Keep unchanged lines inside a multi-range source gesture as well as its edges.
        // Very large replacements retain only the certainly unchanged prefix/suffix.
        if (oldCount <= 1024 && newCount <= 1024)
        {
            Array<UInt32> matrix;
            matrix.Resize((oldCount + 1) * (newCount + 1));
            const auto cell = [&](std::size_t oldLine, std::size_t newLine) -> UInt32& {
                return matrix[oldLine * (newCount + 1) + newLine];
            };
            for (std::size_t row = oldCount; row > 0; --row)
            {
                for (std::size_t column = newCount; column > 0; --column)
                {
                    cell(row - 1, column - 1) = oldLines[prefix + row - 1] == newLines[prefix + column - 1]
                        ? cell(row, column) + UInt32(1) : Max(cell(row, column - 1), cell(row - 1, column));
                }
            }
            std::size_t row = 0, column = 0;
            while (row < oldCount || column < newCount)
            {
                const std::size_t oldStart = row, newStart = column;
                while (row < oldCount && column < newCount && oldLines[prefix + row] != newLines[prefix + column])
                {
                    if (cell(row + 1, column) >= cell(row, column + 1))
                    {
                        ++row;
                    }
                    else
                    {
                        ++column;
                    }
                }
                if (row == oldCount || column == newCount)
                {
                    row = oldCount;
                    column = newCount;
                }
                const auto paired = Min(row - oldStart, column - newStart);
                for (std::size_t at = 0; at < paired; ++at)
                {
                    mapping[prefix + oldStart + at] = static_cast<UInt32>(prefix + newStart + at + 1);
                }
                if (row < oldCount && column < newCount)
                {
                    mapping[prefix + row] = static_cast<UInt32>(prefix + column + 1);
                    ++row;
                    ++column;
                }
            }
        }
        Array<Anchor> result;
        for (auto anchor : anchors)
        {
            if (anchor.line > 0 && anchor.line <= mapping.Size() && mapping[anchor.line.Get() - 1] != 0)
            {
                anchor.line = mapping[anchor.line.Get() - 1];
                result.Add(anchor);
            }
        }
        return result;
    }
    void Breakpoints::Open(const String& path, const String& text, UInt64 history)
    {
        Document* document = Find(path);
        if (document == nullptr)
        {
            Document created;
            created.path = LanguageClient::NormalizePath(path);
            m_documents.Add(std::move(created));
            document = &m_documents.Last();
        }
        document->anchors = Move(document->text, text, document->anchors);
        document->text = text;
        document->history = history;
        document->snapshots.Clear();
        document->snapshots.Add({history, Fingerprint(text), document->anchors});
        ++m_revision;
    }
    Array<Breakpoints::Anchor> Breakpoints::Move(const SourceChange& change, const Array<Anchor>& anchors)
    {
        Array<Anchor> result;
        const UInt32 removedLines = change.endLine - change.startLine;
        for (auto anchor : anchors)
        {
            UInt32 row = anchor.line - UInt32(1);
            Bool keep = true;
            if (!change.removesText)
            {
                if (row > change.startLine || (row == change.startLine && change.startsAtLineStart))
                {
                    row += change.insertedLines;
                }
            }
            else if (row > change.endLine || (removedLines > 0 && row == change.endLine && change.endsAtLineStart))
            {
                row = row - removedLines + change.insertedLines;
            }
            else if (row >= change.startLine)
            {
                if (row == change.startLine && (removedLines == 0 || !change.startsAtLineStart || change.insertsText))
                {
                    // Replacing a statement preserves its line; a pure full-line deletion does not.
                }
                else if (change.insertsText && row - change.startLine <= change.insertedLines)
                {
                    row = change.startLine + (row - change.startLine);
                }
                else
                {
                    keep = false;
                }
            }
            if (keep)
            {
                anchor.line = row + UInt32(1);
                result.Add(anchor);
            }
        }
        return result;
    }
    void Breakpoints::Apply(Document& document, const SourceTransaction& transaction)
    {
        Array<Anchor> moved = document.anchors;
        for (const auto& edit : transaction.edits)
        {
            moved = Move(edit, moved);
        }
        if (transaction.empty && transaction.kind == SourceChangeKind::Edit)
        {
            moved.Clear();
        }
        const Snapshot* target = nullptr;
        if (transaction.kind != SourceChangeKind::Edit)
        {
            for (const auto& snapshot : document.snapshots)
            {
                if (snapshot.history == transaction.history)
                {
                    target = &snapshot;
                    break;
                }
            }
        }
        if (target != nullptr)
        {
            Array<Anchor> restored;
            for (const auto& anchor : target->anchors)
            {
                if (!document.disabled.Contains(anchor.id))
                {
                    restored.Add(anchor);
                }
            }
            for (const auto& anchor : moved)
            {
                Bool known = false;
                for (const auto& old : target->anchors)
                {
                    known = known || old.id == anchor.id;
                }
                if (!known && !document.disabled.Contains(anchor.id))
                {
                    restored.Add(anchor);
                }
            }
            document.anchors = std::move(restored);
        }
        else
        {
            for (std::size_t at = document.snapshots.Size(); at > 0; --at)
            {
                if (document.snapshots[at - 1].history > document.history)
                {
                    document.snapshots.RemoveAt(at - 1);
                }
            }
            document.anchors = std::move(moved);
            document.snapshots.Add({transaction.history, 0, document.anchors});
        }
        document.history = transaction.history;
    }
    void Breakpoints::Change(const String& path, const String& text, UInt64 history, const SourceChanges& changes)
    {
        Document* document = Find(path);
        if (document == nullptr || changes.reset)
        {
            Open(path, text, history);
            return;
        }
        if (changes.transactions.IsEmpty())
        {
            if (document->text != text || document->history != history)
            {
                Change(path, text, history);
            }
            return;
        }
        for (const auto& transaction : changes.transactions)
        {
            Apply(*document, transaction);
        }
        document->text = text;
        document->history = history;
        for (auto& snapshot : document->snapshots)
        {
            if (snapshot.history == history)
            {
                snapshot.fingerprint = Fingerprint(text);
            }
        }
        ++m_revision;
    }
    void Breakpoints::Change(const String& path, const String& text, UInt64 history)
    {
        Document* document = Find(path);
        if (document == nullptr)
        {
            Open(path, text, history);
            return;
        }
        if (document->text == text && document->history == history)
        {
            return;
        }
        const Snapshot* previous = nullptr;
        const UInt64 fingerprint = Fingerprint(text);
        for (const auto& snapshot : document->snapshots)
        {
            if (snapshot.history == history && snapshot.fingerprint == fingerprint)
            {
                previous = &snapshot;
                break;
            }
        }
        const auto moved = Move(document->text, text, document->anchors);
        if (previous != nullptr)
        {
            Array<Anchor> restored;
            for (const auto& anchor : previous->anchors)
            {
                if (!document->disabled.Contains(anchor.id))
                {
                    restored.Add(anchor);
                }
            }
            for (const auto& anchor : moved)
            {
                Bool known = false;
                for (const auto& old : previous->anchors)
                {
                    known = known || old.id == anchor.id;
                }
                if (!known && !document->disabled.Contains(anchor.id))
                {
                    restored.Add(anchor);
                }
            }
            document->anchors = std::move(restored);
        }
        else
        {
            for (std::size_t at = document->snapshots.Size(); at > 0; --at)
            {
                if (document->snapshots[at - 1].history > document->history)
                {
                    document->snapshots.RemoveAt(at - 1);
                }
            }
            document->anchors = moved;
            document->snapshots.Add({history, fingerprint, document->anchors});
        }
        document->text = text;
        document->history = history;
        ++m_revision;
    }
    void Breakpoints::Toggle(const String& path, UInt32 line)
    {
        Document* document = Find(path);
        if (document == nullptr || line == 0)
        {
            return;
        }
        Bool removed = false;
        for (std::size_t at = document->anchors.Size(); at > 0; --at)
        {
            if (document->anchors[at - 1].line == line)
            {
                document->disabled.Add(document->anchors[at - 1].id);
                document->anchors.RemoveAt(at - 1);
                removed = true;
            }
        }
        if (!removed)
        {
            document->anchors.Add({m_next++, line});
        }
        for (auto& snapshot : document->snapshots)
        {
            if (snapshot.history == document->history && (snapshot.fingerprint == 0 || snapshot.fingerprint == Fingerprint(document->text)))
            {
                snapshot.anchors = document->anchors;
            }
        }
        ++m_revision;
    }
    Array<UInt32> Breakpoints::Lines(const String& path) const
    {
        Array<UInt32> result;
        const String normalized = LanguageClient::NormalizePath(path);
        for (const auto& document : m_documents)
        {
            if (document.path == normalized)
            {
                for (const auto& anchor : document.anchors)
                {
                    if (!result.Contains(anchor.line))
                    {
                        result.Add(anchor.line);
                    }
                }
                break;
            }
        }
        std::sort(result.begin(), result.end());
        return result;
    }
    void Breakpoints::Clear() { m_documents.Clear(); ++m_revision; }
    UInt64 Breakpoints::GetRevision() const { return m_revision; }
}
