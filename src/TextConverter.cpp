#include "TextConverter.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdexcept>

std::wstring utf8ToWide(std::string_view text)
{
    if (text.empty())
    {
        return {};
    }

    const int size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);

    if (size <= 0)
    {
        throw std::runtime_error(
            "Failed to convert UTF-8 to UTF-16");
    }

    std::wstring result(
        static_cast<std::size_t>(size),
        L'\0');

    const int convertedSize = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        size);

    if (convertedSize <= 0)
    {
        throw std::runtime_error(
            "Failed to convert UTF-8 to UTF-16");
    }

    return result;
}

std::string wideToUtf8(std::wstring_view text)
{
    if (text.empty())
    {
        return {};
    }

    const int size = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);

    if (size <= 0)
    {
        throw std::runtime_error(
            "Failed to convert UTF-16 to UTF-8");
    }

    std::string result(
        static_cast<std::size_t>(size),
        '\0');

    const int convertedSize = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        size,
        nullptr,
        nullptr);

    if (convertedSize <= 0)
    {
        throw std::runtime_error(
            "Failed to convert UTF-16 to UTF-8");
    }

    return result;
}
