#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <functional>

constexpr UINT MENU_EXIT = 1001;
constexpr UINT MENU_STARTUP = 1002;
constexpr UINT MENU_CONFIGURE = 1003;
constexpr UINT MENU_VOCABULARY = 1004;

class TrayIcon
{
public:
    TrayIcon() = default;
    ~TrayIcon();

    TrayIcon(const TrayIcon &) = delete;
    TrayIcon &operator=(const TrayIcon &) = delete;

    bool initialize(HINSTANCE instance);
    void shutdown();

    // 托盘菜单「生词本」点击回调
    void setOnOpenVocabulary(std::function<void()> callback);

private:
    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    bool iconAdded_ = false;
    std::function<void()> onOpenVocabulary_;

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
