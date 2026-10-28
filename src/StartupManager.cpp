
#include "StartupManager.h"

#include <windows.h>

#include <string>
#include <vector>

namespace
{
    constexpr wchar_t kRegistryPath[] =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

    constexpr wchar_t kValueName[] = L"TranslateCpp";

    // 获取当前 EXE 的绝对路径
    std::wstring getExecutablePath()
    {
        std::vector<wchar_t> buffer(512);

        while (true)
        {
            DWORD length = GetModuleFileNameW(
                nullptr,
                buffer.data(),
                static_cast<DWORD>(buffer.size()));

            if (length == 0)
            {
                return {};
            }

            if (length < buffer.size())
            {
                return std::wstring(buffer.data(), length);
            }

            if (buffer.size() >= 32768)
            {
                return {};
            }

            buffer.resize(buffer.size() * 2);
        }
    }
}

namespace StartupManager
{
    bool isEnabled()
    {
        HKEY hKey = nullptr;

        LONG result = RegOpenKeyExW(
            HKEY_CURRENT_USER,
            kRegistryPath,
            0,
            KEY_QUERY_VALUE,
            &hKey);

        if (result != ERROR_SUCCESS)
        {
            return false;
        }

        DWORD type = 0;
        DWORD dataSize = 0;

        result = RegQueryValueExW(
            hKey,
            kValueName,
            nullptr,
            &type,
            nullptr,
            &dataSize);

        RegCloseKey(hKey);

        // 不存在对应的注册表值
        if (result != ERROR_SUCCESS)
        {
            return false;
        }

        // REG_SZ 和 REG_EXPAND_SZ 均为字符串类型
        return type == REG_SZ || type == REG_EXPAND_SZ;
    }

    bool setEnabled(bool enabled)
    {
        HKEY hKey = nullptr;
        DWORD disposition = 0;

        LONG result = RegCreateKeyExW(
            HKEY_CURRENT_USER,
            kRegistryPath,
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE,
            nullptr,
            &hKey,
            &disposition);

        if (result != ERROR_SUCCESS)
        {
            return false;
        }

        // 关闭自启动：删除注册表值
        if (!enabled)
        {
            result = RegDeleteValueW(hKey, kValueName);

            RegCloseKey(hKey);

            return result == ERROR_SUCCESS ||
                   result == ERROR_FILE_NOT_FOUND;
        }

        // 开启自启动：获取 EXE 绝对路径
        std::wstring exePath = getExecutablePath();

        if (exePath.empty())
        {
            RegCloseKey(hKey);
            return false;
        }

        // 路径加引号，避免路径包含空格时解析错误
        std::wstring command = L"\"" + exePath + L"\"";

        result = RegSetValueExW(
            hKey,
            kValueName,
            0,
            REG_SZ,
            reinterpret_cast<const BYTE *>(command.c_str()),
            static_cast<DWORD>(
                (command.size() + 1) * sizeof(wchar_t)));

        RegCloseKey(hKey);

        return result == ERROR_SUCCESS;
    }
}
