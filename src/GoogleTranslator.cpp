#include "GoogleTranslator.h"
#include "TextConverter.h"

#include <windows.h>
#include <winhttp.h>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace
{
    // WinHTTP 的 URL 上限约 32 KB，中文按 3 字节再转义成 9 个字符，
    // 留出余量后限制在这个长度，避免请求直接失败。
    constexpr std::size_t kMaxTextLength = 3000;

    std::string getWindowsErrorMessage(DWORD errorCode)
    {
        LPWSTR buffer = nullptr;

        DWORD size = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER |
                FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            errorCode,
            0,
            reinterpret_cast<LPWSTR>(&buffer),
            0,
            nullptr);

        std::string message;

        if (size > 0 && buffer != nullptr)
        {
            int utf8Size = WideCharToMultiByte(
                CP_UTF8, 0, buffer, static_cast<int>(size),
                nullptr, 0, nullptr, nullptr);

            if (utf8Size > 0)
            {
                message.resize(static_cast<std::size_t>(utf8Size));

                WideCharToMultiByte(
                    CP_UTF8, 0, buffer, static_cast<int>(size),
                    message.data(), utf8Size, nullptr, nullptr);
            }

            LocalFree(buffer);
        }

        return message;
    }

    std::string urlEncode(std::string_view text)
    {
        constexpr char hex[] = "0123456789ABCDEF";

        std::string result;
        result.reserve(text.size() * 3);

        for (unsigned char ch : text)
        {
            const bool unreserved =
                (ch >= 'A' && ch <= 'Z') ||
                (ch >= 'a' && ch <= 'z') ||
                (ch >= '0' && ch <= '9') ||
                ch == '-' || ch == '_' ||
                ch == '.' || ch == '~';

            if (unreserved)
            {
                result.push_back(static_cast<char>(ch));
            }
            else
            {
                result.push_back('%');
                result.push_back(hex[ch >> 4]);
                result.push_back(hex[ch & 0x0F]);
            }
        }

        return result;
    }

    // 去掉用户可能写上的 "http://" 前缀和结尾斜杠，
    // WinHTTP 只接受 "host:port" 形式。
    std::wstring normalizeProxy(std::string_view proxy)
    {
        const auto scheme = proxy.find("://");

        if (scheme != std::string_view::npos)
        {
            proxy.remove_prefix(scheme + 3);
        }

        while (!proxy.empty() && proxy.back() == '/')
        {
            proxy.remove_suffix(1);
        }

        return utf8ToWide(proxy);
    }

    std::wstring userAgent()
    {
#ifndef TRANSLATECPP_VERSION
#define TRANSLATECPP_VERSION "0.1.0"
#endif
        return L"TranslateCpp/" + utf8ToWide(TRANSLATECPP_VERSION);
    }

    class WinHttpHandle
    {
    public:
        explicit WinHttpHandle(HINTERNET handle = nullptr)
            : handle_(handle)
        {
        }

        ~WinHttpHandle()
        {
            if (handle_)
            {
                WinHttpCloseHandle(handle_);
            }
        }

        WinHttpHandle(const WinHttpHandle &) = delete;
        WinHttpHandle &operator=(const WinHttpHandle &) = delete;

        WinHttpHandle(WinHttpHandle &&other) noexcept
            : handle_(other.handle_)
        {
            other.handle_ = nullptr;
        }

        [[nodiscard]]
        HINTERNET get() const
        {
            return handle_;
        }

    private:
        HINTERNET handle_;
    };
}

GoogleTranslator::GoogleTranslator(std::string proxy)
    : proxy_(normalizeProxy(proxy))
{
}

