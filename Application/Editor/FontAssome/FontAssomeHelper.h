#pragma once
#include <string>
#include <array>

namespace FontAssomeHelper
{
    inline constexpr std::string UnicodeToUTF8(unsigned int codepoint)
    {
        std::string out;

        if (codepoint <= 0x7F)
        {
            out += static_cast<char>(codepoint);
        }
        else if (codepoint <= 0x7FF)
        {
            out += static_cast<char>(0xC0 | (codepoint >> 6));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        else if (codepoint <= 0xFFFF)
        {
            out += static_cast<char>(0xE0 | (codepoint >> 12));
            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        else if (codepoint <= 0x10FFFF)
        {
            out += static_cast<char>(0xF0 | (codepoint >> 18));
            out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        }

        return out;
    }

    inline constexpr std::array<char, 5> UnicodeToUTF8Array(unsigned codepoint)
    {
        std::array<char, 5> out = {};
        if (codepoint <= 0x7F)
        {
            out[0] = static_cast<char>(codepoint);
        }
        else if (codepoint <= 0x7FF)
        {
            out[0] = static_cast<char>(0xC0 | (codepoint >> 6));
            out[1] = static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        else if (codepoint <= 0xFFFF)
        {
            out[0] = static_cast<char>(0xE0 | (codepoint >> 12));
            out[1] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out[2] = static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        else // 최대 0x10FFFF
        {
            out[0] = static_cast<char>(0xF0 | (codepoint >> 18));
            out[1] = static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            out[2] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out[3] = static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        // out[4]는 기본으로 '\0'
        return out;
    }

    constexpr const char* ICON_GEAR = "\xEF\x80\x93"; // f013
    constexpr const char* ICON_GEARS = "\xEF\x80\x85"; // f085

    constexpr const char* ICON_X_MARK = "\xEF\x80\x8D"; // f013

    constexpr const char* ICON_EYE = "\xEF\x81\xAE"; // U+F06E
    constexpr const char* ICON_EYE_SLASH = "\xEF\x81\xB0"; // U+F070

    constexpr const char* ICON_ROTATE_LEFT = "\xEF\x82\xF8"; // f2f8
    constexpr const char* ICON_ROTATE_RIGHT = "\xEF\x82\xF9"; // f2f9

    constexpr const char* ICON_LOCK = "\xEF\x80\xA3"; // f023
	constexpr const char* ICON_UNLOCK = "\xEF\x83\xC1"; // f3c1

    constexpr const char* ICON_LANGUAGE = "\xEF\x81\xAB"; // f1ab

    constexpr const char* ICON_FILTER = "\xEF\x80\xB0"; // f0b0
    constexpr const char* ICON_SLIDER = "\xEF\x81\xDE"; // f1de

    constexpr const char* ICON_SORT = "\xEF\x80\xDC"; // f0dc
    constexpr const char* ICON_SORT_DOWN = "\xEF\x80\xDD"; // f0dd
    constexpr const char* ICON_SORT_UP = "\xEF\x80\xDE"; // f0de

    constexpr const char* ICON_MUSIC = "\xEF\x80\x01"; // f001
    constexpr const char* ICON_VOLUME = "\xEF\x86\xA8"; // f6a8
	constexpr const char* ICON_VOLUME_HIGH = "\xEF\x80\x28"; // f028
	constexpr const char* ICON_VOLUME_LOW = "\xEF\x80\x27"; // f027
	constexpr const char* ICON_VOLUME_OFF = "\xEF\x80\x26"; // f026

    constexpr const char* ICON_PLAY = "\xEF\x80\x4B"; // f04b
    constexpr const char* ICON_STOP = "\xEF\x80\x4D"; // f04d
    constexpr const char* ICON_PAUSE = "\xEF\x80\x4C"; // f04c

    constexpr const char* ICON_BARS = "\xEF\x83\x89"; // U+F0C9
	constexpr const char* ICON_ELLIPSIS_VERTICAL = "\xEF\x85\x82"; // U+F142 <i class="fa-solid fa-ellipsis-vertical"></i>

    constexpr const char* ICON_FILE = "\xef\x85\x9b"; // f15b
    constexpr const char* ICON_FILE_COPY = "\xef\xa4\x8d"; // f24d
    constexpr const char* ICON_FILE_PASTE = "\xef\x83\xaa"; // f0ea
    constexpr const char* ICON_FILE_SAVE = "\xef\x83\x87"; // f0c7
    constexpr const char* ICON_FILE_CODE = "\xef\x87\x89"; // f1c9
    constexpr const char* ICON_FILE_AUDIO = "\xef\x87\x87"; // f1c7
    constexpr const char* ICON_FILE_IMAGE = "\xef\x87\x85"; // f1c5
    constexpr const char* ICON_FILE_DOCS = "\xef\x85\x9c"; // f15c

    constexpr const char* ICON_FOLDER = "\xef\x81\xbb"; // 
    constexpr const char* ICON_FOLDER_OPEN = "\xef\x81\xbc"; // f07c

    constexpr const char* ICON_HELP = "\xef\x81\x99"; // f059
    constexpr const char* ICON_QUESTION = "\xef\x81\x99"; // f059
    constexpr const char* ICON_EDIT = "\xef\x83\x84"; // f044
    constexpr const char* ICON_LIST = "\xEF\x80\xA2"; // f0a2  리스트-alt
    constexpr const char* ICON_IMAGE = "\xEF\x80\xBE"; // f03e  이미지
    constexpr const char* ICON_IMAGES = "\xEF\x8C\x82"; // f302  이미지s
    constexpr const char* ICON_STAR = "\xEF\x80\x85"; // f005  별
    constexpr const char* ICON_HEART = "\xEF\x80\x84"; // f004  하트
    constexpr const char* ICON_THUMBS_UP = "\xEF\x85\xA4"; // f164  따봉
    constexpr const char* ICON_KEYBOARD = "\xef\x84\x9c"; // f11c
    constexpr const char* ICON_SQUARE = "\xef\x83\x88"; // f0c8
    constexpr const char* ICON_CIRCLE = "\xef\x84\x91"; // f111

    constexpr const char* ICON_CIRCLE_ARROW_LEFT = "\xef\x8d\x99"; // f359
    constexpr const char* ICON_CIRCLE_ARROW_RIGHT = "\xef\x8d\x9a"; // f35a
    constexpr const char* ICON_CIRCLE_ARROW_UP = "\xef\x8d\x9b"; // f35b
    constexpr const char* ICON_CIRCLE_ARROW_DOWN = "\xef\x8d\x9c"; // f35c
    constexpr const char* ICON_CIRCLE_X = "\xef\x81\x97"; // f35c

    constexpr const char* ICON_BELL_ON = "\xef\x83\xb3"; // f0f3
    constexpr const char* ICON_BELL_OFF = "\xef\x87\xb6"; // f1f6

    
}
