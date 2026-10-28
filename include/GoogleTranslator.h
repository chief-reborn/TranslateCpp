#pragma once

#include <string>
#include <string_view>

class GoogleTranslator
{
public:
    // proxy 为空表示直连；否则形如 "127.0.0.1:7897"。
    explicit GoogleTranslator(std::string proxy = {});

    [[nodiscard]]
    std::string translate(
        std::string_view text,
        std::string_view sourceLanguage = "auto",
        std::string_view targetLanguage = "zh-CN");

private:
    // WinHTTP 需要宽字符，构造时转换一次。
    std::wstring proxy_;
};
