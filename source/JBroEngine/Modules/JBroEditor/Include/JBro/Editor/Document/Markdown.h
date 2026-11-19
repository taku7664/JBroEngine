#pragma once
#include <JBro/Types/String.h>
#include <JBro/Types/Bool.h>
#include <JBro/Types/UInt.h>
#include <cstdint>

namespace JBro::Markdown
{
    enum class Kind { Document, Paragraph, Heading, Quote, UnorderedList, OrderedList, Item, Rule,
        Code, Html, Table, TableHead, TableBody, Row, HeaderCell, Cell };
    enum Style : std::uint8_t { Normal = 0, Bold = 1, Italic = 2, Strike = 4, InlineCode = 8 };
    struct Run
    {
        UInt32 begin = 0, end = 0, style = 0;
        String target;
        Bool image = false;
    };
    struct Block
    {
        Kind kind = Kind::Document;
        UInt32 parent = 0, begin = 0, end = 0, level = 0, number = 0, alignment = 0;
        Bool task = false, checked = false;
        String language, anchor;
        Array<UInt32> children;
        Array<Run> runs;
    };
    struct Document
    {
        String source, text;
        Array<Block> blocks;
    };
    struct Match { UInt32 begin = 0, end = 0; };
    Bool Parse(const String& source, Document& result);
    Array<Match> Find(const Document& document, const String& query);
    UInt32 Boundary(const String& text, UInt32 offset);
    String Copy(const Document& document, UInt32 first, UInt32 last);
    String Anchor(const String& label);
    String DecodeTarget(const String& target);
    Bool IsExternalLink(const String& target);
}
