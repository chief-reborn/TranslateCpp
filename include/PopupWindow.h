#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

class PopupWindow
{
public:
    PopupWindow() = default;

    // 注册窗口类
    bool initialize(HINSTANCE instance);

    // 显示翻译结果；如果窗口已存在，则更新内容
    void show(
        const std::string &original,
        const std::string &translated);

    // 显示失败原因，译文区域用红色标注
    void showError(
        const std::string &original,
        const std::string &message);

    // 关闭悬浮窗
    void close();

private:
    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;

    std::wstring originalText_;
    std::wstring translatedText_;
    bool isError_ = false;

    void showInternal(
        const std::string &original,
        const std::string &translated,
        bool isError);

    // 96 DPI 下的基准尺寸，实际按窗口所在显示器的 DPI 缩放
    static constexpr int BASE_WIDTH = 560;
    static constexpr int BASE_HEIGHT = 300;

    static constexpr wchar_t WINDOW_CLASS_NAME[] =
        L"TranslateCppPopupWindow";

    static LRESULT CALLBACK windowProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam);

    void paint(HWND hwnd);
};
