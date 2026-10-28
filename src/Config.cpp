#include "Config.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <string_view>
#include <system_error>

namespace
{
    std::filesystem::path executableDirectory()
    {
        std::wstring buffer(MAX_PATH, L'\0');

        for (;;)
        {
            const DWORD length = GetModuleFileNameW(
                nullptr,
                buffer.data(),
                static_cast<DWORD>(buffer.size()));

            if (length == 0)
            {
                return {};
            }

            if (length < buffer.size())
            {
                buffer.resize(length);
                break;
            }

            if (buffer.size() >= 32768)
            {
                return {};
            }

            buffer.resize(buffer.size() * 2);
        }

        return std::filesystem::path(buffer).parent_path();
    }

    std::string trimmed(std::string_view text)
    {
        const auto first = text.find_first_not_of(" \t\r\n");

        if (first == std::string_view::npos)
        {
            return {};
        }

        const auto last = text.find_last_not_of(" \t\r\n");

        return std::string(text.substr(first, last - first + 1));
    }
}

std::filesystem::path configFilePath()
{
    return executableDirectory() / L"config.json";
}

Config loadConfig()
{
    Config config;

    std::error_code error;

    if (std::filesystem::exists(configFilePath(), error))
    {
        std::ifstream file(configFilePath(), std::ios::binary);

        if (file)
        {
            try
            {
                const nlohmann::json data =
                    nlohmann::json::parse(file);

                if (data.contains("proxy") &&
                    data["proxy"].is_string())
                {
                    config.proxy =
                        trimmed(data["proxy"].get<std::string>());
                }

                if (data.contains("target_language") &&
                    data["target_language"].is_string())
                {
                    const std::string language = trimmed(
                        data["target_language"].get<std::string>());

                    if (!language.empty())
                    {
                        config.targetLanguage = language;
                    }
                }
            }
            catch (const std::exception &)
            {
                // 配置写错就退回默认值：一个多余的逗号不该让程序起不来。
            }
        }
    }

    // 环境变量优先级最高，临时换代理时不必改文件。
    if (const char *proxy = std::getenv("TRANSLATECPP_PROXY");
        proxy != nullptr)
    {
        const std::string value = trimmed(proxy);

        if (!value.empty())
        {
            config.proxy = value;
        }
    }

    return config;
}
