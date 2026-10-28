#include "TrayIcon.h"
#include "Config.h"
#include "StartupManager.h"

#include <shellapi.h>

#include <filesystem>
#include <fstream>
#include <system_error>

namespace
{
    // 与 resources/TranslateCpp.rc.in 中的图标 ID 保持一致
    constexpr int IDI_APP_ICON = 101;

    HICON loadAppIcon(HINSTANCE instance)
    {
        HICON icon = LoadIconW(
            instance,
            MAKEINTRESOURCEW(IDI_APP_ICON));

        if (icon == nullptr)
        {
            icon = LoadIconW(
                nullptr,
                MAKEINTRESOURCEW(32512));
        }

        return icon;
    }

    // 配置文件不存在时先写一份默认内容，用户点开就能直接改。
    bool ensureConfigFile()
    {
        const std::filesystem::path path = configFilePath();

        std::error_code error;

        if (std::filesystem::exists(path, error))
        {
            return true;
        }

        std::ofstream file(path, std::ios::binary);

        if (!file)
        {
            return false;
        }

        file << "{\n"
                "    \"proxy\": \"\",\n"
                "    \"target_language\": \"zh-CN\"\n"
                "}\n";

        return static_cast<bool>(file);
    }
}

TrayIcon::~TrayIcon()
{
    shutdown();
}

bool TrayIcon::initialize(HINSTANCE instance)
{
    if (hwnd_ != nullptr)
    {
        return true;
    }

    instance_ = instance;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = TrayIcon::windowProc;
    wc.hInstance = instance_;
    wc.lpszClassName = WINDOW_CLASS_NAME;

    if (RegisterClassExW(&wc) == 0)
    {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            return false;
        }
    }

    hwnd_ = CreateWindowExW(
        0,
        WINDOW_CLASS_NAME,
        L"TranslateCpp Tray",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,
        nullptr,
        instance_,
        this);

    if (hwnd_ == nullptr)
    {
        return false;
    }

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = TRAY_ICON_ID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = TRAY_CALLBACK_MESSAGE;
    nid.hIcon = loadAppIcon(instance_);

    wcscpy_s(nid.szTip, L"TranslateCpp - 双击 Ctrl+C 翻译");

    if (!Shell_NotifyIconW(NIM_ADD, &nid))
    {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }

    iconAdded_ = true;

    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);

    return true;
}

void TrayIcon::shutdown()
{
    if (iconAdded_ && hwnd_ != nullptr)
    {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd_;
        nid.uID = TRAY_ICON_ID;

        Shell_NotifyIconW(NIM_DELETE, &nid);
        iconAdded_ = false;
    }

    if (hwnd_ != nullptr)
    {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void TrayIcon::showContextMenu()
{
    HMENU menu = CreatePopupMenu();

    if (menu == nullptr)
    {
        return;
    }

    // 根据当前注册表状态决定是否勾选
    UINT startupFlags = MF_STRING;

    if (StartupManager::isEnabled())
    {
        startupFlags |= MF_CHECKED;
    }
    else
    {
        startupFlags |= MF_UNCHECKED;
    }

    AppendMenuW(
        menu,
        startupFlags,
        MENU_STARTUP,
        L"开机自启动");

    AppendMenuW(
        menu,
        MF_STRING,
        MENU_CONFIGURE,
        L"打开配置文件");

    AppendMenuW(
        menu,
        MF_SEPARATOR,
        0,
        nullptr);

    AppendMenuW(
        menu,
        MF_STRING,
        MENU_EXIT,
        L"退出 TranslateCpp");

    POINT point{};
    GetCursorPos(&point);

    SetForegroundWindow(hwnd_);

    TrackPopupMenu(
        menu,
        TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
        point.x,
        point.y,
        0,
        hwnd_,
        nullptr);

    DestroyMenu(menu);

    // 让菜单关闭后的消息得到正常处理。
    PostMessageW(hwnd_, WM_NULL, 0, 0);
}

LRESULT CALLBACK TrayIcon::windowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    TrayIcon *self = nullptr;

    if (message == WM_NCCREATE)
    {
        const auto *createStruct =
            reinterpret_cast<const CREATESTRUCTW *>(lParam);

        self = static_cast<TrayIcon *>(createStruct->lpCreateParams);

        SetWindowLongPtrW(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<TrayIcon *>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self == nullptr)
    {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    switch (message)
    {
    case TRAY_CALLBACK_MESSAGE:
    {
        // 使用 NOTIFYICON_VERSION_4 后，
        // 鼠标事件位于 lParam 的低 16 位。
        const UINT event = LOWORD(lParam);

        if (event == WM_CONTEXTMENU ||
            event == WM_RBUTTONUP)
        {
            self->showContextMenu();
            return 0;
        }

        return 0;
    }

    case WM_COMMAND:
    {
        const UINT command = LOWORD(wParam);

        switch (command)
        {
        case MENU_STARTUP:
        {
            // 读取当前状态，然后切换
            const bool enabled = StartupManager::isEnabled();

            if (!StartupManager::setEnabled(!enabled))
            {
                MessageBoxW(
                    hwnd,
                    L"设置开机自启动失败。",
                    L"TranslateCpp",
                    MB_OK | MB_ICONERROR);
            }

            return 0;
        }

        case MENU_CONFIGURE:
        {
            if (!ensureConfigFile())
            {
                MessageBoxW(
                    hwnd,
                    L"无法创建配置文件，请确认程序所在目录可写。",
                    L"TranslateCpp",
                    MB_OK | MB_ICONERROR);

                return 0;
            }

            const HINSTANCE result = ShellExecuteW(
                hwnd,
                L"open",
                configFilePath().c_str(),
                nullptr,
                nullptr,
                SW_SHOWNORMAL);

            if (reinterpret_cast<INT_PTR>(result) <= 32)
            {
                MessageBoxW(
                    hwnd,
                    L"无法打开配置文件，请手动到程序目录查看 config.json。",
                    L"TranslateCpp",
                    MB_OK | MB_ICONWARNING);
            }

            return 0;
        }

        case MENU_EXIT:
            PostQuitMessage(0);
            return 0;
        }

        break;
    }

    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);

        if (self->hwnd_ == hwnd)
        {
            self->hwnd_ = nullptr;
        }
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}
