#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

constexpr UINT MENU_EXIT = 1001;
constexpr UINT MENU_STARTUP = 1002;
constexpr UINT MENU_CONFIGURE = 1003;

class TrayIcon
{
public:
    TrayIcon() = default;
    ~TrayIcon();

    TrayIcon(const TrayIcon &) = delete;
    TrayIcon &operator=(const TrayIcon &) = delete;

    bool initialize(HINSTANCE instance);
    void shutdown();

private:
    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    bool iconAdded_ = false;

    static constexpr wchar_t WINDOW_CLASS_NAME[] =
        L"TranslateCppTrayWindow";

    static constexpr UINT TRAY_CALLBACK_MESSAGE = WM_APP + 10;
    static constexpr UINT TRAY_ICON_ID = 1;

    static LRESULT CALLBACK windowProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam);

    void showContextMenu();
};
