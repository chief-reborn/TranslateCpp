#pragma once

#include <string>
#include <string_view>

// UTF-8 转 UTF-16（Windows 宽字符串）
[[nodiscard]]
std::wstring utf8ToWide(std::string_view text);

// UTF-16（Windows 宽字符串）转 UTF-8
[[nodiscard]]
std::string wideToUtf8(std::wstring_view text);