std::string GoogleTranslator::translate(
    std::string_view text,
    std::string_view sourceLanguage,
    std::string_view targetLanguage)
{
    if (text.empty())
    {
        return {};
    }

    if (text.size() > kMaxTextLength)
    {
        throw std::runtime_error(
            "文本太长（超过 3000 字符），请分段后再翻译。");
    }

    const std::string path =
        "/translate_a/single?client=gtx&sl=" +
        urlEncode(sourceLanguage) +
        "&tl=" +
        urlEncode(targetLanguage) +
        "&dt=t&q=" +
        urlEncode(text);

    const std::wstring agent = userAgent();

    WinHttpHandle session(WinHttpOpen(
        agent.c_str(),
        proxy_.empty() ? WINHTTP_ACCESS_TYPE_NO_PROXY
                       : WINHTTP_ACCESS_TYPE_NAMED_PROXY,
        proxy_.empty() ? WINHTTP_NO_PROXY_NAME : proxy_.c_str(),
        WINHTTP_NO_PROXY_BYPASS,
        0));

    if (!session.get())
    {
        throw std::runtime_error(
            "无法初始化网络请求，错误码 " +
            std::to_string(GetLastError()) + "。");
    }

    WinHttpSetTimeouts(
        session.get(),
        5000,
        5000,
        10000,
        10000);

    WinHttpHandle connection(
        WinHttpConnect(
            session.get(),
            L"translate.googleapis.com",
            INTERNET_DEFAULT_HTTPS_PORT,
            0));

    if (!connection.get())
    {
        throw std::runtime_error("无法连接翻译服务。");
    }

    const std::wstring widePath = utf8ToWide(path);

    WinHttpHandle request(
        WinHttpOpenRequest(
            connection.get(),
            L"GET",
            widePath.c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE));

    if (!request.get())
    {
        throw std::runtime_error("无法创建翻译请求。");
    }

    if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
    {
        const DWORD errorCode = GetLastError();

        std::string message =
            "网络请求失败，错误码 " +
            std::to_string(errorCode) + "。";

        const std::string systemMessage = getWindowsErrorMessage(errorCode);

        if (!systemMessage.empty())
        {
            message += systemMessage;
        }

        message += " 检查网络连接，或按需在 config.json 里配置代理。";

        throw std::runtime_error(message);
    }

    if (!WinHttpReceiveResponse(request.get(), nullptr))
    {
        throw std::runtime_error(
            "翻译服务没有响应，检查网络或代理设置。");
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);

    if (!WinHttpQueryHeaders(
            request.get(),
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode,
            &statusSize,
            WINHTTP_NO_HEADER_INDEX))
    {
        throw std::runtime_error("无法读取服务器响应状态。");
    }

    std::string response;

    for (;;)
    {
        DWORD available = 0;

        if (!WinHttpQueryDataAvailable(request.get(), &available))
        {
            throw std::runtime_error("读取响应长度失败。");
        }

        if (available == 0)
        {
            break;
        }

        std::vector<char> buffer(available);
        DWORD downloaded = 0;

        if (!WinHttpReadData(
                request.get(),
                buffer.data(),
                available,
                &downloaded))
        {
            throw std::runtime_error("读取响应内容失败。");
        }

        response.append(buffer.data(), downloaded);
    }

    if (statusCode != 200)
    {
        std::string message =
            "翻译接口返回 HTTP " +
            std::to_string(statusCode) + "。";

        if (!response.empty())
        {
            // 服务端报错通常是简短的文本，带上便于排查。
            message += " " + response.substr(0, 200);
        }

        throw std::runtime_error(message);
    }

    json data;

    try
    {
        data = json::parse(response);
    }
    catch (const json::exception &)
    {
        throw std::runtime_error(
            "无法解析翻译结果，接口可能已变更。");
    }

    // Google 自动检测到的源语言
    std::string detectedLanguage;

    if (data.is_array() &&
        data.size() > 2 &&
        data[2].is_string())
    {
        detectedLanguage = data[2].get<std::string>();
    }

    // 如果当前目标语言是中文，且 Google 检测到源语言是中文，
    // 则重新请求一次，将中文翻译成英语。
    if (sourceLanguage == "auto" &&
        targetLanguage == "zh-CN" &&
        detectedLanguage.starts_with("zh"))
    {
        return translate(text, "auto", "en");
    }

    if (!data.is_array() ||
        data.empty() ||
        !data[0].is_array())
    {
        throw std::runtime_error("翻译结果格式异常。");
    }

    std::string translated;

    for (const auto &segment : data[0])
    {
        if (segment.is_array() &&
            !segment.empty() &&
            segment[0].is_string())
        {
            translated += segment[0].get<std::string>();
        }
    }

    if (translated.empty())
    {
        throw std::runtime_error("没有获得翻译结果，请换一段文本重试。");
    }

    return translated;
}
