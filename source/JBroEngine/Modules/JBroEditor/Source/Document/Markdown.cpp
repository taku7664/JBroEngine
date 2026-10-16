#include <JBro/Editor/Document/Markdown.h>
#include <JBro/Types/Int.h>
#include <md4c.h>
extern "C" {
#include <entity.h>
}
#include <algorithm>
#include <limits>
#include <cstdint>
#include <cctype>

namespace JBro::Markdown
{
    namespace
    {
        void Utf8(String& out, unsigned code)
        {
            if (code == 0 || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
            {
                code = 0xfffd;
            }
            if (code < 0x80)
            {
                out += static_cast<char>(code);
            }
            else if (code < 0x800)
            {
                out += static_cast<char>(0xc0 | (code >> 6));
                out += static_cast<char>(0x80 | (code & 63));
            }
            else if (code < 0x10000)
            {
                out += static_cast<char>(0xe0 | (code >> 12));
                out += static_cast<char>(0x80 | ((code >> 6) & 63));
                out += static_cast<char>(0x80 | (code & 63));
            }
            else
            {
                out += static_cast<char>(0xf0 | (code >> 18));
                out += static_cast<char>(0x80 | ((code >> 12) & 63));
                out += static_cast<char>(0x80 | ((code >> 6) & 63));
                out += static_cast<char>(0x80 | (code & 63));
            }
        }
        String Entity(std::string_view value)
        {
            String out;
            if (value.size() > 3 && value[1] == '#')
            {
                unsigned code = 0;
                std::size_t at = 2;
                unsigned base = 10;
                if (value[at] == 'x' || value[at] == 'X')
                {
                    base = 16;
                    ++at;
                }
                for (; at + 1 < value.size(); ++at)
                {
                    const char c = value[at];
                    const unsigned digit = c >= '0' && c <= '9' ? c - '0'
                        : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : base;
                    if (digit >= base || code > 0x10ffff / base)
                    {
                        code = 0xfffd;
                        break;
                    }
                    code = code * base + digit;
                }
                // CommonMark follows the HTML legacy numeric-reference replacements.
                constexpr unsigned legacy[] = {0x20ac,0x81,0x201a,0x192,0x201e,0x2026,0x2020,0x2021,
                    0x2c6,0x2030,0x160,0x2039,0x152,0x8d,0x17d,0x8f,0x90,0x2018,0x2019,0x201c,
                    0x201d,0x2022,0x2013,0x2014,0x2dc,0x2122,0x161,0x203a,0x153,0x9d,0x17e,0x178};
                if (code >= 0x80 && code <= 0x9f)
                {
                    code = legacy[code - 0x80];
                }
                Utf8(out, code);
            }
            else if (const auto* entity = entity_lookup(value.data(), value.size()))
            {
                Utf8(out, entity->codepoints[0]);
                if (entity->codepoints[1])
                {
                    Utf8(out, entity->codepoints[1]);
                }
            }
            else
            {
                out = value;
            }
            return out;
        }
        String Attribute(const MD_ATTRIBUTE& value)
        {
            String out;
            for (unsigned i = 0; value.size && value.substr_offsets[i] < value.size; ++i)
            {
                const auto begin = value.substr_offsets[i];
                const auto size = value.substr_offsets[i + 1] - begin;
                const auto part = std::string_view(value.text + begin, size);
                out += value.substr_types[i] == MD_TEXT_ENTITY ? Entity(part) : String(part);
            }
            return out;
        }
        Kind Convert(MD_BLOCKTYPE type)
        {
            switch (type)
            {
            case MD_BLOCK_QUOTE: return Kind::Quote;
            case MD_BLOCK_UL: return Kind::UnorderedList;
            case MD_BLOCK_OL: return Kind::OrderedList;
            case MD_BLOCK_LI: return Kind::Item;
            case MD_BLOCK_HR: return Kind::Rule;
            case MD_BLOCK_H: return Kind::Heading;
            case MD_BLOCK_CODE: return Kind::Code;
            case MD_BLOCK_HTML: return Kind::Html;
            case MD_BLOCK_P: return Kind::Paragraph;
            case MD_BLOCK_TABLE: return Kind::Table;
            case MD_BLOCK_THEAD: return Kind::TableHead;
            case MD_BLOCK_TBODY: return Kind::TableBody;
            case MD_BLOCK_TR: return Kind::Row;
            case MD_BLOCK_TH: return Kind::HeaderCell;
            case MD_BLOCK_TD: return Kind::Cell;
            default: return Kind::Document;
            }
        }
        struct SpanState { UInt32 style = 0; String target; Bool image = false; };
        struct Builder
        {
            Document document;
            Array<UInt32> stack;
            Array<SpanState> spans;
            SpanState span;
            const char* sourceBegin = nullptr;
            const char* sourceEnd = nullptr;
            UInt32 codeIndent = 0;
            void AppendCode(std::string_view value)
            {
                const auto address = reinterpret_cast<std::uintptr_t>(value.data());
                const Bool fromSource = address >= reinterpret_cast<std::uintptr_t>(sourceBegin)
                    && address < reinterpret_cast<std::uintptr_t>(sourceEnd);
                if (!fromSource && value.find_first_not_of(' ') == std::string_view::npos)
                {
                    codeIndent += static_cast<UInt32>(value.size());
                    return;
                }
                if (codeIndent > 0)
                {
                    if (fromSource)
                    {
                        // MD4C reports code indentation separately after expanding tabs. Its source
                        // pointer identifies the original whitespace; restore only the retained columns.
                        const char* prefix = value.data();
                        while (prefix > sourceBegin && (prefix[-1] == ' ' || prefix[-1] == '\t'))
                        {
                            --prefix;
                        }
                        UInt32 columns = 0;
                        for (auto at = prefix; at < value.data(); ++at)
                        {
                            columns += *at == '\t' ? 4 - columns % 4 : UInt32(1);
                        }
                        const UInt32 skip = columns > codeIndent ? columns - codeIndent : UInt32(0);
                        UInt32 column = 0;
                        String indentation;
                        for (auto at = prefix; at < value.data(); ++at)
                        {
                            const UInt32 end = column + (*at == '\t' ? 4 - column % 4 : UInt32(1));
                            if (column >= skip)
                            {
                                indentation += *at;
                            }
                            else if (end > skip)
                            {
                                indentation.append((end - skip).Get(), ' ');
                            }
                            column = end;
                        }
                        Append(indentation);
                    }
                    else
                    {
                        Append(String(codeIndent.Get(), ' '));
                    }
                    codeIndent = 0;
                }
                Append(value);
            }
            void Append(std::string_view value)
            {
                if (value.empty() || stack.IsEmpty())
                {
                    return;
                }
                auto& runs = document.blocks[stack[stack.Size() - 1].Get()].runs;
                const UInt32 begin = static_cast<UInt32>(document.text.size());
                document.text.append(value);
                if (!runs.IsEmpty())
                {
                    auto& previous = runs[runs.Size() - 1];
                    if (previous.end == begin && previous.target == span.target && previous.image == span.image
                        && (previous.style == span.style || span.image))
                    {
                        previous.end = static_cast<UInt32>(document.text.size());
                        return;
                    }
                }
                Run run;
                run.begin = begin;
                run.end = static_cast<UInt32>(document.text.size());
                run.style = span.style;
                run.target = span.target;
                run.image = span.image;
                runs.Add(std::move(run));
            }
            void Separator(char value)
            {
                if (!document.text.empty() && document.text.back() != value)
                {
                    document.text += value;
                }
            }
        };
        int EnterBlock(MD_BLOCKTYPE type, void* detail, void* user)
        {
            auto& b = *static_cast<Builder*>(user);
            Block block;
            block.kind = Convert(type);
            block.begin = static_cast<UInt32>(b.document.text.size());
            const UInt32 index = static_cast<UInt32>(b.document.blocks.Size());
            if (!b.stack.IsEmpty())
            {
                block.parent = b.stack[b.stack.Size() - 1];
                b.document.blocks[block.parent.Get()].children.Add(index);
            }
            if (type == MD_BLOCK_H)
            {
                block.level = static_cast<MD_BLOCK_H_DETAIL*>(detail)->level;
            }
            else if (type == MD_BLOCK_CODE)
            {
                block.language = Attribute(static_cast<MD_BLOCK_CODE_DETAIL*>(detail)->lang);
            }
            else if (type == MD_BLOCK_OL)
            {
                block.number = static_cast<MD_BLOCK_OL_DETAIL*>(detail)->start;
            }
            else if (type == MD_BLOCK_LI)
            {
                const auto& item = *static_cast<MD_BLOCK_LI_DETAIL*>(detail);
                block.task = item.is_task != 0;
                block.checked = item.is_task && item.task_mark != ' ';
                auto& parent = b.document.blocks[block.parent.Get()];
                block.number = parent.number;
                if (parent.kind == Kind::OrderedList)
                {
                    ++parent.number;
                }
            }
            else if (type == MD_BLOCK_TH || type == MD_BLOCK_TD)
            {
                block.alignment = static_cast<unsigned>(static_cast<MD_BLOCK_TD_DETAIL*>(detail)->align);
            }
            b.document.blocks.Add(std::move(block));
            b.stack.Add(index);
            return 0;
        }
        int LeaveBlock(MD_BLOCKTYPE type, void*, void* user)
        {
            auto& b = *static_cast<Builder*>(user);
            auto& block = b.document.blocks[b.stack[b.stack.Size() - 1].Get()];
            block.end = static_cast<UInt32>(b.document.text.size());
            if (type == MD_BLOCK_H)
            {
                const String base = Anchor(b.document.text.Substr(block.begin.Get(), (block.end - block.begin).Get()));
                block.anchor = base;
                UInt32 suffix = 0;
                Bool duplicate;
                do
                {
                    duplicate = false;
                    for (const auto& previous : b.document.blocks)
                    {
                        if (&previous != &block && previous.kind == Kind::Heading && previous.anchor == block.anchor)
                        {
                            duplicate = true;
                            block.anchor = base + "-" + std::to_string((++suffix).Get());
                            break;
                        }
                    }
                } while (duplicate);
            }
            b.stack.RemoveAt(b.stack.Size() - 1);
            if (type == MD_BLOCK_TH || type == MD_BLOCK_TD)
            {
                b.document.text += '\t';
            }
            else if (type == MD_BLOCK_TR)
            {
                if (!b.document.text.empty() && b.document.text.back() == '\t')
                {
                    b.document.text.pop_back();
                }
                b.Separator('\n');
            }
            else if (type == MD_BLOCK_P || type == MD_BLOCK_H || type == MD_BLOCK_CODE || type == MD_BLOCK_LI || type == MD_BLOCK_HTML)
            {
                b.Separator('\n');
            }
            return 0;
        }
        int EnterSpan(MD_SPANTYPE type, void* detail, void* user)
        {
            auto& b = *static_cast<Builder*>(user);
            b.spans.Add(b.span);
            switch (type)
            {
            case MD_SPAN_EM: b.span.style |= Italic; break;
            case MD_SPAN_STRONG: b.span.style |= Bold; break;
            case MD_SPAN_DEL: b.span.style |= Strike; break;
            case MD_SPAN_CODE: b.span.style |= InlineCode; break;
            case MD_SPAN_A: b.span.target = Attribute(static_cast<MD_SPAN_A_DETAIL*>(detail)->href); break;
            case MD_SPAN_IMG:
                b.span.target = Attribute(static_cast<MD_SPAN_IMG_DETAIL*>(detail)->src);
                b.span.image = true;
                {
                    Run image;
                    image.begin = image.end = static_cast<UInt32>(b.document.text.size());
                    image.image = true;
                    image.target = b.span.target;
                    b.document.blocks[b.stack[b.stack.Size() - 1].Get()].runs.Add(std::move(image));
                }
                break;
            default: break;
            }
            return 0;
        }
        int LeaveSpan(MD_SPANTYPE, void*, void* user)
        {
            auto& b = *static_cast<Builder*>(user);
            b.span = std::move(b.spans[b.spans.Size() - 1]);
            b.spans.RemoveAt(b.spans.Size() - 1);
            return 0;
        }
        int Text(MD_TEXTTYPE type, const char* text, MD_SIZE size, void* user)
        {
            auto& b = *static_cast<Builder*>(user);
            if (type == MD_TEXT_CODE && !b.stack.IsEmpty()
                && b.document.blocks[b.stack[b.stack.Size() - 1].Get()].kind == Kind::Code)
            {
                b.AppendCode(std::string_view(text, size));
                return 0;
            }
            switch (type)
            {
            case MD_TEXT_NULLCHAR: b.Append("\xef\xbf\xbd"); break;
            case MD_TEXT_ENTITY: b.Append(Entity(std::string_view(text, size))); break;
            case MD_TEXT_SOFTBR: b.Append(" "); break;
            case MD_TEXT_BR: b.Append("\n"); break;
            default: b.Append(std::string_view(text, size)); break;
            }
            return 0;
        }
    }
    Bool Parse(const String& source, Document& result)
    {
        if (source.size() > std::numeric_limits<unsigned>::max())
        {
            return false;
        }
        Builder builder;
        builder.document.source = source;
        builder.sourceBegin = source.data();
        builder.sourceEnd = source.data() + source.size();
        const std::size_t bom = source.StartsWith("\xef\xbb\xbf") ? 3 : 0;
        MD_PARSER parser{};
        parser.flags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS;
        parser.enter_block = EnterBlock;
        parser.leave_block = LeaveBlock;
        parser.enter_span = EnterSpan;
        parser.leave_span = LeaveSpan;
        parser.text = Text;
        if (md_parse(source.data() + bom, static_cast<unsigned>(source.size() - bom), &parser, &builder) != 0)
        {
            return false;
        }
        result = std::move(builder.document);
        return true;
    }
    UInt32 Boundary(const String& text, UInt32 offset)
    {
        offset = static_cast<UInt32>(std::min<std::size_t>(offset.Get(), text.size()));
        while (offset > 0 && offset < text.size() && (static_cast<unsigned char>(text[offset.Get()]) & 0xc0) == 0x80)
        {
            --offset;
        }
        return offset;
    }
    String Copy(const Document& document, UInt32 first, UInt32 last)
    {
        first = Boundary(document.text, first);
        last = Boundary(document.text, last);
        if (last < first)
        {
            std::swap(first, last);
        }
        return document.text.Substr(first.Get(), (last - first).Get());
    }
    Array<Match> Find(const Document& document, const String& query)
    {
        Array<Match> result;
        if (!query.empty())
        {
            std::size_t at = 0;
            while ((at = document.text.find(query, at)) != String::npos)
            {
                result.Add({static_cast<UInt32>(at), static_cast<UInt32>(at + query.size())});
                at += query.size();
            }
        }
        return result;
    }
    String Anchor(const String& label)
    {
        String result;
        Bool space = false;
        for (const unsigned char c : label)
        {
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            {
                space = !result.empty();
            }
            else
            {
                if (space)
                {
                    result += '-';
                    space = false;
                }
                result += static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
            }
        }
        return result;
    }
    String DecodeTarget(const String& target)
    {
        String result;
        auto hex = [](char c) -> Int32 {
            return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        };
        for (std::size_t i = 0; i < target.size(); ++i)
        {
            if (target[i] == '%' && i + 2 < target.size() && hex(target[i + 1]) >= 0 && hex(target[i + 2]) >= 0)
            {
                const char decoded = static_cast<char>(hex(target[i + 1]) * 16 + hex(target[i + 2]));
                if (decoded == 0)
                {
                    return {};
                }
                result += decoded;
                i += 2;
            }
            else
            {
                result += target[i];
            }
        }
        return result;
    }
    Bool IsExternalLink(const String& target)
    {
        const auto colon = target.find(':');
        if (colon == String::npos)
        {
            return false;
        }
        String scheme = target.Substr(0, colon);
        std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return scheme == "mailto" || ((scheme == "http" || scheme == "https") && target.View().substr(colon + 1, 2) == "//");
    }
}
